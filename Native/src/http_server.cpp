#include "agentflow/http_server.hpp"
#include <initializer_list>
#include <utility>
#include "agentflow/edit_executor.hpp"
#include "agentflow/provider_setup.hpp"
#include "agentflow/graph_service.hpp"
#include "agentflow/a2a_task_control.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <charconv>
#include <iomanip>
#include <random>
#include <sstream>
#include <set>
#include <atomic>
#include <thread>
#include <chrono>
#include <regex>
#if defined(_WIN32)
#include "agentflow/view_sessions.hpp"
#endif

namespace agentflow {
void validate_local_auth_token(std::string_view token) {
    if(token.size()<32 || token.size()>256) throw std::invalid_argument("Authentication token must contain 32-256 printable bytes");
    for(unsigned char c:token) if(c<33 || c>126) throw std::invalid_argument("Authentication token must contain printable bytes without spaces");
}
namespace {
using Json=nlohmann::json;
using Request=httplib::Request;
using Response=httplib::Response;
void reply(Response& response,const Json& body,int status=200) {
    response.status=status;response.set_content(body.dump(),"application/json");
    response.set_header("Cache-Control","no-store");response.set_header("X-Content-Type-Options","nosniff");
}
bool equal_token(const std::string& a,const std::string& b) {
    std::size_t difference=a.size()^b.size();
    for(std::size_t i=0;i<b.size();++i) difference|=static_cast<unsigned char>(b[i])^(i<a.size()?static_cast<unsigned char>(a[i]):0);
    return difference==0;
}
Json body(const Request& request,std::initializer_list<const char*> allowed) {
    if(request.get_header_value("Content-Type")!="application/json") throw std::invalid_argument("Use application/json");
    std::vector<std::set<std::string>> fields;
    auto value=Json::parse(request.body,[&](int depth,Json::parse_event_t event,Json& parsed) {
        if(depth>64) throw std::invalid_argument("Request JSON nesting exceeds limits");
        if(event==Json::parse_event_t::object_start) fields.emplace_back();
        else if(event==Json::parse_event_t::object_end) fields.pop_back();
        else if(event==Json::parse_event_t::key && !fields.back().insert(parsed.get<std::string>()).second) throw std::invalid_argument("Duplicate request field");
        return true;
    });
    if(!value.is_object()) throw std::invalid_argument("Expected a JSON object");
    for(auto it=value.begin();it!=value.end();++it) {
        bool known=false;for(const auto* key:allowed) if(it.key()==key) known=true;
        if(!known) throw std::invalid_argument("Unknown request field");
    }
    return value;
}
std::string string_field(const Json& value,const char* key,std::size_t max=4096) {
    if(!value.contains(key) || !value[key].is_string()) throw std::invalid_argument("Missing or invalid string field");
    auto result=value[key].get<std::string>();
    if(result.empty() || result.size()>max || result.find('\0')!=std::string::npos) throw std::invalid_argument("String field exceeds limits");
    return result;
}
std::string identifier(const std::string& value) {
    if(value.empty() || value.size()>128) throw std::invalid_argument("Invalid ID");
    for(unsigned char c:value) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-')) throw std::invalid_argument("Invalid ID");
    return value;
}
std::int64_t graph_revision(const Json& value,const char* field) {
    if(!value.contains(field) || !value[field].is_number_integer() || value[field]<1 || value[field]>9007199254740991)throw std::invalid_argument("Invalid graph revision");
    return value[field].get<std::int64_t>();
}
std::string new_id() {
    std::random_device random;std::ostringstream value;
    value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i) value<<std::setw(8)<<random();
    return value.str();
}
Json encode(const Session& value) {return {{"id",value.id},{"title",value.title}};}
Json encode(const Run& value) {return {{"id",value.id},{"session_id",value.session_id},{"state",to_string(value.state)},
    {"parent_id",value.parent_id},{"node_id",value.node_id},{"graph_root",value.graph_root}};}
Json graph_record(const GraphRootRecord& root) {
    return {{"run",encode(root.run)},{"graph_id",root.graph_id},{"graph_revision",root.graph_revision},
        {"checkpoint_revision",root.checkpoint_revision},{"checkpoint",Json::parse(root.checkpoint_json)},
        {"spec",Json::parse(root.specification_json)},{"input",root.input_json.empty()?Json(nullptr):Json::parse(root.input_json)}};
}
Json encode(const Event& value) {return {{"seq",value.sequence},{"run_id",value.run_id},{"kind",value.kind},{"data",Json::parse(value.json)}};}
Json encode(const Message& value) {return {{"seq",value.sequence},{"role",value.role},{"data",Json::parse(value.json)}};}
Json encode(const Operation& value) {
    // Keep exact argument bytes: clients must not reserialize numeric/content
    // values and present a different payload as the granted operation.
    return {{"id",value.id},{"run_id",value.spec.run_id},{"workspace_id",value.spec.workspace},
        {"tool",value.spec.tool},{"arguments_json",value.spec.arguments_json},{"resources",value.spec.resources},{"state",to_string(value.state)},
        {"expires_unix_ms",value.expires_unix_ms},{"decision_actor",value.decision_actor},{"result_json",value.result_json}};
}
Json encode(const EditRecoveryInspection& value) {
    const auto& observed=value.observed;
    const char* match=value.match==EditSnapshotMatch::before?"before":(value.match==EditSnapshotMatch::after?"after":"different");
    return {{"operation",encode(value.operation)},{"observed",{{"path",observed.path},{"workspace_id",observed.workspace_id},
        {"file_id",observed.file_id},{"content_sha256",observed.content_sha256},{"size",observed.size}}},
        {"match",match},{"same_file",value.same_file},{"observed_unix_ms",value.observed_unix_ms},
        {"quarantine_released",false}};
}
template<class Values> Json encode_all(const Values& values) {
    auto result=Json::array();for(const auto& value:values) result.push_back(encode(value));return result;
}
std::int64_t cursor(const Request& request) {
    if(!request.has_param("after")) return 0;
    if(request.get_param_value_count("after")!=1) throw std::invalid_argument("Duplicate event cursor");
    const auto value=request.get_param_value("after");std::int64_t number=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
    if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || number<0) throw std::invalid_argument("Invalid event cursor");
    return number;
}
template<class Handler> auto guarded(Handler handler) {
    return [handler=std::move(handler)](const Request& request,Response& response) {
        try {handler(request,response);}
        catch(const NotFound&) {reply(response,{{"detail","Resource not found"}},404);}
        catch(const Conflict& error) {reply(response,{{"detail",error.what()}},409);}
        catch(const PersistenceBusy&) {reply(response,{{"detail","Backend busy; retry later"}},503);}
        catch(const PersistenceClosed&) {reply(response,{{"detail","Backend shutting down"}},503);}
        catch(const RunBusy&) {reply(response,{{"detail","Agent queue is full"}},503);}
        catch(const RunUnavailable&) {reply(response,{{"detail","Agent executor is unavailable"}},503);}
        catch(const ProviderHttpError& error) {
            std::string detail="Model discovery provider returned HTTP "+std::to_string(error.status);
            Json result={{"provider_status",error.status}};
            for(const auto& [name,value]:std::initializer_list<std::pair<const char*,std::string>>{
                {"provider_error_type",error.type},{"provider_error_code",error.code},{"provider_error_param",error.param}}){
                if(!value.empty()){result[name]=value;detail+=" · "+std::string(name)+": "+value;}
            }
            result["detail"]=detail;reply(response,result,502);
        }
        catch(const TransportTimeout&) {reply(response,{{"detail","Model discovery timed out; try again"}},504);}
        catch(const TransportError&) {reply(response,{{"detail","Model discovery failed; check provider access and try again"}},502);}
        catch(const ToolAccessDenied&) {reply(response,{{"detail","Workspace inspection denied"}},403);}
        catch(const ToolFileError&) {reply(response,{{"detail","Workspace file unavailable for inspection"}},409);}
        catch(const Json::exception&) {reply(response,{{"detail","Invalid JSON request"}},400);}
        catch(const std::invalid_argument& error) {reply(response,{{"detail",error.what()}},400);}
        catch(...) {reply(response,{{"detail","Backend operation failed"}},500);}
    };
}
}
struct HttpServer::Impl {
#if defined(_WIN32)
    std::unique_ptr<ViewSessions> view_sessions;
#endif
    PersistenceService& persistence;
    RunExecutor* executor;
    EditRecoveryReader* recovery;
    std::vector<McpServerMetadata> mcp_servers;
    std::vector<ProcessProfileMetadata> process_profiles;
    AgentInstructionMetadata instructions;
    std::string authorization;
    std::atomic<bool> stopping=false;
    std::atomic<std::size_t> active_streams=0;
    struct StreamLease {
        std::atomic<std::size_t>& active;bool acquired=false;
        explicit StreamLease(std::atomic<std::size_t>& count):active(count){}
        bool acquire(){auto count=active.load();while(count<2){if(active.compare_exchange_weak(count,count+1)){acquired=true;return true;}}return false;}
        ~StreamLease(){if(acquired)active.fetch_sub(1);}
    };
    httplib::Server server;
    int port=-1;
    Impl(PersistenceService& store,std::string token,RunExecutor* execution,EditRecoveryReader* inspection,std::vector<McpServerMetadata> configured,std::vector<ProcessProfileMetadata> profiles,AgentInstructionMetadata policy,ProviderSetup* setup,GraphExecution* graphs):persistence(store),executor(execution),recovery(inspection),mcp_servers(std::move(configured)),process_profiles(std::move(profiles)),instructions(policy),authorization("Bearer "+token) {
        validate_local_auth_token(token);
#if defined(_WIN32)
        view_sessions=std::make_unique<ViewSessions>(store,token);
#endif
        server.new_task_queue=[] {return new httplib::ThreadPool(4,4,32);};
        server.set_payload_max_length(1024*1024);
        server.set_read_timeout(5,0);server.set_write_timeout(5,0);server.set_keep_alive_max_count(10);
        // Each keep-alive socket occupies a bounded worker. Release idle
        // clients promptly so a burst from other views/CLI clients can run.
        server.set_keep_alive_timeout(1);
        server.set_pre_routing_handler([this](const Request& request,Response& response) {
            const auto host=request.get_header_value("Host");
            if(request.get_header_value_count("Host")!=1 || (host!="127.0.0.1:"+std::to_string(port) && host!="localhost:"+std::to_string(port))) {
                reply(response,{{"detail","Invalid host"}},400);return httplib::Server::HandlerResponse::Handled;
            }
            if(request.has_header("Origin")) {
                reply(response,{{"detail","Browser origins require a configured view adapter"}},403);return httplib::Server::HandlerResponse::Handled;
            }
            bool authenticated=request.get_header_value_count("Authorization")==1 && equal_token(request.get_header_value("Authorization"),authorization);
#if defined(_WIN32)
            const auto supplied=request.get_header_value("Authorization");
            static const std::regex view_route(R"(^/v1/(health|models|graphs|provider/(configuration|models)|sessions(/[A-Za-z0-9_-]+/(history|runs))?|runs(/[A-Za-z0-9_-]+(/(events|cancel|operations))?)?|graph-runs(/[A-Za-z0-9_-]+(/(children(/[A-Za-z0-9_-]+/history)?|events|human/[A-Za-z0-9_.-]+))?)?|operations/[A-Za-z0-9_-]+(/(inspection|decision))?|view-sessions/(current|revoke))$)");
            if(!authenticated && request.get_header_value_count("Authorization")==1 && supplied.starts_with("View ") && request.get_header_value_count("X-XMind-View-Origin")==1 && std::regex_match(request.path,view_route) && (request.method=="GET"||request.method=="POST")){
                try{authenticated=view_sessions->accepts(std::string_view(supplied).substr(5),request.get_header_value("X-XMind-View-Origin"));}
                catch(const std::invalid_argument&){}
                catch(...){reply(response,{{"detail","View authentication unavailable"}},503);return httplib::Server::HandlerResponse::Handled;}
            }
#endif
            if(!authenticated) {
                response.set_header("WWW-Authenticate","Bearer realm=\"xMind\"");
                reply(response,{{"detail","Authentication required"}},401);return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        server.set_error_handler([](const Request&,Response& response) {
            if(response.body.empty()) reply(response,{{"detail","HTTP request rejected"}},response.status);
        });
        server.set_exception_handler([](const Request&,Response& response,std::exception_ptr) {reply(response,{{"detail","Backend operation failed"}},500);});
#if defined(_WIN32)
        server.Post("/v1/view-sessions",guarded([this](const Request& request,Response& response){
            const auto input=body(request,{"origin"});const auto session=view_sessions->issue(string_field(input,"origin",256));
            reply(response,{{"credential",session.credential},{"expires_unix_ms",session.expires_unix_ms}});
        }));
        server.Post("/v1/view-sessions/current",guarded([this](const Request& request,Response& response){
            body(request,{});const auto supplied=request.get_header_value("Authorization");
            if(!supplied.starts_with("View ")){reply(response,{{"detail","View credential required"}},401);return;}
            reply(response,{{"connected",true}});
        }));
        server.Post("/v1/view-sessions/revoke",guarded([this](const Request& request,Response& response){
            body(request,{});const auto supplied=request.get_header_value("Authorization");
            if(!supplied.starts_with("View ")){reply(response,{{"detail","View credential required"}},401);return;}
            view_sessions->revoke(std::string_view(supplied).substr(5),request.get_header_value("X-XMind-View-Origin"));reply(response,{{"connected",false}});
        }));
#endif
        server.Post("/a2a",[this](const Request& request,Response& response){
            if(request.get_header_value("Content-Type")!="application/json"&&request.get_header_value("Content-Type")!="application/a2a+json"){reply(response,{{"detail","Use application/json"}},415);return;}
            if(!request.params.empty()){reply(response,{{"detail","A2A does not accept query parameters"}},400);return;}
            const auto requested=request.get_header_value("A2A-Version");
            const auto version=request.get_header_value_count("A2A-Version")>1?A2aVersion::unsupported:(requested.empty()||requested=="0.3")?A2aVersion::legacy:requested=="1.0"?A2aVersion::v1:A2aVersion::unsupported;
            A2aStreamStart start;auto lease=std::make_shared<StreamLease>(active_streams);
            const auto result=A2aTaskControl(persistence,executor,version).dispatch(request.body,&start,[&]{return !stopping&&lease->acquire();});
            response.set_header("Cache-Control","no-store");response.set_header("X-Content-Type-Options","nosniff");
            if(version!=A2aVersion::unsupported)response.set_header("A2A-Version",version==A2aVersion::v1?"1.0":"0.3");
            if(start.blocking){
                try{
                    while(!stopping&&!request.is_connection_closed()){
                        const auto run=persistence.run(start.task_id).get();
                        if(run.state==RunState::completed||run.state==RunState::failed||run.state==RunState::cancelled||run.state==RunState::paused){
                            response.set_content(A2aTaskControl(persistence,executor,version).snapshot(start),"application/json");return;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    response.set_content(Json{{"jsonrpc","2.0"},{"id",Json::parse(start.id_json)},{"error",{{"code",-32004},{"message","Blocking interaction interrupted"}}}}.dump(),"application/json");response.status=503;return;
                }catch(...){response.set_content(Json{{"jsonrpc","2.0"},{"id",Json::parse(start.id_json)},{"error",{{"code",-32603},{"message","Blocking interaction interrupted"}}}}.dump(),"application/json");return;}
            }
            if(start.enabled){
                auto stream=std::make_shared<A2aTaskStream>(persistence,start);
                const auto id=start.id_json;
                response.set_chunked_content_provider("text/event-stream",[this,stream,lease,id](std::size_t,httplib::DataSink& sink){
                    auto heartbeat=std::chrono::steady_clock::now()+std::chrono::seconds(1);
                    while(!stopping&&sink.is_writable()){
                        try{
                            const auto batch=stream->poll();
                            for(const auto& item:batch.responses){const auto frame="data: "+item+"\n\n";if(!sink.write(frame.data(),frame.size()))return false;}
                            if(batch.final){sink.done();return true;}
                            if(!batch.responses.empty())return true;
                        }catch(...){
                            const auto frame="data: "+Json{{"jsonrpc","2.0"},{"id",Json::parse(id)},{"error",{{"code",-32603},{"message","Stream interrupted"}}}}.dump()+"\n\n";
                            if(!sink.write(frame.data(),frame.size()))return false;sink.done();return true;
                        }
                        if(std::chrono::steady_clock::now()>=heartbeat){const std::string frame=": keepalive\n\n";return sink.write(frame.data(),frame.size());}
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    return false;
                });return;
            }
            if(result)response.set_content(*result,"application/json");else response.status=204;
        });
        server.Get("/.well-known/agent-card.json",guarded([this](const Request& request,Response& response){
            if(!request.params.empty())throw std::invalid_argument("Agent Card does not accept query parameters");
            const auto requested=request.get_header_value("A2A-Version");
            if(request.get_header_value_count("A2A-Version")>1||(!requested.empty()&&requested!="0.3"&&requested!="1.0")){reply(response,{{"jsonrpc","2.0"},{"id",nullptr},{"error",{{"code",-32009},{"message","Protocol version is not supported"}}}},400);return;}
            auto skills=Json::array();
            if(executor&&executor->available())skills.push_back({{"id","native.agent"},{"name","Native agent"},{"description","Run text requests using the configured model and native agent executor."},{"tags",Json::array({"general-agent"})}});
            if(requested=="1.0"){
                response.set_header("A2A-Version","1.0");
                reply(response,{{"name","xMind"},{"description","Local native agent tasks with persistent conversations and authenticated task controls."},{"version","0.1.0"},
                    {"supportedInterfaces",Json::array({{{"url","http://127.0.0.1:"+std::to_string(port)+"/a2a"},{"protocolBinding","JSONRPC"},{"protocolVersion","1.0"}},{{"url","http://127.0.0.1:"+std::to_string(port)+"/a2a"},{"protocolBinding","JSONRPC"},{"protocolVersion","0.3"}}})},
                    {"capabilities",{{"streaming",true},{"pushNotifications",false},{"extendedAgentCard",false}}},
                    {"securitySchemes",{{"ownerToken",{{"httpAuthSecurityScheme",{{"scheme","Bearer"}}}}}}},{"securityRequirements",Json::array({{{"schemes",{{"ownerToken",{{"list",Json::array()}}}}}}})},
                    {"defaultInputModes",Json::array({"text/plain"})},{"defaultOutputModes",Json::array({"text/plain"})},{"skills",std::move(skills)}});return;
            }
            response.set_header("A2A-Version","0.3");
            reply(response,{{"protocolVersion","0.3.0"},{"name","xMind"},{"description","Local native agent tasks with persistent conversations and authenticated task controls."},
                {"url","http://127.0.0.1:"+std::to_string(port)+"/a2a"},{"preferredTransport","JSONRPC"},{"version","0.1.0"},
                {"capabilities",{{"streaming",true},{"pushNotifications",false},{"stateTransitionHistory",false}}},
                {"securitySchemes",{{"ownerToken",{{"type","http"},{"scheme","bearer"}}}}},{"security",Json::array({{{"ownerToken",Json::array()}}})},
                {"defaultInputModes",Json::array({"text/plain"})},{"defaultOutputModes",Json::array({"text/plain"})},{"skills",std::move(skills)},{"supportsAuthenticatedExtendedCard",false}});
        }));
        server.Get("/v1/health",guarded([this](const Request&,Response& response) {
            const auto models=executor?executor->models():std::vector<std::string>{};
            reply(response,{{"status",executor && !executor->healthy()?"degraded":"ok"},{"api_version","v1"},{"core","C++"},{"storage","xlang3-sqlite"},{"agent_execution",executor && executor->available()},{"model",models.empty()?"":models.front()}});
        }));
        if(setup){
            server.Post("/v1/provider/models",guarded([setup](const Request& request,Response& response){
                if(!request.params.empty() || request.body.size()>65536)throw std::invalid_argument("Model discovery request exceeds limits");
                const auto value=body(request,{"api_key","expected_revision"});
                if(!value.contains("expected_revision") || !value["expected_revision"].is_number_integer() || value["expected_revision"]<0 || value["expected_revision"]>9007199254740991)throw std::invalid_argument("Invalid provider revision");
                const auto key=value.contains("api_key")?string_field(value,"api_key",32768):std::string{};SecretBytes secret({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()});
                auto entries=Json::array();for(const auto& id:setup->discover(std::move(secret),value["expected_revision"].get<std::int64_t>()))entries.push_back({{"id",id}});
                reply(response,{{"models",entries}});
            }));
            const auto metadata=[](const ProviderSetupMetadata& value){return Json{{"revision",value.revision},{"provider",value.provider},{"model",value.model},{"endpoint",value.endpoint},{"configured",value.configured},{"wire",value.wire==ProviderWire::responses?"responses":"chat-completions"}};};
            server.Get("/v1/provider/configuration",guarded([setup,metadata](const Request& request,Response& response){if(!request.params.empty())throw std::invalid_argument("Provider metadata does not accept query parameters");reply(response,metadata(setup->configuration()));}));
            server.Post("/v1/provider/configuration",guarded([setup,metadata](const Request& request,Response& response){
                if(!request.params.empty() || request.body.size()>65536)throw std::invalid_argument("Provider setup request exceeds limits");
                const auto value=body(request,{"model","api_key","expected_revision"});
                if(!value.contains("expected_revision") || !value["expected_revision"].is_number_integer() || value["expected_revision"]<0 || value["expected_revision"]>9007199254740991)throw std::invalid_argument("Invalid provider setup fields");
                const auto key=value.contains("api_key")?string_field(value,"api_key",32768):std::string{};
                SecretBytes secret({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()});
                reply(response,metadata(setup->configure(string_field(value,"model",256),std::move(secret),value["expected_revision"].get<std::int64_t>())));
            }));
        }
        if(graphs){
            server.Get("/v1/graphs",guarded([graphs](const Request& request,Response& response){
                if(!request.params.empty())throw std::invalid_argument("Graph catalog does not accept query parameters");
                auto entries=Json::array();for(const auto& graph:graphs->graphs())entries.push_back({{"id",graph.id},{"revision",graph.revision},{"node_count",graph.node_count},{"executable",graph.executable}});
                reply(response,{{"graphs",entries}});
            }));
            server.Post("/v1/graph-runs",guarded([graphs](const Request& request,Response& response){
                if(!request.params.empty())throw std::invalid_argument("Graph admission does not accept query parameters");
                const auto value=body(request,{"id","session_id","graph_id","graph_revision","prompt","model_id"});
                const auto id=value.contains("id")?identifier(string_field(value,"id",128)):new_id();
                const auto model=value.contains("model_id")?string_field(value,"model_id",256):std::string{};
                reply(response,encode(graphs->submit_graph(id,identifier(string_field(value,"session_id",128)),string_field(value,"graph_id",64),graph_revision(value,"graph_revision"),string_field(value,"prompt",1024*1024),model)),202);
            }));
            server.Get(R"(/v1/graph-runs/([A-Za-z0-9_-]+))",guarded([this](const Request& request,Response& response){
                if(!request.params.empty())throw std::invalid_argument("Graph detail does not accept query parameters");
                reply(response,graph_record(persistence.graph_run(identifier(request.matches[1])).get()));
            }));
            server.Get(R"(/v1/graph-runs/([A-Za-z0-9_-]+)/children)",guarded([this](const Request& request,Response& response){
                if(!request.params.empty())throw std::invalid_argument("Graph children do not accept query parameters");
                const auto id=identifier(request.matches[1]);persistence.graph_run(id).get();reply(response,encode_all(persistence.children(id).get()));
            }));
            server.Get(R"(/v1/graph-runs/([A-Za-z0-9_-]+)/children/([A-Za-z0-9_-]+)/history)",guarded([this](const Request& request,Response& response){
                if(!request.params.empty())throw std::invalid_argument("Graph child history does not accept query parameters");
                const auto root=identifier(request.matches[1]),child=identifier(request.matches[2]);
                persistence.graph_run(root).get();if(persistence.run(child).get().parent_id!=root)throw NotFound("Child does not belong to this graph");
                reply(response,encode_all(persistence.run_history(child).get()));
            }));
            server.Get(R"(/v1/graph-runs/([A-Za-z0-9_-]+)/events)",guarded([this](const Request& request,Response& response){
                if(request.params.size()>1 || (!request.params.empty() && !request.has_param("after")))throw std::invalid_argument("Invalid graph event cursor parameters");
                reply(response,encode_all(persistence.graph_events(identifier(request.matches[1]),cursor(request)).get()));
            }));
            server.Post(R"(/v1/graph-runs/([A-Za-z0-9_-]+)/human/([A-Za-z0-9_.-]+))",guarded([graphs](const Request& request,Response& response){
                if(!request.params.empty() || request.body.size()>131072)throw std::invalid_argument("Graph input exceeds limits");
                const auto value=body(request,{"input","input_json","expected_checkpoint_revision"});
                if(value.contains("input")==value.contains("input_json"))throw std::invalid_argument("Supply one graph human input representation");
                std::string input;
                if(value.contains("input_json"))input=string_field(value,"input_json",65536);
                else {if(!value["input"].is_object())throw std::invalid_argument("Human input must be a JSON object");input=value["input"].dump();}
                reply(response,graph_record(graphs->human_input(identifier(request.matches[1]),request.matches[2],input,"local-owner",graph_revision(value,"expected_checkpoint_revision"))));
            }));
        }
        server.Get("/v1/models",guarded([this](const Request&,Response& response) {
            const auto models=executor?executor->models():std::vector<std::string>{};
            auto entries=Json::array();for(const auto& model:models)entries.push_back({{"id",model}});
            reply(response,{{"default_model",models.empty()?"":models.front()},{"models",entries}});
        }));
        server.Get("/v1/mcp/servers",guarded([this](const Request& request,Response& response) {
            if(!request.params.empty())throw std::invalid_argument("MCP metadata does not accept query parameters");
            Json configured=Json::array();for(const auto& item:mcp_servers)configured.push_back({{"id",item.id},{"revision",item.revision},{"enabled",item.enabled},{"transport","stdio"}});
            reply(response,{{"servers",configured},{"runtime_state","per_run"}});
        }));
        server.Get("/v1/sessions",guarded([this](const Request&,Response& response) {reply(response,encode_all(persistence.sessions().get()));}));
        server.Get("/v1/process/profiles",guarded([this](const Request& request,Response& response) {
            if(!request.params.empty())throw std::invalid_argument("Process metadata does not accept query parameters");
            Json profiles=Json::array();for(const auto& item:process_profiles)profiles.push_back({{"id",item.id},{"revision",item.revision},{"max_timeout_ms",item.max_timeout_ms}});
            reply(response,{{"profiles",profiles},{"runtime_state","per_operation"}});
        }));
        server.Get("/v1/agent/instructions",guarded([this](const Request& request,Response& response) {
            if(!request.params.empty())throw std::invalid_argument("Instruction metadata does not accept query parameters");
            reply(response,{{"revision",instructions.revision},{"byte_count",instructions.byte_count},{"scope","server"},{"runtime_state","startup_snapshot"}});
        }));
        server.Post("/v1/sessions",guarded([this](const Request& request,Response& response) {
            const auto value=body(request,{"id","title"});
            const auto id=value.contains("id")?identifier(string_field(value,"id",128)):new_id();
            reply(response,encode(persistence.create_session(id,value.contains("title")?string_field(value,"title"):"New session").get()),201);
        }));
        server.Get(R"(/v1/sessions/([A-Za-z0-9_-]+)/history)",guarded([this](const Request& request,Response& response) {
            reply(response,encode_all(persistence.history(identifier(request.matches[1])).get()));
        }));
        server.Post(R"(/v1/sessions/([A-Za-z0-9_-]+)/messages)",guarded([this](const Request& request,Response& response) {
            const auto value=body(request,{"role","data"});const auto role=string_field(value,"role",16);
            if(role!="user") throw std::invalid_argument("Client messages must have user role");
            if(!value.contains("data") || !value["data"].is_object()) throw std::invalid_argument("Message data must be an object");
            const auto& data=value["data"];
            if(data.size()!=1 || !data.contains("content")) throw std::invalid_argument("User messages currently require only a text content field");
            const auto content=string_field(data,"content",1024*1024);
            persistence.append_user_message(identifier(request.matches[1]),Json{{"content",content}}.dump()).get();reply(response,{{"saved",true}},201);
        }));
        server.Get(R"(/v1/sessions/([A-Za-z0-9_-]+)/runs)",guarded([this](const Request& request,Response& response) {
            reply(response,encode_all(persistence.runs(identifier(request.matches[1])).get()));
        }));
        server.Get(R"(/v1/runs/([A-Za-z0-9_-]+))",guarded([this](const Request& request,Response& response) {reply(response,encode(persistence.run(identifier(request.matches[1])).get()));}));
        server.Get(R"(/v1/runs/([A-Za-z0-9_-]+)/events)",guarded([this](const Request& request,Response& response) {reply(response,encode_all(persistence.events(identifier(request.matches[1]),cursor(request)).get()));}));
        server.Get(R"(/v1/runs/([A-Za-z0-9_-]+)/operations)",guarded([this](const Request& request,Response& response) {
            reply(response,encode_all(persistence.operations(identifier(request.matches[1])).get()));
        }));
        server.Get(R"(/v1/operations/([A-Za-z0-9_-]+))",guarded([this](const Request& request,Response& response) {
            reply(response,encode(persistence.operation(identifier(request.matches[1])).get()));
        }));
        if(recovery) {
            server.Get(R"(/v1/operations/([A-Za-z0-9_-]+)/inspection)",guarded([this](const Request& request,Response& response) {
                if(!request.params.empty()) throw std::invalid_argument("Inspection accepts only a recorded operation ID");
                reply(response,encode(recovery->inspect_uncertain(identifier(request.matches[1]))));
            }));
        }
        server.Post(R"(/v1/operations/([A-Za-z0-9_-]+)/decision)",guarded([this](const Request& request,Response& response) {
            const auto value=body(request,{"decision"});const auto decision=string_field(value,"decision",8);
            if(decision!="allow" && decision!="deny") throw std::invalid_argument("Decision must be allow or deny");
            // This loopback adapter has one full-access principal authenticated
            // by its configured bearer credential. Never accept actor identity
            // from request JSON. Team principals/scopes require another adapter.
            reply(response,encode(persistence.decide_operation(identifier(request.matches[1]),
                decision=="allow"?OperationDecision::allow:OperationDecision::deny,"local-owner").get()));
        }));
        if(executor) {
            server.Post("/v1/runs",guarded([this](const Request& request,Response& response) {
                const auto value=body(request,{"id","session_id","prompt","model_id"});
                const auto id=value.contains("id")?identifier(string_field(value,"id",128)):new_id();
                const auto model=value.contains("model_id")?string_field(value,"model_id",256):std::string{};
                reply(response,encode(executor->submit_model(id,identifier(string_field(value,"session_id",128)),string_field(value,"prompt",1024*1024),model)),202);
            }));
            server.Post(R"(/v1/runs/([A-Za-z0-9_-]+)/cancel)",guarded([this](const Request& request,Response& response) {
                body(request,{});const auto id=identifier(request.matches[1]);executor->cancel(id);
                reply(response,{{"id",id},{"cancellation_requested",true}},202);
            }));
        }
    }
};
HttpServer::HttpServer(PersistenceService& store,std::string token,RunExecutor* executor,EditRecoveryReader* recovery,std::vector<McpServerMetadata> configured,std::vector<ProcessProfileMetadata> profiles,AgentInstructionMetadata policy,ProviderSetup* setup,GraphExecution* graphs):impl_(std::make_unique<Impl>(store,std::move(token),executor,recovery,std::move(configured),std::move(profiles),policy,setup,graphs)) {}
HttpServer::~HttpServer(){stop();}
int HttpServer::bind(int port) {
    if(port<0 || port>65535 || impl_->port!=-1) throw std::invalid_argument("Invalid bind request");
    const auto bound=port==0?impl_->server.bind_to_any_port("127.0.0.1"):(impl_->server.bind_to_port("127.0.0.1",port)?port:-1);
    if(bound<0) throw std::runtime_error("Cannot bind loopback server");impl_->port=bound;return bound;
}
bool HttpServer::listen() {if(impl_->port<0) throw std::logic_error("Bind before listen");return impl_->server.listen_after_bind();}
void HttpServer::stop() {impl_->stopping=true;impl_->server.stop();}
}
