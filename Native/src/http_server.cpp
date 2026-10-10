#include "agentflow/http_server.hpp"
#include <initializer_list>
#include <utility>
#include <algorithm>
#include "agentflow/edit_executor.hpp"
#include "agentflow/provider_setup.hpp"
#include "agentflow/provider_profile_setup.hpp"
#include "agentflow/graph_service.hpp"
#include "agentflow/dynamic_plan.hpp"
#include "agentflow/a2a_task_control.hpp"
#include "agentflow/backend_owner_control.hpp"
#include "agentflow/mcp_oauth_setup.hpp"
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
#include <optional>
#include <string_view>
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
Json session_skill_metadata(const std::string& session,const WorkspaceSessionSkills& value){
    return {{"session_id",session},{"workspace_id",value.workspace.workspace_id},{"authority_id",value.workspace.authority_id},
        {"revision",value.state.revision},{"ids",value.state.selections.ids},{"manual_ids",value.state.selections.manual_ids},{"editable",value.state.editable}};
}
std::optional<ProviderProfileAdmission> profile_admission(const Json& value){
    const bool named=value.contains("provider_profile_id"),versioned=value.contains("expected_provider_revision");
    if(named!=versioned)throw std::invalid_argument("Provider profile and revision must be supplied together");
    if(!named)return {};
    if(!value["provider_profile_id"].is_string()||!value["expected_provider_revision"].is_number_integer()||value["expected_provider_revision"]<0||value["expected_provider_revision"]>9007199254740991)throw std::invalid_argument("Invalid provider profile admission binding");
    const auto id=value["provider_profile_id"].get<std::string>();
    if(id.size()>256||id.starts_with("sk-")||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)throw std::invalid_argument("Invalid provider profile admission identity");
    return ProviderProfileAdmission{id,value["expected_provider_revision"].get<std::int64_t>()};
}
std::optional<WorkspaceAdmission> workspace_admission(const Json& value){
    const bool named=value.contains("expected_workspace_id"),versioned=value.contains("expected_workspace_authority_id");
    if(named!=versioned)throw std::invalid_argument("Workspace identity and authority must be supplied together");
    if(!named)return {};
    const auto id=string_field(value,"expected_workspace_id",256),authority=string_field(value,"expected_workspace_authority_id",32);
    if(id.empty()||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos||
       authority.size()!=32||authority.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::invalid_argument("Invalid workspace admission identity");
    return WorkspaceAdmission{id,authority};
}
std::string identifier(const std::string& value) {
    if(value.empty() || value.size()>128) throw std::invalid_argument("Invalid ID");
    for(unsigned char c:value) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-')) throw std::invalid_argument("Invalid ID");
    return value;
}
std::string strict_context_model_query(const Request& request) {
    // Params cannot establish uniqueness: pinned cpp-httplib removes identical
    // raw key/value pairs before populating that decoded collection.
    const auto marker=request.target.find('?');
    if(marker==std::string::npos){if(!request.params.empty())throw std::invalid_argument("Invalid context model query");return {};}
    const auto query=std::string_view(request.target).substr(marker+1);
    if(query.empty()||query.size()>1024||query.find('&')!=std::string_view::npos)
        throw std::invalid_argument("Invalid context model query");
    const auto equal=query.find('=');
    if(equal==std::string_view::npos||equal==0||equal+1==query.size()||query.find('=',equal+1)!=std::string_view::npos)
        throw std::invalid_argument("Invalid context model query");
    const auto decode=[](std::string_view encoded,std::size_t limit){
        std::string value;value.reserve(std::min(encoded.size(),limit));
        const auto hex=[](unsigned char byte)->int{if(byte>='0'&&byte<='9')return byte-'0';if(byte>='a'&&byte<='f')return byte-'a'+10;if(byte>='A'&&byte<='F')return byte-'A'+10;return -1;};
        for(std::size_t i=0;i<encoded.size();++i){unsigned char byte=static_cast<unsigned char>(encoded[i]);
            if(byte=='%'){if(encoded.size()-i<3)throw std::invalid_argument("Invalid context model query");const auto high=hex(static_cast<unsigned char>(encoded[i+1])),low=hex(static_cast<unsigned char>(encoded[i+2]));if(high<0||low<0)throw std::invalid_argument("Invalid context model query");byte=static_cast<unsigned char>((high<<4)|low);i+=2;}
            else if(byte=='+')byte=' ';
            if(byte<=32||byte>=127||value.size()>=limit)throw std::invalid_argument("Invalid context model query");value.push_back(static_cast<char>(byte));
        }
        return value;
    };
    const auto key=decode(query.substr(0,equal),8),model=decode(query.substr(equal+1),256);
    if(key!="model_id"||model.empty()||model.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos||
       request.params.size()!=1||!request.has_param("model_id")||request.get_param_value("model_id")!=model)
        throw std::invalid_argument("Invalid context model query");
    return model;
}
std::int64_t graph_revision(const Json& value,const char* field) {
    if(!value.contains(field) || !value[field].is_number_integer() || value[field]<1 || value[field]>9007199254740991)throw std::invalid_argument("Invalid graph revision");
    return value[field].get<std::int64_t>();
}
void plan_input_json(const std::string& source){
    if(source.size()>16384)throw std::invalid_argument("Plan human input exceeds limits");
    std::vector<std::set<std::string>> fields;
    const auto value=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& parsed){
        if(depth>64)throw std::invalid_argument("Plan human input nesting exceeds limits");
        if(event==Json::parse_event_t::object_start)fields.emplace_back();
        else if(event==Json::parse_event_t::object_end)fields.pop_back();
        else if(event==Json::parse_event_t::key&&!fields.back().insert(parsed.get<std::string>()).second)throw std::invalid_argument("Duplicate plan human input field");
        return true;
    });
    if(!value.is_object())throw std::invalid_argument("Plan human input must be a JSON object");
}
std::string new_id() {
    std::random_device random;std::ostringstream value;
    value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i) value<<std::setw(8)<<random();
    return value.str();
}
Json encode(const Session& value) {return {{"id",value.id},{"title",value.title}};}
Json encode(const Run& value) {Json result={{"id",value.id},{"session_id",value.session_id},{"state",to_string(value.state)},
    {"parent_id",value.parent_id},{"node_id",value.node_id},{"graph_root",value.graph_root}};if(!value.provider_context_json.empty())result["provider_context"]=Json::parse(value.provider_context_json);return result;}
Json encode(const OwnedChildRecord& value){Json result={{"run",encode(value.run)},{"kind",value.kind},{"batch_id",value.batch_id},{"task_id",value.task_id},{"preset_id",value.preset_id},{"preset_revision",value.preset_revision}};if(value.kind=="dynamic_agent"){result["plan_id"]=value.plan_id;result["node_label"]=value.node_label;result["claim_id"]=value.claim_id;result["definition_revision"]=value.definition_revision;result["claim_revision"]=value.claim_revision;}return result;}
Json public_usage(const Json& value){
    Json result=Json::object();if(!value.is_object())return result;
    for(const auto* field:{"input_tokens","output_tokens","total_tokens","prompt_tokens","completion_tokens","cached_tokens","reasoning_tokens","cache_read_input_tokens","cache_creation_input_tokens","promptTokenCount","candidatesTokenCount","totalTokenCount","thoughtsTokenCount","cachedContentTokenCount"})if(value.contains(field)&&value[field].is_number_integer()&&value[field]>=0&&value[field]<=9007199254740991)result[field]=value[field];
    for(const auto* field:{"input_tokens_details","output_tokens_details","prompt_tokens_details","completion_tokens_details"})if(value.contains(field)&&value[field].is_object()){
        auto details=Json::object();for(const auto* key:{"cached_tokens","reasoning_tokens","audio_tokens","accepted_prediction_tokens","rejected_prediction_tokens"})if(value[field].contains(key)&&value[field][key].is_number_integer()&&value[field][key]>=0&&value[field][key]<=9007199254740991)details[key]=value[field][key];if(!details.empty())result[field]=std::move(details);
    }return result;
}
Json public_response(const std::string& actual){
    const auto source=Json::parse(actual);Json result=Json::object();
    if(source.contains("model")&&source["model"].is_string()&&source["model"].get_ref<const std::string&>().size()<=256)result["model"]=source["model"];
    if(source.contains("usage")){auto usage=public_usage(source["usage"]);if(!usage.empty())result["usage"]=std::move(usage);}
    for(const auto* field:{"elapsed_ms","first_token_ms"})if(source.contains(field)&&source[field].is_number_integer()&&source[field]>=0&&source[field]<=9007199254740991)result[field]=source[field];
    return result;
}
Json context_record(const ContextControlSnapshot& value){
    Json result={{"session_id",value.session_id},{"model_id",value.model_id},{"enabled",value.enabled},
        {"automatic",value.automatic},{"head_revision",value.head_revision},{"source_watermark",value.source_watermark},
        {"manual",nullptr},{"checkpoint",nullptr}};
    if(value.manual)result["manual"]={{"id",value.manual->id},{"state",value.manual->state}};
    if(value.checkpoint){const auto& c=*value.checkpoint;const auto supplied=Json::parse(c.actual_usage_json);
        result["checkpoint"]={{"id",c.id},{"provider_elapsed_ms",c.provider_elapsed_ms},
            {"preparation_elapsed_ms",c.preparation_elapsed_ms?Json(*c.preparation_elapsed_ms):Json(nullptr)},
            {"usage",supplied.is_null()?Json(nullptr):public_usage(supplied)}};
    }
    return result;
}
Json public_plan(const DynamicPlanRecord& value){
    const auto decision=inspect_dynamic_plan(value);Json result={{"id",value.id},{"root_run_id",value.root_run_id},{"revision",value.revision},{"state_sequence",value.state_sequence},{"state",value.state},{"ready",decision.ready},{"blocked",decision.blocked},{"claimed",decision.claimed},{"waiting_human",decision.waiting_human},{"finished",decision.finished},{"report_ready",decision.report_ready},{"halted",decision.halted},{"retired_labels",value.retired_labels},{"planned_children_reserved",value.planned_children_reserved},{"planned_humans_reserved",value.planned_humans_reserved},{"humans_published",value.humans_published},{"nodes",Json::array()}};
    auto state=[](DynamicNodeState value){switch(value){case DynamicNodeState::pending:return "pending";case DynamicNodeState::blocked:return "blocked";case DynamicNodeState::claimed:return "claimed";case DynamicNodeState::waiting_human:return "waiting_human";case DynamicNodeState::settled:return "settled";case DynamicNodeState::skipped:return "skipped";case DynamicNodeState::cancelled:return "cancelled";case DynamicNodeState::uncertain:return "uncertain";}throw DatabaseError("Invalid actual plan node state");};
    for(const auto& n:value.nodes){Json node={{"id",n.definition.label},{"type",n.definition.kind==DynamicNodeKind::agent?"agent":"human"},{"backend_node_id",n.backend_node_id},{"definition_revision",n.definition_revision},{"state",state(n.state)},{"protected",n.protected_definition||n.claim_revision>0||n.state==DynamicNodeState::skipped||n.state==DynamicNodeState::cancelled},{"depends_on",Json::array()},{"effect_state",n.effect_state}};
        if(n.definition.kind==DynamicNodeKind::agent){node["objective"]=n.definition.objective;node["preset"]=n.definition.preset_id;node["preset_revision"]=n.definition.preset_revision;}else node["question"]=n.definition.question;
        for(const auto& edge:n.definition.dependencies)node["depends_on"].push_back({{"task",edge.task},{"require",edge.require==DynamicDependencyRequirement::success?"success":"observed"}});
        if(n.claim_revision){node["claim_revision"]=n.claim_revision;node["claim_id"]=n.claim_id;}if(!n.child_run_id.empty())node["child_run_id"]=n.child_run_id;if(!n.child_state.empty())node["child_state"]=n.child_state;if(!n.human_request_id.empty())node["human_request_id"]=n.human_request_id;if(!n.outcome_json.empty())node["outcome_json"]=n.outcome_json;if(n.settled_event_seq)node["settled_event_seq"]=*n.settled_event_seq;result["nodes"].push_back(std::move(node));
    }return result;
}
Json public_plan_policy(const DynamicPlanCapabilities& c){
    auto presets=Json::array();for(const auto& p:c.presets)presets.push_back({{"id",p.id},{"revision",p.revision},{"readonly",p.readonly},{"turn_limit",p.turn_limit}});
    return {{"revision",c.revision},{"catalogue_finalized",c.catalogue_finalized},{"limits",{{"max_nodes",c.max_nodes},{"max_revisions",c.max_revisions},{"max_humans",c.max_humans},{"change_bytes",c.change_bytes},{"human_expiry_ms",c.human_expiry_ms},{"max_parent_turns",c.max_parent_turns}}},{"presets",std::move(presets)}};
}
Json public_plan_budget(const RootBudgetRecord& b){return {{"policy_id",b.spec.policy_id},{"policy_revision",b.spec.policy_revision},{"revision",b.revision},{"max_children",b.spec.max_children},{"max_parallel",b.spec.max_parallel},{"max_model_calls",b.spec.max_model_calls},{"wall_limit_ms",b.spec.wall_limit_ms},{"children_admitted",b.children_admitted},{"planned_children_reserved",b.planned_children_reserved},{"model_calls_reserved",b.model_calls_reserved},{"parent_calls_held",b.parent_calls_held},{"parent_model_calls_reserved",b.parent_model_calls_reserved}};}
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
template<class Handler> auto guard_errors(Handler handler) {
    return [handler=std::move(handler)](const Request& request,Response& response) {
        try {handler(request,response);}
        catch(const NotFound&) {reply(response,{{"detail","Resource not found"}},404);}
        catch(const Conflict& error) {reply(response,{{"detail",error.what()}},409);}
        catch(const PersistenceBusy&) {reply(response,{{"detail","Backend busy; retry later"}},503);}
        catch(const PersistenceClosed&) {reply(response,{{"detail","Backend shutting down"}},503);}
        catch(const BackendQuiesced&) {reply(response,{{"detail","Native backend is quiesced; admission is closed"}},503);}
        catch(const RuntimeGenerationDenied&) {reply(response,{{"detail","Native runtime package cannot be qualified"}},409);}
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
        catch(const ToolGuidanceChanged&) {reply(response,{{"detail","Selected workspace guidance changed or is unavailable; refresh skills"}},409);}
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
    BackendOwnerControl* owner_control;
    McpOAuthSetup* mcp_oauth;
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
    bool owner_admission_open()const{
#if defined(_WIN32)
        if(owner_control)return !owner_control->status().quiesced;
#endif
        return true;
    }
    template<class Handler> auto guarded(Handler handler){return guard_errors([this,handler=std::move(handler)](const Request& request,Response& response){
#if defined(_WIN32)
        std::optional<std::shared_lock<std::shared_mutex>> admission;
        // Context status can start counting/index maintenance even on GET.
        if(owner_control&&(request.method!="GET"||std::regex_match(request.path,std::regex(R"(^/v1/sessions/[A-Za-z0-9_-]+/context$)"))))admission.emplace(owner_control->admit());
#endif
        handler(request,response);
    });}
    void event_stream(const Request& request,Response& response,bool graph,bool tree){
        for(const auto& [name,value]:request.params)if(name!="after")throw std::invalid_argument("Unknown event stream query");
        auto after=cursor(request);
        if(request.get_header_value_count("Last-Event-ID")>1)throw std::invalid_argument("Duplicate stream cursor");
        if(request.has_header("Last-Event-ID")){
            const auto value=request.get_header_value("Last-Event-ID");std::int64_t resumed=0;
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),resumed);
            if(value.empty()||parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||resumed<0)throw std::invalid_argument("Invalid stream cursor");
            if(request.has_param("after")&&after!=resumed)throw std::invalid_argument("Conflicting stream cursors");
            after=resumed;
        }
        if(after>9007199254740991LL)throw std::invalid_argument("Stream cursor exceeds public integer limits");
        const auto id=identifier(request.matches[1]);const auto initial=persistence.run(id).get();
        if(graph){if(!initial.graph_root)throw std::invalid_argument("Expected a graph root");persistence.graph_run(id).get();}
        if((graph||tree)&&!initial.parent_id.empty())throw std::invalid_argument("Tree observation requires a root run");
        if(after){const auto boundary=(graph||tree)?persistence.tree_events(id,after-1,1).get():persistence.event_batch(id,after-1,1).get();if(boundary.empty()||boundary.front().sequence!=after)throw std::invalid_argument("Cursor does not belong to this event stream");}
        auto lease=std::make_shared<StreamLease>(active_streams);
        if(stopping||!lease->acquire()){reply(response,{{"detail","Event stream capacity is busy; reconnect later"}},503);return;}
        const auto supplied=request.get_header_value("Authorization"),view_origin=request.get_header_value("X-XMind-View-Origin");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);
        response.set_header("Cache-Control","no-store");response.set_header("X-Content-Type-Options","nosniff");response.set_header("X-Accel-Buffering","no");
        response.set_chunked_content_provider("text/event-stream",[this,id,graph,tree,after,lease,supplied,view_origin,deadline,first=true,written=std::size_t{0}](std::size_t,httplib::DataSink& sink)mutable{
            const auto finish=[&](const char* reason){const auto frame="event: end\ndata: "+Json{{"reason",reason},{"after",after}}.dump()+"\n\n";if(!sink.write(frame.data(),frame.size()))return false;sink.done();return true;};
            const auto heartbeat=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            while(!stopping&&sink.is_writable()){
                try{
#if defined(_WIN32)
                    if(supplied.starts_with("View ")&&!view_sessions->accepts(std::string_view(supplied).substr(5),view_origin))return finish("reauthenticate");
#endif
                    // Read state before the batch: terminal state and final events
                    // are committed together. Drain that batch before ending.
                    const auto run=persistence.run(id).get();
                    if(first){const auto frame="event: observation\ndata: "+Json{{"run",encode(run)},{"after",after},{"scope",graph?"graph":tree?"tree":"run"}}.dump()+"\n\n";if(!sink.write(frame.data(),frame.size()))return false;first=false;}
                    const auto events=(graph||tree)?persistence.tree_events(id,after,16).get():persistence.event_batch(id,after,16).get();
                    for(const auto& event:events){
                        if(event.sequence<=after||event.sequence>9007199254740991LL)throw std::runtime_error("Invalid persisted event cursor");
                        const auto frame="id: "+std::to_string(event.sequence)+"\nevent: committed\ndata: "+encode(event).dump()+"\n\n";
                        if(frame.size()>16*1024*1024)throw std::runtime_error("Persisted event exceeds stream limit");
                        if(written+frame.size()>32*1024*1024)return finish("reconnect");
                        if(!sink.write(frame.data(),frame.size()))return false;
                        written+=frame.size();after=event.sequence;
                    }
                    if(events.size()<16&&(run.state==RunState::completed||run.state==RunState::failed||run.state==RunState::cancelled))return finish("terminal");
                    if(std::chrono::steady_clock::now()>=deadline)return finish("reconnect");
                    if(!events.empty())return true;
                }catch(...){return finish("interrupted");}
                if(std::chrono::steady_clock::now()>=heartbeat){const std::string frame=": keepalive\n\n";return sink.write(frame.data(),frame.size());}
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return false;
        });
    }
    Impl(PersistenceService& store,std::string token,RunExecutor* execution,EditRecoveryReader* inspection,std::vector<McpServerMetadata> configured,std::vector<ProcessProfileMetadata> profiles,AgentInstructionMetadata policy,ProviderSetup* setup,GraphExecution* graphs,ProviderProfileSetup* profile_setup,BackendOwnerControl* owner,McpOAuthSetup* oauth):persistence(store),executor(execution),owner_control(owner),mcp_oauth(oauth),recovery(inspection),mcp_servers(std::move(configured)),process_profiles(std::move(profiles)),instructions(policy),authorization("Bearer "+token) {
        validate_local_auth_token(token);
#if defined(_WIN32)
        if(owner_control&&!owner_control->covers(store,execution,graphs))throw std::invalid_argument("Native owner controller does not cover this transport");
#endif
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
            static const std::regex view_route(R"(^/v1/(health|workspace(/skills)?|models|graphs|agent/(delegation|planning)|provider/(configuration|models|profiles(/(select|models))?)|sessions(/[A-Za-z0-9_-]+/(history|runs|title|skills|context(/(compact|requests/[A-Za-z0-9_-]+))?))?|runs(/[A-Za-z0-9_-]+(/(events|tree-events|children(/[A-Za-z0-9_-]+/history)?|cancel|operations|plan(/(human/[A-Za-z0-9_-]+|resume))?))?)?|graph-runs(/[A-Za-z0-9_-]+(/(children(/[A-Za-z0-9_-]+/history)?|events|human/[A-Za-z0-9_.-]+|resume))?)?|operations/[A-Za-z0-9_-]+(/(inspection|decision))?|view-sessions/(current|revoke))$)");
            static const std::regex owned_read_route(R"(^/v1/(workspace/skills|agent/(delegation|planning)|runs/[A-Za-z0-9_-]+/(tree-events|children(/[A-Za-z0-9_-]+/history)?|plan)|sessions/[A-Za-z0-9_-]+/context(/requests/[A-Za-z0-9_-]+)?)$)");
            static const std::regex plan_write_route(R"(^/v1/(runs/[A-Za-z0-9_-]+/plan/(human/[A-Za-z0-9_-]+|resume)|sessions/[A-Za-z0-9_-]+/context/compact|graph-runs/[A-Za-z0-9_-]+/resume)$)");
            static const std::regex stream_route(R"(^/v1/(runs/[A-Za-z0-9_-]+/(events|tree-events)|graph-runs/[A-Za-z0-9_-]+/events)/stream$)");
            const bool stream=std::regex_match(request.path,stream_route);
            static const std::regex mcp_auth_read(R"(^/v1/mcp/authorization/(servers|attempts/[A-Za-z0-9_-]+)$)");
            static const std::regex mcp_auth_write(R"(^/v1/mcp/authorization/(attempts(/[A-Za-z0-9_-]+/cancel)?|renewals)$)");
            const bool mcp_authorized=(request.method=="GET"&&std::regex_match(request.path,mcp_auth_read))||(request.method=="POST"&&std::regex_match(request.path,mcp_auth_write));
            if(!authenticated && request.get_header_value_count("Authorization")==1 && supplied.starts_with("View ") && request.get_header_value_count("X-XMind-View-Origin")==1 && (mcp_authorized||(stream&&request.method=="GET")||(!stream&&std::regex_match(request.path,view_route) && ((request.method=="GET"&&!std::regex_match(request.path,plan_write_route))||(request.method=="POST"&&!std::regex_match(request.path,owned_read_route)))))){
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
        if(owner_control){
            const auto encode_owner=[this](const BackendOwnerState& state){FILETIME birth{},exit{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user))throw std::runtime_error("Cannot observe native owner process");const auto created=(static_cast<std::uint64_t>(birth.dwHighDateTime)<<32)|birth.dwLowDateTime;Json value{{"legacy_ticket_id",state.legacy_ticket_id},{"generation",state.generation},{"revision",state.revision},{"quiesced",state.quiesced},{"receipt_id",state.receipt_id},{"retirement_requested",state.retirement_requested},{"replacement_prepared",state.replacement_prepared},{"retirement_supported",owner_control->replacement_supported()},{"process_id",GetCurrentProcessId()},{"process_birth",std::to_string(created)},{"bootstrap_receipt",nullptr}};if(state.replacement_source)value["bootstrap_receipt"]={{"generation",state.replacement_source->generation},{"revision",state.replacement_source->revision},{"receipt_id",state.replacement_source->receipt_id}};return value;};
            server.Get("/v1/backend/owner",guard_errors([this,encode_owner](const Request& request,Response& response){if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("Owner status does not accept query parameters");reply(response,encode_owner(owner_control->status()));}));
            server.Post("/v1/backend/owner/quiesce",guard_errors([this,encode_owner](const Request& request,Response& response){
                if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("Owner control does not accept query parameters");const auto input=body(request,{"expected_generation","expected_revision","expected_workspace_id","expected_workspace_authority_id"});const auto authority=workspace_admission(input);if(!authority)throw std::invalid_argument("Owner control requires workspace authority");reply(response,encode_owner(owner_control->quiesce({string_field(input,"expected_generation",32),graph_revision(input,"expected_revision")},*authority)));
            }));
            server.Post("/v1/backend/owner/resume",guard_errors([this,encode_owner](const Request& request,Response& response){
                if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("Owner control does not accept query parameters");const auto input=body(request,{"expected_generation","expected_revision","receipt_id","expected_workspace_id","expected_workspace_authority_id"});const auto authority=workspace_admission(input);if(!authority)throw std::invalid_argument("Owner control requires workspace authority");reply(response,encode_owner(owner_control->resume({string_field(input,"expected_generation",32),string_field(input,"receipt_id",32),graph_revision(input,"expected_revision")},*authority)));
            }));
            if(owner_control->replacement_supported()){
                server.Post("/v1/backend/owner/retire",guard_errors([this,encode_owner](const Request& request,Response& response){
                    if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("Owner control does not accept query parameters");const auto input=body(request,{"expected_generation","expected_revision","receipt_id","expected_workspace_id","expected_workspace_authority_id","target_runtime_root","target_manifest_sha256","approved_edits"});const auto authority=workspace_admission(input);if(!authority||!input.contains("approved_edits")||!input.at("approved_edits").is_boolean())throw std::invalid_argument("Retirement requires workspace authority and an edit policy");VerifiedRuntimeGeneration target(string_field(input,"target_runtime_root",32768),string_field(input,"target_manifest_sha256",64),executor->execution_workspace().root);auto state=owner_control->retire_to({string_field(input,"expected_generation",32),string_field(input,"receipt_id",32),graph_revision(input,"expected_revision")},*authority,target,input.at("approved_edits").get<bool>());reply(response,encode_owner(state));server.stop();
                }));
                server.Post("/v1/backend/owner/activate",guard_errors([this,encode_owner](const Request& request,Response& response){
                    if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("Owner control does not accept query parameters");const auto input=body(request,{"expected_generation","expected_revision","receipt_id","expected_workspace_id","expected_workspace_authority_id"});const auto authority=workspace_admission(input);if(!authority)throw std::invalid_argument("Activation requires workspace authority");reply(response,encode_owner(owner_control->activate_replacement({string_field(input,"expected_generation",32),string_field(input,"receipt_id",32),graph_revision(input,"expected_revision")},*authority)));
                }));
            }
        }
        server.Post("/v1/view-sessions",guarded([this](const Request& request,Response& response){
            const auto input=body(request,{"origin","process_id","process_birth"});std::uint32_t pid=0;std::string birth;
            if(input.contains("process_id")||input.contains("process_birth")){if(!input.contains("process_id")||!input.at("process_id").is_number_unsigned()||input.at("process_id")==0||input.at("process_id").get<std::uint64_t>()>0xffffffff)throw std::invalid_argument("Invalid view process identity");pid=input.at("process_id").get<std::uint32_t>();birth=string_field(input,"process_birth",20);}
            const auto session=view_sessions->issue(string_field(input,"origin",256),pid,birth);
            reply(response,{{"credential",session.credential},{"expires_unix_ms",session.expires_unix_ms},{"max_age_seconds",session.max_age_seconds}});
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
#if defined(_WIN32)
            std::optional<std::shared_lock<std::shared_mutex>> admission;
            try{if(owner_control)admission.emplace(owner_control->admit());}catch(const BackendQuiesced&){reply(response,{{"detail","Native backend admission is closed"}},503);return;}catch(...){reply(response,{{"detail","Native admission is unavailable"}},503);return;}
#endif
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
        server.Get("/v1/health",guarded([this,graphs](const Request&,Response& response) {
            const auto models=executor?executor->models():std::vector<std::string>{};
            reply(response,{{"status",executor && !executor->healthy()?"degraded":"ok"},{"api_version","v1"},{"core","C++"},{"storage","xlang3-sqlite"},{"session_rename",true},{ "skill_controls",executor&&executor->supports_session_skills()},{"file_edit_proposals",executor&&executor->supports_file_edit_proposals()},{"backend_owner_control",owner_control!=nullptr},{"agent_execution",owner_admission_open() && executor && executor->available()},{"model",models.empty()?"":models.front()},{"provider_profile_admission",executor&&executor->supports_profile_admission()},{"graph_provider_profile_admission",graphs&&graphs->supports_graph_profile_admission()},{"owned_child_observation",true},{"agent_delegation",executor&&executor->supports_delegation()},{"agent_planning",executor&&executor->supports_dynamic_planning()},{"context_controls",executor&&executor->supports_context()}});
        }));
        server.Get("/v1/workspace",guarded([this](const Request& request,Response& response){
            if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("Workspace metadata does not accept query parameters");
            const auto actual=executor?executor->execution_workspace():ExecutionWorkspaceMetadata{};
            reply(response,{{"configured",actual.configured},{"root",actual.configured?Json(actual.root):Json(nullptr)},
                {"workspace_id",actual.configured?Json(actual.workspace_id):Json(nullptr)},{"authority_id",actual.configured?Json(actual.authority_id):Json(nullptr)}});
        }));
        server.Get("/v1/workspace/skills",guarded([this](const Request& request,Response& response){
            if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("Skill catalogue does not accept query parameters");
            if(!executor||!executor->supports_skill_catalogue())throw RunUnavailable("Workspace skill inspection is unavailable");
            const auto actual=executor->workspace_skills();auto catalogue=Json::parse(actual.catalogue_json);
            catalogue["workspace_id"]=actual.workspace.workspace_id;catalogue["authority_id"]=actual.workspace.authority_id;
            reply(response,catalogue);
        }));
        server.Get(R"(/v1/sessions/([A-Za-z0-9_-]+)/skills)",guarded([this](const Request& request,Response& response){
            if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("Session skills do not accept query parameters");
            if(!executor||!executor->supports_session_skills())throw RunUnavailable("Session skill controls are unavailable");
            const auto session=request.matches[1].str();reply(response,session_skill_metadata(session,executor->session_skills(session)));
        }));
        server.Post(R"(/v1/sessions/([A-Za-z0-9_-]+)/skills)",guarded([this](const Request& request,Response& response){
            if(request.target.find('?')!=std::string::npos||!request.params.empty()||request.body.size()>8192)throw std::invalid_argument("Invalid session skill request target or size");
            if(!executor||!executor->supports_session_skills())throw RunUnavailable("Session skill controls are unavailable");
            const auto value=body(request,{"ids","expected_revision","expected_workspace_id","expected_workspace_authority_id"});
            const auto binding=workspace_admission(value);if(!binding||value.size()!=4||!value.contains("ids")||!value["ids"].is_array()||value["ids"].size()>8||!value.contains("expected_revision")||!value["expected_revision"].is_number_integer()||value["expected_revision"]<0||value["expected_revision"]>=9007199254740991LL)throw std::invalid_argument("Skill changes require exact ids, revision and workspace authority");
            std::vector<std::string> ids;for(const auto& id:value["ids"]){if(!id.is_string())throw std::invalid_argument("Skill ids must be strings");ids.push_back(id.get<std::string>());}
            const auto session=request.matches[1].str();reply(response,session_skill_metadata(session,executor->replace_session_skills(session,std::move(ids),value["expected_revision"].get<std::int64_t>(),*binding)));
        }));
        server.Get("/v1/agent/planning",guarded([this](const Request& request,Response& response){
            if(!request.params.empty())throw std::invalid_argument("Planning metadata does not accept query parameters");
            const bool enabled=executor&&executor->supports_dynamic_planning();
            reply(response,{{"enabled",enabled},{"tools",enabled?Json::array({"inspect_plan","plan_tasks","revise_plan"}):Json::array()}});
        }));
        server.Get("/v1/agent/delegation",guarded([this](const Request& request,Response& response){
            if(!request.params.empty())throw std::invalid_argument("Delegation metadata does not accept query parameters");
            const bool enabled=executor&&executor->supports_delegation();
            auto presets=Json::array();if(enabled)presets.push_back({{"id","workspace.inspect"},{"revision",1},{"readonly",true},{"tools",{"read_file","list_files","search_files","glob_files","read_repository_instructions","list_skills","load_skill"}}});
            reply(response,{{"enabled",enabled},{"presets",std::move(presets)},{"limits",enabled?Json{{"tasks_per_batch",4},{"parallel_children",2},{"total_children",8},{"depth",1},{"model_calls",32},{"leaf_turns",4},{"child_result_bytes",32768},{"tool_result_bytes",65536}}:Json(nullptr)}});
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
            const auto metadata=[](const ProviderSetupMetadata& value){return Json{{"revision",value.revision},{"provider",value.provider},{"model",value.model},{"endpoint",value.endpoint},{"configured",value.configured},{"wire",value.wire==ProviderWire::responses?"responses":value.wire==ProviderWire::anthropic_messages?"anthropic-messages":value.wire==ProviderWire::gemini_generate_content?"gemini-generate-content":"chat-completions"}};};
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
        if(profile_setup){
            const auto metadata=[profile_setup](const ProviderProfileRuntimeMetadata& value){
                auto entries=Json::array();for(const auto& profile:value.profiles)entries.push_back({{"id",profile.id},{"route_id",profile.route_id},{"provider",profile.provider},{"model",profile.model},{"revision",profile.revision}});
                auto routes=Json::array();for(const auto& route:profile_setup->profile_routes()){
                    const char* wire=nullptr;switch(route.wire){case ProviderWire::chat_completions:wire="chat-completions";break;case ProviderWire::responses:wire="responses";break;case ProviderWire::anthropic_messages:wire="anthropic-messages";break;case ProviderWire::gemini_generate_content:wire="gemini-generate-content";break;}
                    if(!wire)throw std::invalid_argument("Unknown backend provider wire");
                    routes.push_back({{"id",route.id},{"provider",route.provider},{"wire",wire},{"discovery",route.discovery}});
                }
                return Json{{"revision",value.revision},{"active",value.active},{"profiles",std::move(entries)},{"routes",std::move(routes)}};
            };
            const auto revision=[](const Json& value){if(!value.contains("expected_revision")||!value["expected_revision"].is_number_integer()||value["expected_revision"]<0||value["expected_revision"]>9007199254740991)throw std::invalid_argument("Invalid provider profile revision");return value["expected_revision"].get<std::int64_t>();};
            server.Get("/v1/provider/profiles",guarded([profile_setup,metadata](const Request& request,Response& response){
                if(!request.params.empty())throw std::invalid_argument("Provider profile metadata does not accept query parameters");
                reply(response,metadata(profile_setup->configuration()));
            }));
            server.Post("/v1/provider/configuration/import",guarded([profile_setup,metadata,revision](const Request& request,Response& response){
                if(!request.params.empty()||request.body.size()>65536)throw std::invalid_argument("Provider configuration import request exceeds limits");
                const auto value=body(request,{"path","expected_revision"});
                const auto path=string_field(value,"path",32768);
                if(path.empty())throw std::invalid_argument("Provider configuration path is unavailable");
                reply(response,metadata(profile_setup->import_yaml_configuration(std::filesystem::u8path(path),revision(value))));
            }));
            server.Post("/v1/provider/profiles",guarded([profile_setup,metadata,revision](const Request& request,Response& response){
                if(!request.params.empty()||request.body.size()>65536)throw std::invalid_argument("Provider profile setup request exceeds limits");
                const auto value=body(request,{"id","route_id","model","api_key","expected_revision","activate"});
                if(value.contains("activate")&&!value["activate"].is_boolean())throw std::invalid_argument("Invalid provider profile activation flag");
                const auto key=value.contains("api_key")?string_field(value,"api_key",32768):std::string{};SecretBytes secret({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()});
                reply(response,metadata(profile_setup->save_profile(string_field(value,"id",256),string_field(value,"route_id",256),string_field(value,"model",256),std::move(secret),revision(value),value.value("activate",false))));
            }));
            server.Post("/v1/provider/profiles/select",guarded([profile_setup,metadata,revision](const Request& request,Response& response){
                if(!request.params.empty()||request.body.size()>65536)throw std::invalid_argument("Provider profile selection request exceeds limits");
                const auto value=body(request,{"id","expected_revision"});reply(response,metadata(profile_setup->select_profile(string_field(value,"id",256),revision(value))));
            }));
            server.Post("/v1/provider/profiles/models",guarded([profile_setup,revision](const Request& request,Response& response){
                if(!request.params.empty()||request.body.size()>65536)throw std::invalid_argument("Provider profile discovery request exceeds limits");
                const auto value=body(request,{"id","route_id","api_key","expected_revision"});
                const auto key=value.contains("api_key")?string_field(value,"api_key",32768):std::string{};SecretBytes secret({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()});
                auto entries=Json::array();for(const auto& id:profile_setup->discover_models(string_field(value,"id",256),string_field(value,"route_id",256),std::move(secret),revision(value)))entries.push_back({{"id",id}});
                reply(response,{{"models",std::move(entries)}});
            }));
        }
        if(graphs){
            server.Get("/v1/graphs",guarded([graphs](const Request& request,Response& response){
                if(!request.params.empty())throw std::invalid_argument("Graph catalog does not accept query parameters");
                auto entries=Json::array();for(const auto& graph:graphs->graphs())entries.push_back({{"id",graph.id},{"revision",graph.revision},{"node_count",graph.node_count},{"executable",graph.executable}});
                reply(response,{{"graphs",entries}});
            }));
            server.Post("/v1/graph-runs",guarded([graphs](const Request& request,Response& response){
                if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("Graph admission does not accept query parameters");
                const auto value=body(request,{"id","session_id","graph_id","graph_revision","prompt","model_id","provider_profile_id","expected_provider_revision","expected_workspace_id","expected_workspace_authority_id"});
                const auto id=value.contains("id")?identifier(string_field(value,"id",128)):new_id();
                const auto model=value.contains("model_id")?string_field(value,"model_id",256):std::string{};
                const auto binding=profile_admission(value);const auto workspace=workspace_admission(value);
                const auto session=identifier(string_field(value,"session_id",128)),graph=string_field(value,"graph_id",64),prompt=string_field(value,"prompt",1024*1024);const auto revision=graph_revision(value,"graph_revision");
                reply(response,encode(workspace?graphs->submit_graph_workspace(id,session,graph,revision,prompt,model,*workspace,binding):binding?graphs->submit_graph_profile(id,session,graph,revision,prompt,model,*binding):graphs->submit_graph(id,session,graph,revision,prompt,model)),202);
            }));
            server.Get(R"(/v1/graph-runs/([A-Za-z0-9_-]+))",guarded([this,graphs](const Request& request,Response& response){
                if(!request.params.empty())throw std::invalid_argument("Graph detail does not accept query parameters");
                const auto id=identifier(request.matches[1]);
                for(int attempt=0;attempt<3;++attempt){
                    const auto root=persistence.graph_run(id).get();auto value=graph_record(root);
                    const auto context=graphs->graph_context(id);value["context"]={{"enabled",context.enabled},{"resumable",context.resumable},{"remaining_active_ms",context.remaining_active_ms?Json(*context.remaining_active_ms):Json(nullptr)}};
                    // Disabled context is immutable for this admitted graph;
                    // its original single repository snapshot is sufficient.
                    if(!context.enabled){reply(response,value);return;}
                    const auto fresh=persistence.graph_run(id).get();
                    if(fresh.checkpoint_revision==root.checkpoint_revision&&fresh.run.state==root.run.state){reply(response,value);return;}
                }
                throw Conflict("Graph context observation changed");
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
            server.Post(R"(/v1/graph-runs/([A-Za-z0-9_-]+)/resume)",guarded([graphs](const Request& request,Response& response){
                if(!request.params.empty())throw std::invalid_argument("Graph resume does not accept query parameters");
                const auto value=body(request,{"expected_checkpoint_revision"});
                reply(response,encode(graphs->resume_graph(identifier(request.matches[1]),"local-owner",graph_revision(value,"expected_checkpoint_revision"))),202);
            }));
        }
        server.Get("/v1/models",guarded([this](const Request&,Response& response) {
            const auto models=executor?executor->models():std::vector<std::string>{};
            auto entries=Json::array();for(const auto& model:models)entries.push_back({{"id",model}});
            reply(response,{{"default_model",models.empty()?"":models.front()},{"models",entries}});
        }));
        server.Get("/v1/mcp/servers",guarded([this](const Request& request,Response& response) {
            if(!request.params.empty())throw std::invalid_argument("MCP metadata does not accept query parameters");
            Json configured=Json::array();for(const auto& item:mcp_servers)configured.push_back({{"id",item.id},{"revision",item.revision},{"enabled",item.enabled},{"transport",item.transport}});
            reply(response,{{"servers",configured},{"runtime_state","per_run"}});
        }));
        if(mcp_oauth){
            const auto encode_authorization=[](const McpOAuthAttemptStatus& value){return Json{{"id",value.id},{"server_id",value.server_id},{"state",value.state},{"config_revision",value.config_revision},{"credential_revision",value.credential_revision},{"authorization_url",value.authorization_url.empty()?Json(nullptr):Json(value.authorization_url)},{"expires_unix_ms",value.expires_unix_ms},{"reason",value.reason.empty()?Json(nullptr):Json(value.reason)},{"cancellation_requested",value.cancellation_requested}};};
            server.Get("/v1/mcp/authorization/servers",guarded([this](const Request& request,Response& response){
                if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("MCP authorization status does not accept query parameters");
                Json values=Json::array();for(const auto& value:mcp_oauth->servers())values.push_back({{"id",value.id},{"config_revision",value.config_revision},{"credential_revision",value.credential_revision},{"enabled",value.enabled},{"configured",value.configured},{"state",value.state},{"expires_unix_ms",value.expires_unix_ms?Json(*value.expires_unix_ms):Json(nullptr)}});reply(response,{{"servers",values}});
            }));
            server.Post("/v1/mcp/authorization/attempts",guarded([this,encode_authorization](const Request& request,Response& response){
                if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("MCP authorization does not accept query parameters");
                const auto input=body(request,{"server_id","expected_config_revision","expected_credential_revision","request_id"});
                if(!input.contains("expected_credential_revision")||!input.at("expected_credential_revision").is_number_integer()||input.at("expected_credential_revision")<0||input.at("expected_credential_revision")>9007199254740991LL)throw std::invalid_argument("Invalid MCP credential revision");
                reply(response,encode_authorization(mcp_oauth->start(string_field(input,"server_id",64),graph_revision(input,"expected_config_revision"),input.at("expected_credential_revision").get<std::int64_t>(),identifier(string_field(input,"request_id",128)))),202);
            }));
            server.Post("/v1/mcp/authorization/renewals",guarded([this,encode_authorization](const Request& request,Response& response){
                if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("MCP renewal does not accept query parameters");
                const auto input=body(request,{"server_id","expected_config_revision","expected_credential_revision","request_id"});
                if(!input.contains("expected_credential_revision")||!input.at("expected_credential_revision").is_number_integer()||input.at("expected_credential_revision")<1||input.at("expected_credential_revision")>9007199254740991LL)throw std::invalid_argument("Invalid MCP renewal credential revision");
                reply(response,encode_authorization(mcp_oauth->renew(string_field(input,"server_id",64),graph_revision(input,"expected_config_revision"),input.at("expected_credential_revision").get<std::int64_t>(),identifier(string_field(input,"request_id",128)))),202);
            }));
            server.Get(R"(/v1/mcp/authorization/attempts/([A-Za-z0-9_-]+))",guarded([this,encode_authorization](const Request& request,Response& response){if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("MCP authorization status does not accept query parameters");reply(response,encode_authorization(mcp_oauth->status(identifier(request.matches[1]))));}));
            server.Post(R"(/v1/mcp/authorization/attempts/([A-Za-z0-9_-]+)/cancel)",guarded([this,encode_authorization](const Request& request,Response& response){if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("MCP authorization cancellation does not accept query parameters");body(request,{});reply(response,encode_authorization(mcp_oauth->cancel(identifier(request.matches[1]))),202);}));
        }
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
        server.Post(R"(/v1/sessions/([A-Za-z0-9_-]+)/title)",guarded([this](const Request& request,Response& response) {
            if(!request.params.empty())throw std::invalid_argument("Session rename does not accept query parameters");
            const auto value=body(request,{"title","expected_title"});
            if(!value.contains("expected_title")||!value["expected_title"].is_string())throw std::invalid_argument("Expected title is required");
            reply(response,encode(persistence.rename_session(identifier(request.matches[1]),string_field(value,"title"),value["expected_title"].get<std::string>()).get()));
        }));
        server.Get(R"(/v1/sessions/([A-Za-z0-9_-]+)/history)",guarded([this](const Request& request,Response& response) {
            reply(response,encode_all(persistence.history(identifier(request.matches[1])).get()));
        }));
        server.Get(R"(/v1/sessions/([A-Za-z0-9_-]+)/context)",guarded([this](const Request& request,Response& response){
            const auto model=strict_context_model_query(request);
            if(!executor)throw RunUnavailable("Configure a provider before inspecting context");
            reply(response,context_record(executor->context_status(identifier(request.matches[1]),model)));
        }));
        server.Get(R"(/v1/sessions/([A-Za-z0-9_-]+)/context/requests/([A-Za-z0-9_-]+))",guarded([this](const Request& request,Response& response){
            const auto model=strict_context_model_query(request);
            if(!executor)throw RunUnavailable("Configure a provider before inspecting context");
            const auto recorded=executor->context_request(identifier(request.matches[1]),identifier(request.matches[2]),model);
            reply(response,{{"id",recorded.id},{"state",recorded.state}});
        }));
        server.Post(R"(/v1/sessions/([A-Za-z0-9_-]+)/context/compact)",guarded([this](const Request& request,Response& response){
            if(request.target.find('?')!=std::string::npos||!request.params.empty()||request.body.size()>16384)throw std::invalid_argument("Context request exceeds limits");
            if(!executor||!executor->supports_context())throw RunUnavailable("This provider has no registered context controls");
            const auto value=body(request,{"id","model_id","expected_head_revision","provider_profile_id","expected_provider_revision"});
            if(!value.contains("expected_head_revision")||!value["expected_head_revision"].is_number_integer()||value["expected_head_revision"]<0||value["expected_head_revision"]>9007199254740991)throw std::invalid_argument("Invalid observed context head revision");
            const auto binding=profile_admission(value);if(executor->supports_profile_admission()&&!binding)throw Conflict("Capture the active provider profile before requesting compaction");
            if(binding&&!executor->supports_profile_admission())throw std::invalid_argument("Context provider profile admission is unavailable");
            const auto session=identifier(request.matches[1]);const auto id=value.contains("id")?identifier(string_field(value,"id",128)):new_id();
            const auto model=value.contains("model_id")?string_field(value,"model_id",256):std::string{};
            const auto head=value["expected_head_revision"].get<std::int64_t>();
            const auto admitted=binding?executor->request_context_profile(session,id,"local-owner",head,model,*binding):executor->request_context(session,id,"local-owner",head,model);
            reply(response,{{"id",admitted.id},{"state",admitted.state}},202);
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
        server.Get(R"(/v1/runs/([A-Za-z0-9_-]+)/plan)",guarded([this](const Request& request,Response& response){
            if(!request.params.empty())throw std::invalid_argument("Plan detail does not accept query parameters");
            const auto root=identifier(request.matches[1]);const auto run=persistence.run(root).get();
            if(!run.parent_id.empty()||run.graph_root)throw NotFound("Select an ordinary owning root");
            Json result={{"run",encode(run)},{"enabled",executor&&executor->supports_dynamic_planning()},{"plan",nullptr},{"questions",Json::array()},{"revisions",Json::array()},{"calls",Json::array()},{"policy",nullptr},{"budget",nullptr}};
            std::optional<RootBudgetRecord> budget;std::optional<DynamicPlanCapabilities> captured;
            try{captured=persistence.dynamic_capabilities(root).get();}catch(const NotFound&){}
            if(captured){result["policy"]=public_plan_policy(*captured);budget=persistence.root_budget(root).get();result["budget"]=public_plan_budget(*budget);}
            const auto plan=persistence.dynamic_plan_for_root(root).get();
            if(plan){
                result["plan"]=public_plan(*plan);
                for(const auto& h:persistence.dynamic_human_requests(plan->id).get()){
                    Json question={{"id",h.id},{"plan_id",h.plan_id},{"root_run_id",h.root_run_id},{"label",h.label},{"backend_node_id",h.backend_node_id},{"question",h.question},{"state",h.state},{"definition_revision",h.definition_revision},{"published_event_seq",h.published_event_seq},{"expires_unix_ms",h.expires_unix_ms},{"input_json",h.input_json},{"input_event_seq",h.input_event_seq?Json(*h.input_event_seq):Json(nullptr)}};
                    result["questions"].push_back(std::move(question));
                }
                for(const auto& revision:persistence.dynamic_plan_revisions(plan->id).get())result["revisions"].push_back({{"plan_id",revision.plan_id},{"revision",revision.revision},{"accepted_event_seq",revision.accepted_event_seq},{"call_id",revision.call_id},{"spec",Json::parse(revision.canonical_spec_json)}});
                for(const auto& call:persistence.dynamic_plan_calls(root).get()){
                    const bool pending=!call.conversation_commit_seq&&(call.state=="accepted"||call.state=="report_ready");
                    const auto actual=Json::parse(call.parent_assistant_json);
                    Json value={{"id",call.id},{"plan_id",call.plan_id},{"root_run_id",call.root_run_id},{"provider_tool_call_id",call.provider_tool_call_id},{"origin_attempt_id",call.origin_attempt_id},{"name",actual.at("tool_calls").at(0).at("name")},{"state",call.state},{"accepted_revision",call.accepted_revision},{"accepted_event_seq",call.accepted_event_seq},{"result_event_seq",call.result_event_seq?Json(*call.result_event_seq):Json(nullptr)},{"conversation_commit_seq",call.conversation_commit_seq?Json(*call.conversation_commit_seq):Json(nullptr)},{"continuation_attempt_id",call.continuation_attempt_id},{"pending",pending}};
                    if(pending)value["response"]=public_response(call.parent_assistant_json);
                    result["calls"].push_back(std::move(value));
                }
            }
            // Separate typed reads are never presented as one revision when an
            // owner concurrently changes topology, questions, budget or state.
            const auto fresh=persistence.dynamic_plan_for_root(root).get();
            if(plan.has_value()!=fresh.has_value()||(plan&&(plan->id!=fresh->id||plan->revision!=fresh->revision||plan->state_sequence!=fresh->state_sequence||plan->state!=fresh->state))||persistence.run(root).get().state!=run.state||(budget&&persistence.root_budget(root).get().revision!=budget->revision)||(captured&&persistence.dynamic_capabilities(root).get().catalogue_finalized!=captured->catalogue_finalized))throw Conflict("Plan changed during inspection; refresh recorded state");
            reply(response,result);
        }));
        server.Get(R"(/v1/runs/([A-Za-z0-9_-]+)/events)",guarded([this](const Request& request,Response& response) {reply(response,encode_all(persistence.events(identifier(request.matches[1]),cursor(request)).get()));}));
        server.Get(R"(/v1/runs/([A-Za-z0-9_-]+)/events/stream)",guarded([this](const Request& request,Response& response){event_stream(request,response,false,false);}));
        server.Get(R"(/v1/runs/([A-Za-z0-9_-]+)/tree-events/stream)",guarded([this](const Request& request,Response& response){event_stream(request,response,false,true);}));
        server.Get(R"(/v1/graph-runs/([A-Za-z0-9_-]+)/events/stream)",guarded([this](const Request& request,Response& response){event_stream(request,response,true,true);}));
        server.Get(R"(/v1/runs/([A-Za-z0-9_-]+)/children)",guarded([this](const Request& request,Response& response){
            if(!request.params.empty())throw std::invalid_argument("Owned children do not accept query parameters");
            reply(response,encode_all(persistence.owned_children(identifier(request.matches[1])).get()));
        }));
        server.Get(R"(/v1/runs/([A-Za-z0-9_-]+)/children/([A-Za-z0-9_-]+)/history)",guarded([this](const Request& request,Response& response){
            if(!request.params.empty())throw std::invalid_argument("Owned child history does not accept query parameters");
            reply(response,encode_all(persistence.owned_child_history(identifier(request.matches[1]),identifier(request.matches[2])).get()));
        }));
        server.Get(R"(/v1/runs/([A-Za-z0-9_-]+)/tree-events)",guarded([this](const Request& request,Response& response){
            if(request.params.size()>1||(!request.params.empty()&&!request.has_param("after")))throw std::invalid_argument("Invalid tree event cursor parameters");
            reply(response,encode_all(persistence.tree_events(identifier(request.matches[1]),cursor(request),256).get()));
        }));
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
            server.Post(R"(/v1/runs/([A-Za-z0-9_-]+)/plan/human/([A-Za-z0-9_-]+))",guarded([this](const Request& request,Response& response){
                if(!request.params.empty()||request.body.size()>131072)throw std::invalid_argument("Plan input request exceeds limits");
                const auto value=body(request,{"input_json","expected_revision","expected_state_sequence"});
                const auto input=string_field(value,"input_json",16384);plan_input_json(input);
                const auto revision=graph_revision(value,"expected_revision"),sequence=graph_revision(value,"expected_state_sequence");
                const auto root=identifier(request.matches[1]),question=identifier(request.matches[2]);const auto run=persistence.run(root).get();
                if(!run.parent_id.empty()||run.graph_root)throw NotFound("Select an ordinary owning root");
                const auto plan=persistence.dynamic_plan_for_root(root).get();if(!plan)throw NotFound("Root has no accepted plan");
                const auto actual=persistence.dynamic_human_request(plan->id,question).get();if(actual.root_run_id!=root)throw NotFound("Question does not belong to this root");
                reply(response,encode(executor->plan_input(root,question,input,"local-owner",revision,sequence)));
            }));
            server.Post(R"(/v1/runs/([A-Za-z0-9_-]+)/plan/resume)",guarded([this](const Request& request,Response& response){
                if(!request.params.empty()||request.body.size()>4096)throw std::invalid_argument("Plan resume request exceeds limits");
                const auto value=body(request,{"expected_revision","expected_state_sequence"});const auto revision=graph_revision(value,"expected_revision"),sequence=graph_revision(value,"expected_state_sequence");
                const auto root=identifier(request.matches[1]);const auto run=persistence.run(root).get();if(!run.parent_id.empty()||run.graph_root||!persistence.dynamic_plan_for_root(root).get())throw NotFound("Select an ordinary root with an accepted plan");
                reply(response,encode(executor->resume_plan(root,"local-owner",revision,sequence)),202);
            }));
            server.Post("/v1/runs",guarded([this](const Request& request,Response& response) {
                if(request.target.find('?')!=std::string::npos||!request.params.empty())throw std::invalid_argument("Run admission does not accept query parameters");
                const auto value=body(request,{"id","session_id","prompt","model_id","provider_profile_id","expected_provider_revision","expected_workspace_id","expected_workspace_authority_id"});
                const auto id=value.contains("id")?identifier(string_field(value,"id",128)):new_id();
                const auto model=value.contains("model_id")?string_field(value,"model_id",256):std::string{};
                const auto binding=profile_admission(value);const auto workspace=workspace_admission(value);const auto session=identifier(string_field(value,"session_id",128)),prompt=string_field(value,"prompt",1024*1024);
                reply(response,encode(workspace?executor->submit_workspace(id,session,prompt,model,*workspace,binding):binding?executor->submit_profile(id,session,prompt,model,*binding):executor->submit_model(id,session,prompt,model)),202);
            }));
            server.Post(R"(/v1/runs/([A-Za-z0-9_-]+)/cancel)",guarded([this](const Request& request,Response& response) {
                body(request,{});const auto id=identifier(request.matches[1]);executor->cancel(id);
                reply(response,{{"id",id},{"cancellation_requested",true}},202);
            }));
        }
    }
};
HttpServer::HttpServer(PersistenceService& store,std::string token,RunExecutor* executor,EditRecoveryReader* recovery,std::vector<McpServerMetadata> configured,std::vector<ProcessProfileMetadata> profiles,AgentInstructionMetadata policy,ProviderSetup* setup,GraphExecution* graphs,ProviderProfileSetup* profile_setup,BackendOwnerControl* owner,McpOAuthSetup* oauth):impl_(std::make_unique<Impl>(store,std::move(token),executor,recovery,std::move(configured),std::move(profiles),policy,setup,graphs,profile_setup,owner,oauth)) {}
HttpServer::~HttpServer(){stop();}
int HttpServer::bind(int port) {
    if(port<0 || port>65535 || impl_->port!=-1) throw std::invalid_argument("Invalid bind request");
    const auto bound=port==0?impl_->server.bind_to_any_port("127.0.0.1"):(impl_->server.bind_to_port("127.0.0.1",port)?port:-1);
    if(bound<0) throw std::runtime_error("Cannot bind loopback server");impl_->port=bound;return bound;
}
bool HttpServer::listen() {if(impl_->port<0) throw std::logic_error("Bind before listen");return impl_->server.listen_after_bind();}
void HttpServer::stop() {impl_->stopping=true;impl_->server.stop();}
}
