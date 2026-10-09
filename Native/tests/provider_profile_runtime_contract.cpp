#include "agentflow/provider_profile_runtime.hpp"
#include "agentflow/provider_profile_legacy_setup.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/http_server.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <future>
#include <tuple>
using namespace agentflow;using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class F>void rejects(F call){try{call();}catch(const Error&){return;}throw std::runtime_error("Expected native profile runtime rejection");}
SecretBytes key(const std::string& value){return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
struct Access {
    HttpServer server;int port;std::thread thread;
    Access(PersistenceService& store,ProviderProfileRuntime& runtime,ProviderSetup* legacy=nullptr):server(store,"synthetic-profile-runtime-server-access-token",&runtime,nullptr,{},{},{},legacy,&runtime,&runtime),port(server.bind(0)),thread([this]{server.listen();}){}
    ~Access(){server.stop();if(thread.joinable())thread.join();}
};
std::vector<ProviderProfileExecutionPolicy> policy(const std::string& origin){
    std::vector<ProviderProfileExecutionPolicy> result;
    for(const auto& item:std::vector<ProviderProfileRoute>{{"openai.chat","openai",origin+"/chat","server","fixture:openai",ProviderWire::chat_completions},{"anthropic.messages","anthropic",origin+"/messages","server","fixture:anthropic",ProviderWire::anthropic_messages}}){
        ChatProviderConfig provider;provider.endpoint=item.endpoint;provider.wire=item.wire;
        provider.tools=Capability::supported;provider.stream_usage=Capability::supported;provider.output_limit=Capability::supported;
        provider.deadline=std::chrono::seconds(8);provider.idle_timeout=std::chrono::seconds(5);
        result.push_back({item,provider,ProviderCataloguePolicy{origin+(item.provider=="openai"?"/openai-models":"/claude-models"),item.provider=="openai"?ProviderCatalogueFormat::openai:ProviderCatalogueFormat::anthropic}});
    }return result;
}
Run wait(PersistenceService& store,const std::string& id,RunState expected){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    for(;;){const auto value=store.run(id).get();if(value.state==expected)return value;
        require(value.state==RunState::queued||value.state==RunState::running,"Native profile run ended unexpectedly");
        require(std::chrono::steady_clock::now()<deadline,"Native profile run did not settle");std::this_thread::sleep_for(std::chrono::milliseconds(5));}
}
ProviderProfileRuntimeMetadata select(ProviderProfileRuntime& runtime,const std::string& id,std::int64_t revision){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for(;;){try{return runtime.select_profile(id,revision);}catch(const Conflict&){require(std::chrono::steady_clock::now()<deadline,"Native profile workers did not retire");std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
}
void reply(PersistenceService& store,const std::string& session,const std::string& content){const auto history=store.history(session).get();require(history.size()==2,"Actual native run must commit its user and assistant messages");const auto value=Json::parse(history.back().json);require(value.at("content")==content&&value.contains("usage"),"Actual provider reply and usage must reach durable history");}
void provenance(PersistenceService& store,const std::string& session,const std::string& profile,std::int64_t revision,const std::string& model){
    const bool claude=profile=="claude";
    const auto expected=Json{{"profile_id",profile},{"profile_revision",revision},{"route_id",claude?"anthropic.messages":"openai.chat"},{"provider",claude?"anthropic":"openai"},{"wire",claude?"anthropic-messages":"chat-completions"},{"model_id",model}};
    const auto history=store.history(session).get();require(history.size()==2,"Provenance requires the actual persisted task and response");
    const auto runs=store.runs(session).get();require(runs.size()==1&&Json::parse(runs[0].provider_context_json)==expected,"Run descriptor must project its own saved admission context");
    for(const auto& message:history){const auto record=Json::parse(message.json);require(record.at("provider_context")==expected,"Durable provenance must retain the profile version and actual selected model");require(message.json.find("runtime-openai-fixture-key")==std::string::npos&&message.json.find("runtime-claude-fixture-key")==std::string::npos&&message.json.find("credential_id")==std::string::npos&&message.json.find("endpoint")==std::string::npos,"Public provenance cannot contain credentials or destinations");}
}
void workspace_binding_contract(const std::filesystem::path& owned,const std::vector<std::string>& imports,const std::string& origin){
    const auto left_root=owned/"workspace-left",right_root=owned/"workspace-right";
    std::filesystem::create_directory(left_root);std::filesystem::create_directory(right_root);
    for(const auto& [directory,text]:std::vector<std::pair<std::filesystem::path,std::string>>{{left_root,"Actual workspace LEFT file\n"},{right_root,"Actual workspace RIGHT file\n"}}){std::ofstream file(directory/"README.md",std::ios::binary);file<<text;require(static_cast<bool>(file),"Synthetic owned workspace must contain its real native file");}
    auto policies=policy(origin);for(auto& entry:policies)if(entry.route.wire==ProviderWire::anthropic_messages){entry.route.endpoint=origin+"/workspace-messages";entry.provider.endpoint=entry.route.endpoint;}
    const auto left_database=(owned/"workspace-left.sqlite").string(),right_database=(owned/"workspace-right.sqlite").string();Json previous_left;
    const httplib::Headers auth{{"Authorization","Bearer synthetic-profile-runtime-server-access-token"}};
    const auto metadata=[&](httplib::Client& client){const auto result=client.Get("/v1/workspace",auth);require(result&&result->status==200,"Configured owner must expose authenticated effective workspace metadata");const auto value=Json::parse(result->body);require(value.size()==4&&value.contains("configured")&&value.contains("root")&&value.contains("workspace_id")&&value.contains("authority_id")&&value["configured"]==true&&value["root"].is_string()&&value["workspace_id"].is_string()&&value["authority_id"].is_string(),"Workspace DTO must contain only the four actual public fields");const auto nonce=value["authority_id"].get<std::string>();require(nonce.size()==32&&nonce.find_first_not_of("0123456789abcdef")==std::string::npos&&result->body.find("backend_identity")==std::string::npos&&result->body.find("credential_id")==std::string::npos&&result->body.find("runtime-claude-fixture-key")==std::string::npos,"Workspace authority must be an opaque public nonce without settings or keys");return value;};
    const auto bind=[](Json request,const Json& workspace){request["expected_workspace_id"]=workspace["workspace_id"];request["expected_workspace_authority_id"]=workspace["authority_id"];return request;};
    const auto graph_catalog=R"({"graphs":[{"id":"read-local","spec":{"nodes":[{"id":"read","type":"tool","tool":"read_file","arguments":{"path":"README.md"}}]}}]})";
    {
        PersistenceService left(left_database,imports),right(right_database,imports);GraphCatalogStore(left).apply(graph_catalog);GraphCatalogStore(right).apply(graph_catalog);
        AgentSettings left_base,right_base;const auto startup_alias=owned/"workspace-startup-alias";left_base.workspace=startup_alias.string();right_base.workspace=right_root.string();
        ProviderProfileRuntime left_runtime(left,left_base,policies,1,8),right_runtime(right,right_base,policies,1,8);
        const auto captured=left_runtime.execution_workspace();require(captured.workspace_id!=right_runtime.execution_workspace().workspace_id,"Startup alias must initially capture the left physical root");
        std::cout<<"fixture-workspace-alias-captured\n"<<std::flush;std::string retargeted;
        require(static_cast<bool>(std::getline(std::cin,retargeted))&&retargeted=="fixture-workspace-alias-retargeted","Independent peer must retarget the actual owned junction");
        WorkspaceTools changed_alias(startup_alias.string());require(changed_alias.identity()==right_runtime.execution_workspace().workspace_id&&left_runtime.execution_workspace().workspace_id==captured.workspace_id,"Actual alias retarget must not rebind the runtime root handle");
        left_runtime.save_profile("claude","anthropic.messages","fixture-claude",key("runtime-claude-fixture-key"),0,true);right_runtime.save_profile("claude","anthropic.messages","fixture-claude",key("runtime-claude-fixture-key"),0,true);
        Access left_access(left,left_runtime),right_access(right,right_runtime);httplib::Client left_client("127.0.0.1",left_access.port),right_client("127.0.0.1",right_access.port);left_client.set_read_timeout(5);right_client.set_read_timeout(5);
        const auto anonymous=left_client.Get("/v1/workspace");require(anonymous&&anonymous->status==401,"Workspace path metadata requires actual owner authentication");const auto query=left_client.Get("/v1/workspace?extra=1",auth);require(query&&query->status==400,"Workspace metadata must reject arbitrary query inputs");
        const auto lm=metadata(left_client),rm=metadata(right_client);previous_left=lm;
        require(std::filesystem::u8path(lm["root"].get<std::string>()).lexically_normal()==std::filesystem::canonical(left_root).lexically_normal()&&std::filesystem::u8path(rm["root"].get<std::string>()).lexically_normal()==std::filesystem::canonical(right_root).lexically_normal(),"Effective roots must identify the actual opened directories rather than a requested path");
        require(lm["workspace_id"]!=rm["workspace_id"]&&lm["authority_id"]!=rm["authority_id"],"Independent physical roots must have distinct identities and authority generations");
        require(select(left_runtime,"claude",1).revision==2&&metadata(left_client)==lm,"Provider selection must not replace the immutable workspace generation");
        left.create_session("workspace-rejected","Rejected wrong-root admission").get();
        const auto run=Json{{"id","workspace-rejected-run"},{"session_id","workspace-rejected"},{"prompt","Wrong-root work must not start"},{"provider_profile_id","claude"},{"expected_provider_revision",2}};
        auto graph=run;graph["graph_id"]="read-local";graph["graph_revision"]=1;
        for(const auto& [path,request]:std::vector<std::pair<std::string,Json>>{{"/v1/runs",run},{"/v1/graph-runs",graph}}){
            for(const auto& expected:std::vector<Json>{rm,Json{{"workspace_id",lm["workspace_id"]},{"authority_id",rm["authority_id"]}}}){const auto rejected=left_client.Post(path,auth,bind(request,expected).dump(),"application/json");require(rejected&&rejected->status==409,"Wrong physical root or authority must reject before any native admission");}
            for(int mode=0;mode<4;++mode){auto malformed=bind(request,lm);if(mode==0)malformed.erase("expected_workspace_id");if(mode==1)malformed.erase("expected_workspace_authority_id");if(mode==2)malformed["expected_workspace_authority_id"]=17;if(mode==3)malformed["expected_workspace_authority_id"]="invalid";const auto rejected=left_client.Post(path,auth,malformed.dump(),"application/json");require(rejected&&rejected->status==400,"Half or malformed workspace bindings must reject before native work");}
            auto stale_profile=bind(request,lm);stale_profile["expected_provider_revision"]=1;const auto rejected=left_client.Post(path,auth,stale_profile.dump(),"application/json");require(rejected&&rejected->status==409,"Valid workspace binding cannot bypass the same atomic provider CAS");
        }
        require(left.runs("workspace-rejected").get().empty()&&left.history("workspace-rejected").get().empty(),"Rejected workspace/profile guards cannot create roots, child work or prompts");rejects<NotFound>([&]{left.run("workspace-rejected-run").get();});
        for(const auto& [name,store,runtime,client,workspace,expected_revision]:std::vector<std::tuple<std::string,PersistenceService*,ProviderProfileRuntime*,httplib::Client*,Json,std::int64_t>>{{"left",&left,&left_runtime,&left_client,lm,2},{"right",&right,&right_runtime,&right_client,rm,1}}){
            const auto session="workspace-agent-"+name,id="workspace-agent-run-"+name;store->create_session(session,"Real bound workspace read").get();const auto request=bind(Json{{"id",id},{"session_id",session},{"prompt","Workspace binding fixture:"+name},{"provider_profile_id","claude"},{"expected_provider_revision",expected_revision}},workspace);
            const auto admitted=client->Post("/v1/runs",auth,request.dump(),"application/json");require(admitted&&admitted->status==202,"Matching selected-root authority must admit the actual provider/tool loop");wait(*store,id,RunState::completed);
            const auto history=store->history(session).get();require(history.size()==4&&history[2].role=="tool"&&Json::parse(Json::parse(history[2].json).at("content").get<std::string>())==Json{{"path","README.md"},{"content",name=="left"?"Actual workspace LEFT file\n":"Actual workspace RIGHT file\n"}},"Each owner must read only the actual bytes from its own opened root");require(store->operations(id).get().empty(),"Workspace read binding cannot invent an approved effect");
            const auto graph_session="workspace-graph-"+name,graph_id="workspace-graph-run-"+name;store->create_session(graph_session,"Real bound native graph read").get();const auto graph_request=bind(Json{{"id",graph_id},{"session_id",graph_session},{"graph_id","read-local"},{"graph_revision",1},{"prompt","Read only the selected root"},{"provider_profile_id","claude"},{"expected_provider_revision",expected_revision}},workspace);
            const auto graph_admitted=client->Post("/v1/graph-runs",auth,graph_request.dump(),"application/json");require(graph_admitted&&graph_admitted->status==202,"Graph admission must use the same bound root and provider generation");wait(*store,graph_id,RunState::completed);require(Json::parse(store->graph_run(graph_id).get().checkpoint_json).at("nodes").at(0).at("output").at("content")== (name=="left"?"Actual workspace LEFT file\n":"Actual workspace RIGHT file\n"),"Bound graph must read its own root without another provider request");
        }
    }
    {
        PersistenceService left(left_database,imports);AgentSettings settings;settings.workspace=left_root.string();ProviderProfileRuntime runtime(left,settings,policies,1,8);Access access(left,runtime);httplib::Client client("127.0.0.1",access.port);client.set_read_timeout(5);const auto fresh=metadata(client);
        require(fresh["workspace_id"]==previous_left["workspace_id"]&&fresh["root"]==previous_left["root"]&&fresh["authority_id"]!=previous_left["authority_id"],"Restart retains physical root but mints a fresh public admission generation");
        left.create_session("workspace-stale","Stale workspace generation").get();const auto request=Json{{"id","workspace-stale-run"},{"session_id","workspace-stale"},{"graph_id","read-local"},{"graph_revision",1},{"prompt","Read after restart"},{"provider_profile_id","claude"},{"expected_provider_revision",2}};
        const auto stale=client.Post("/v1/graph-runs",auth,bind(request,previous_left).dump(),"application/json");require(stale&&stale->status==409&&left.runs("workspace-stale").get().empty()&&left.history("workspace-stale").get().empty(),"Stale generation must reject without restoring or changing owned data");const auto admitted=client.Post("/v1/graph-runs",auth,bind(request,fresh).dump(),"application/json");require(admitted&&admitted->status==202,"Refreshed same-root generation may admit new actual graph work");wait(left,"workspace-stale-run",RunState::completed);
        require(left.history("workspace-agent-left").get().size()==4&&left.credentials("server").get().size()==1&&left.operations("workspace-stale-run").get().empty(),"Generation refresh must preserve prior history/encrypted credentials and require no provider replay");
    }
}

}
int main(int argc,char** argv){if(argc!=5)return 2;try{
    const auto database=(std::filesystem::u8path(argv[1])/"profile-runtime.sqlite").string();const std::vector<std::string> imports{argv[2],argv[3]};const std::string origin=argv[4];
    AgentSettings base;base.max_output_tokens=64;
    {
        PersistenceService store(database,imports);GraphCatalogStore(store).apply(R"({"graphs":[{"id":"review","spec":{"nodes":[{"id":"review","type":"human","prompt":"Review fixture"}]}},{"id":"model-review","spec":{"nodes":[{"id":"agent","type":"agent","prompt":"Synthetic profile graph task"},{"id":"followup","type":"agent","prompt":"Synthetic followup task","depends_on":["agent"]}]}}]})");
        ProviderProfileRuntime runtime(store,base,policy(origin),1,8);require(!runtime.available()&&runtime.models().empty()&&runtime.configuration().revision==0,"Unconfigured profiles cannot inherit a startup model");
        Access access(store,runtime);httplib::Client client("127.0.0.1",access.port);client.set_read_timeout(5);const httplib::Headers authorized{{"Authorization","Bearer synthetic-profile-runtime-server-access-token"}};
        const auto anonymous=client.Get("/v1/provider/profiles");require(anonymous&&anonymous->status==401,"Profile metadata requires actual native backend authentication");
        const auto initial=client.Get("/v1/provider/profiles",authorized);require(initial&&initial->status==200&&Json::parse(initial->body).at("profiles").empty()&&Json::parse(initial->body).at("routes").size()==2,"Native HTTP API must publish fixed route identities and empty initial profiles");
        const auto request=Json{{"id","openai"},{"route_id","openai.chat"},{"model","fixture-openai"},{"api_key","runtime-openai-fixture-key"},{"expected_revision",0},{"activate",true}};
        const auto rejected_enrollment=client.Post("/v1/provider/profiles",request.dump(),"application/json");require(rejected_enrollment&&rejected_enrollment->status==401,"Profile mutation requires authentication before enrollment");
        auto spoofed=request;spoofed["endpoint"]="https://unapproved.invalid";const auto spoof=client.Post("/v1/provider/profiles",authorized,spoofed.dump(),"application/json");require(spoof&&spoof->status==400&&store.credentials("server").get().empty(),"Client cannot supply a provider destination or cause rejected-key persistence");
        rejects<RunUnavailable>([&]{runtime.submit("unconfigured","missing","fixture");});
        require(runtime.discover_models("openai","openai.chat",key("runtime-openai-fixture-key"),0)==std::vector<std::string>{"fixture-openai","fixture-openai-updated"},"New-key discovery must use the actual OpenAI catalogue");
        require(store.credentials("server").get().empty()&&runtime.configuration().revision==0,"Discovery cannot persist a key or enroll a profile");
        const auto enrolled=client.Post("/v1/provider/profiles",authorized,request.dump(),"application/json");require(enrolled&&enrolled->status==200,"Authenticated profile API must enroll the native execution service");
        require(enrolled->body.find("runtime-openai-fixture-key")==std::string::npos&&enrolled->body.find("credential_id")==std::string::npos&&enrolled->body.find("endpoint")==std::string::npos,"Profile API metadata must omit keys, credential references and destinations");
        auto configured=runtime.configuration();
        require(configured.revision==1&&runtime.available()&&runtime.models()==std::vector<std::string>{"fixture-openai"},"Committed first profile must activate the real execution service");
        configured=runtime.save_profile("claude","anthropic.messages","fixture-claude",key("runtime-claude-fixture-key"),1);
        require(configured.revision==2&&configured.active=="openai"&&configured.profiles.size()==2&&runtime.models()[0]=="fixture-openai","Inactive profile enrollment must preserve active execution");
        rejects<Conflict>([&]{runtime.select_profile("claude",1);});
        rejects<std::invalid_argument>([&]{runtime.save_profile("openai","anthropic.messages","fixture-claude",key(""),2,true);});
        const auto discovered=client.Post("/v1/provider/profiles/models",authorized,R"({"id":"openai","route_id":"openai.chat","expected_revision":2})","application/json");require(discovered&&discovered->status==200&&Json::parse(discovered->body)==Json{{"models",Json::array({{{"id","fixture-openai"}},{{"id","fixture-openai-updated"}}})}},"Authenticated saved-key profile discovery must return native account model identities");
        const auto stale=client.Post("/v1/provider/profiles/select",authorized,R"({"id":"claude","expected_revision":1})","application/json");require(stale&&stale->status==409,"Profile selection API must preserve revision conflicts");
        require(runtime.discover_models("claude","anthropic.messages",key(""),2)==std::vector<std::string>{"fixture-claude","fixture-claude/next"},"Claude discovery must follow bounded provider cursor pages");
        rejects<Conflict>([&]{runtime.discover_models("claude","anthropic.messages",key(""),1);});
        rejects<std::invalid_argument>([&]{runtime.discover_models("openai","anthropic.messages",key("runtime-claude-fixture-key"),2);});
        rejects<std::invalid_argument>([&]{runtime.discover_models("unknown","anthropic.messages",key(""),2);});
        for(const auto* failed:{"runtime-malformed-catalogue-key","runtime-loop-catalogue-key","runtime-reflected-catalogue-key","runtime-prefixed-reflection-key","runtime-redirect-catalogue-key","runtime-page-limit-key","runtime-entry-limit-key"})rejects<TransportError>([&]{runtime.discover_models("claude","anthropic.messages",key(failed),2);});
        std::stop_source cancelled;cancelled.request_stop();rejects<TransportCancelled>([&]{runtime.discover_models("claude","anthropic.messages",key(""),2,cancelled.get_token());});
        require(runtime.configuration().revision==2&&runtime.configuration().active=="openai"&&store.credentials("server").get().size()==2,"Saved-key or failed discovery cannot change registry, selection or credentials");
        store.create_session("openai-first","Profile OpenAI fixture").get();
        const auto admission=Json{{"id","openai-first-run"},{"session_id","openai-first"},{"prompt","OpenAI profile fixture"},{"model_id","fixture-openai"},{"provider_profile_id","openai"},{"expected_provider_revision",2}};
        auto outdated=admission;outdated["id"]="stale-profile-run";outdated["expected_provider_revision"]=1;
        const auto denied=client.Post("/v1/runs",authorized,outdated.dump(),"application/json");require(denied&&denied->status==409,"Stale profile revision must be rejected before native admission");
        outdated=admission;outdated["provider_profile_id"]="claude";const auto wrong_profile=client.Post("/v1/runs",authorized,outdated.dump(),"application/json");require(wrong_profile&&wrong_profile->status==409,"Matching model/revision cannot authorize another active profile");
        outdated=admission;outdated.erase("expected_provider_revision");const auto missing_binding=client.Post("/v1/runs",authorized,outdated.dump(),"application/json");require(missing_binding&&missing_binding->status==400,"Profile identity and revision must be supplied as a complete pair");
        outdated=admission;outdated["expected_provider_revision"]=2.5;const auto malformed_binding=client.Post("/v1/runs",authorized,outdated.dump(),"application/json");require(malformed_binding&&malformed_binding->status==400,"Profile admission must reject fractional revisions");
        require(store.runs("openai-first").get().empty()&&store.history("openai-first").get().empty(),"Rejected profile admission cannot create a run or prompt history");
        const auto admitted=client.Post("/v1/runs",authorized,admission.dump(),"application/json");require(admitted&&admitted->status==202,"Current bound profile must admit the real native model run");require(Json::parse(admitted->body).at("provider_context").at("profile_id")=="openai","Admission reply must carry its committed profile context");
        rejects<Conflict>([&]{runtime.select_profile("claude",2);});wait(store,"openai-first-run",RunState::completed);reply(store,"openai-first","Actual OpenAI fixture reply");provenance(store,"openai-first","openai",1,"fixture-openai");
        require(select(runtime,"claude",2).revision==3&&runtime.models()[0]=="fixture-claude","Selection must publish independently credentialed Claude execution");
        store.create_session("claude-first","Profile Claude fixture").get();runtime.submit("claude-first-run","claude-first","Claude profile fixture");wait(store,"claude-first-run",RunState::completed);reply(store,"claude-first","Actual Claude fixture reply");provenance(store,"claude-first","claude",1,"fixture-claude");
        const auto preserved=store.information("native-provider-profiles","registry").get();
        {XlangSqlite inject(database,imports);inject.execute("CREATE TRIGGER reject_profile_runtime BEFORE UPDATE ON information WHEN NEW.category='native-provider-profiles' BEGIN SELECT RAISE(ABORT,'profile runtime fixture failure'); END");}
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);bool rejected=false;
        while(!rejected){try{runtime.select_profile("openai",3);}catch(const Conflict&){require(std::chrono::steady_clock::now()<deadline,"Profile runtime did not become idle");std::this_thread::sleep_for(std::chrono::milliseconds(5));}catch(const DatabaseError&){rejected=true;}}
        require(runtime.configuration().revision==3&&runtime.models()[0]=="fixture-claude"&&store.information("native-provider-profiles","registry").get()==preserved,"Actual SQL failure must retain previous service, selection and profile keys");
        {XlangSqlite inject(database,imports);inject.execute("DROP TRIGGER reject_profile_runtime");}
        store.create_session("graph-session","Profile graph fixture").get();
        const auto graph_admission=Json{{"id","graph-run"},{"session_id","graph-session"},{"graph_id","review"},{"graph_revision",1},{"prompt","Review fixture"},{"provider_profile_id","claude"},{"expected_provider_revision",3}};
        auto stale_graph=graph_admission;stale_graph["expected_provider_revision"]=2;const auto denied_graph=client.Post("/v1/graph-runs",authorized,stale_graph.dump(),"application/json");require(denied_graph&&denied_graph->status==409&&store.runs("graph-session").get().empty(),"Graph profile conflicts must precede root persistence");
        const auto admitted_graph=client.Post("/v1/graph-runs",authorized,graph_admission.dump(),"application/json");require(admitted_graph&&admitted_graph->status==202,"Current bound graph must use the same native ownership lock");wait(store,"graph-run",RunState::paused);
        rejects<Conflict>([&]{runtime.select_profile("openai",3);});const auto graph=store.graph_run("graph-run").get();require(Json::parse(graph.input_json).at("provider_context").at("profile_id")=="claude"&&Json::parse(graph.input_json).at("provider_context").at("profile_revision")==1,"Graph root must retain its admission profile context independently of registry selection revision");runtime.human_input("graph-run","review",R"({"approved":true})","fixture-controller",graph.checkpoint_revision);wait(store,"graph-run",RunState::completed);
        require(select(runtime,"openai",3).revision==4,"Completed graph must release profile ownership");
        configured=runtime.save_profile("openai","openai.chat","fixture-openai-updated",key(""),4);
        require(configured.revision==5&&configured.active=="openai"&&runtime.models()[0]=="fixture-openai-updated","Updating the active profile must replace its service even without a selection flag");
        store.create_session("openai-updated","Updated profile fixture").get();runtime.submit("openai-updated-run","openai-updated","Updated OpenAI fixture");wait(store,"openai-updated-run",RunState::completed);reply(store,"openai-updated","Actual OpenAI fixture reply");provenance(store,"openai-updated","openai",2,"fixture-openai-updated");
    }
    {
        PersistenceService store(database,imports);ProviderProfileRuntime runtime(store,base,policy(origin),1,8);
        require(runtime.configuration().revision==5&&runtime.configuration().profiles.size()==2&&runtime.models()[0]=="fixture-openai-updated","Restart must restore profile policy, selection and independently encrypted keys");
        require(select(runtime,"claude",5).revision==6,"Restarted runtime must select the saved Claude key");store.create_session("claude-reopened","Reopened profile fixture").get();runtime.submit("claude-reopened-run","claude-reopened","Reopened Claude fixture");wait(store,"claude-reopened-run",RunState::completed);reply(store,"claude-reopened","Actual Claude fixture reply");provenance(store,"claude-reopened","claude",1,"fixture-claude");provenance(store,"openai-first","openai",1,"fixture-openai");provenance(store,"openai-updated","openai",2,"fixture-openai-updated");
        store.create_session("profile-agent-graph","Graph provenance fixture").get();runtime.submit_graph_profile("profile-agent-root","profile-agent-graph","model-review",1,"Synthetic graph provenance",{},ProviderProfileAdmission{"claude",6});wait(store,"profile-agent-root",RunState::completed);
        const auto children=store.children("profile-agent-root").get();require(children.size()==2,"Profile graph must execute two actual dependent agent children");
        const auto completed_graph=store.graph_run("profile-agent-root").get();require(completed_graph.checkpoint_json.find("provider_context")==std::string::npos,"Graph dependency outputs must exclude backend profile metadata");for(const auto& child:children)require(Json::parse(child.provider_context_json).at("profile_id")=="claude","Each graph child descriptor must retain its own profile");
        const auto child_history=store.run_history(children[0].id).get();require(child_history.size()==2,"Agent child must persist task and response separately");
        for(const auto& item:child_history){const auto context=Json::parse(item.json).at("provider_context");require(context.at("profile_id")=="claude"&&context.at("profile_revision")==1&&context.at("model_id")=="fixture-claude","Graph agent child must retain the actual profile and model");}
        const auto incoming=runtime.submit_message("profile-incoming",{},"profile-message","Synthetic incoming provenance",R"({"role":"fixture"})");wait(store,incoming.id,RunState::completed);provenance(store,incoming.session_id,"claude",1,"fixture-claude");
        require(runtime.submit_message("unused-replay-id",{},"profile-message","Synthetic incoming provenance",R"({"role":"fixture"})").id==incoming.id,"Incoming replay must retain its original run and profile receipt");
        auto changed=policy(origin);changed[1].route.endpoint="https://unapproved.invalid/messages";changed[1].provider.endpoint=changed[1].route.endpoint;
        rejects<DatabaseError>([&]{ProviderProfileRuntime invalid(store,base,changed,1,8);});
    }
    {
        PersistenceService store((std::filesystem::u8path(argv[1])/"profile-migration.sqlite").string(),imports);store.put_credential("server","legacy-owned-key","fixture:openai","Legacy fixture",key("runtime-openai-fixture-key"),0).get();
        ProviderProfileRuntime runtime(store,base,policy(origin),1,8);
        require(!runtime.import_legacy_configuration(),"Missing legacy record must leave an unconfigured runtime unchanged");
        const auto legacy=Json{{"provider","openai"},{"endpoint",origin+"/chat"},{"model","fixture-openai"},{"credential_id","legacy-owned-key"},{"revision",9},{"wire","chat-completions"}};
        auto invalid=legacy;invalid["endpoint"]="https://unapproved.invalid/chat";store.put_information("native-provider","active",invalid.dump()).get();
        rejects<DatabaseError>([&]{runtime.import_legacy_configuration();});require(runtime.configuration().revision==0&&store.credentials("server").get().size()==1,"Rejected legacy destination must preserve key and unconfigured runtime");
        invalid=legacy;invalid["wire"]="responses";store.put_information("native-provider","active",invalid.dump()).get();rejects<DatabaseError>([&]{runtime.import_legacy_configuration();});
        invalid=legacy;invalid["extra"]=true;store.put_information("native-provider","active",invalid.dump()).get();rejects<DatabaseError>([&]{runtime.import_legacy_configuration();});
        store.put_information("native-provider","active",std::string("{\"provider\":\"openai\",")+legacy.dump().substr(1)).get();rejects<DatabaseError>([&]{runtime.import_legacy_configuration();});
        require(runtime.configuration().revision==0&&store.credentials("server").get().size()==1,"Malformed legacy records cannot publish or rotate keys");
        store.put_information("native-provider","active",legacy.dump()).get();require(runtime.import_legacy_configuration(),"Native startup reader must import the validated legacy configuration");const auto migrated=runtime.configuration();
        require(migrated.revision==9&&runtime.available()&&store.credentials("server").get().size()==1,"Native profile migration must activate the original encrypted key without rotation");
        require(store.information("native-provider","active").get()==legacy.dump(),"Migration must preserve the original legacy source record");
        store.put_information("native-provider","active",R"({"invalid":"legacy fixture"})").get();require(!runtime.import_legacy_configuration(),"Existing registry must take precedence over stale legacy configuration");
        rejects<Conflict>([&]{runtime.import_existing_profile("other","openai.chat","fixture-openai","legacy-owned-key",9);});
    }
    for(const auto* legacy_model:{"runtime-openai-fixture-key","sk-invalid-legacy-model"}){
        PersistenceService store((std::filesystem::u8path(argv[1])/(std::string(legacy_model)+"-repair.sqlite")).string(),imports);
        store.put_credential("server","legacy-repair-key","fixture:openai","Legacy repair fixture",key("runtime-openai-fixture-key"),0).get();
        const auto legacy=Json{{"provider","openai"},{"endpoint",origin+"/chat"},{"model",legacy_model},{"credential_id","legacy-repair-key"},{"revision",9}}.dump();
        store.put_information("native-provider","active",legacy).get();
        {ProviderProfileRuntime runtime(store,base,policy(origin),1,8);require(runtime.import_legacy_configuration(),"Credential-shaped legacy model must migrate to a key-only profile");
            require(!runtime.available()&&runtime.models().empty()&&runtime.configuration().profiles[0].model.empty(),"Key-only profile cannot expose a secret as a model or execute inference");
            require(store.credentials("server").get().size()==1&&store.information("native-provider","active").get()==legacy,"Key-only migration must preserve original key and source");}
        ProviderProfileRuntime runtime(store,base,policy(origin),1,8);std::vector<ProviderProfileRoute> allowed;for(const auto& entry:policy(origin))allowed.push_back(entry.route);
        ProviderProfileLegacySetup compatibility(runtime,std::move(allowed),"openai","openai.chat","");Access access(store,runtime,&compatibility);
        require(compatibility.configuration().configured&&compatibility.configuration().model.empty()&&compatibility.configuration().revision==9,"Compatibility metadata must retain saved-key status and revision after restart");
        httplib::Client client("127.0.0.1",access.port);const httplib::Headers auth{{"Authorization","Bearer synthetic-profile-runtime-server-access-token"}};
        const auto discovered=client.Post("/v1/provider/models",auth,R"({"expected_revision":9})","application/json");require(discovered&&discovered->status==200,"Legacy saved-key discovery must repair without asking for the key again");
        const auto repaired=client.Post("/v1/provider/configuration",auth,R"({"model":"fixture-openai","expected_revision":9})","application/json");require(repaired&&repaired->status==200&&Json::parse(repaired->body).at("revision")==10&&runtime.available(),"Legacy setup must publish the same profile runtime after repair");
        require(store.information("native-provider","active").get()==legacy,"Repair must not rewrite historical legacy source");
    }
    {
        PersistenceService store((std::filesystem::u8path(argv[1])/"discovery-race.sqlite").string(),imports);ProviderProfileRuntime runtime(store,base,policy(origin),1,8);
        runtime.save_profile("openai","openai.chat","fixture-openai",key("runtime-openai-fixture-key"),0,true);
        auto discovery=std::async(std::launch::async,[&]{return runtime.discover_models("openai","openai.chat",key("runtime-delayed-catalogue-key"),1);});
        std::string signal;require(static_cast<bool>(std::getline(std::cin,signal))&&signal=="fixture-discovery-started","Independent peer must confirm real in-flight discovery");
        require(runtime.select_profile("openai",1).revision==2,"Discovery cannot hold the runtime admission lock during network I/O");
        std::cout<<"fixture-discovery-committed\n"<<std::flush;
        rejects<Conflict>([&]{discovery.get();});require(runtime.configuration().revision==2&&store.credentials("server").get().size()==1,"Stale discovery must not alter committed configuration or keys");
    }
    {
        PersistenceService store((std::filesystem::u8path(argv[1])/"run-context-validation.sqlite").string(),imports);store.create_session("context-validation","Synthetic context validation").get();
        const auto context=Json{{"profile_id","fixture"},{"profile_revision",1},{"route_id","openai.chat"},{"provider","openai"},{"wire","chat-completions"},{"model_id","fixture-model"}};
        auto extra=context;extra["api_key"]="synthetic-forbidden-field";rejects<DatabaseError>([&]{store.start_prompt_run("invalid-context","context-validation",Json{{"content","Synthetic rejected task"},{"provider_context",extra}}.dump()).get();});require(store.runs("context-validation").get().empty()&&store.history("context-validation").get().empty(),"Invalid context must reject before run and history persistence");
        store.append_user_message("context-validation",Json{{"content","Synthetic unrelated legacy message"},{"provider_context",context}}.dump()).get();store.create_run("legacy-run","context-validation").get();require(store.run("legacy-run").get().provider_context_json.empty(),"Legacy run cannot borrow unrelated conversation metadata");
    }
    for(const auto* name:{"startup-valid","startup-repair"}){
        PersistenceService store((std::filesystem::u8path(argv[1])/(std::string(name)+".sqlite")).string(),imports);
        ProviderRuntime legacy(store,base,1,8);legacy.configure("fixture-startup",key("runtime-openai-fixture-key"),0);
        if(std::string(name)=="startup-repair"){auto record=Json::parse(store.information("native-provider","active").get());record["model"]="sk-invalid-legacy-model";store.put_information("native-provider","active",record.dump()).get();}
    }
    {
        PersistenceService store((std::filesystem::u8path(argv[1])/"model-free-bound-graph.sqlite").string(),imports);
        GraphCatalogStore(store).apply(R"({"graphs":[{"id":"review","spec":{"nodes":[{"id":"review","type":"human","prompt":"Review fixture"}]}},{"id":"model-review","spec":{"nodes":[{"id":"agent","type":"agent","prompt":"Synthetic profile graph task"},{"id":"followup","type":"agent","prompt":"Synthetic followup task","depends_on":["agent"]}]}}]})");
        ProviderProfileRuntime runtime(store,base,policy(origin),1,8);Access access(store,runtime);httplib::Client client("127.0.0.1",access.port);const httplib::Headers auth{{"Authorization","Bearer synthetic-profile-runtime-server-access-token"}};
        const auto health=client.Get("/v1/health",auth);require(health&&health->status==200&&Json::parse(health->body).at("provider_profile_admission")==true&&Json::parse(health->body).at("graph_provider_profile_admission")==true,"Backend must advertise its actual profile admission support");
        store.create_session("bound-review","Model-free bound graph fixture").get();const auto accepted=client.Post("/v1/graph-runs",auth,R"({"id":"bound-review-run","session_id":"bound-review","graph_id":"review","graph_revision":1,"prompt":"Review","provider_profile_id":"","expected_provider_revision":0})","application/json");require(accepted&&accepted->status==202,"Empty active-profile binding must preserve real model-free graph execution");wait(store,"bound-review-run",RunState::paused);const auto graph=store.graph_run("bound-review-run").get();runtime.human_input("bound-review-run","review",R"({"approved":true})","fixture-controller",graph.checkpoint_revision);wait(store,"bound-review-run",RunState::completed);
    }
    // A saved Claude profile must prepare an explicit request budget even when
    // the backend base omits it. Existing 64-token cases above prove overrides.
    const auto default_database=(std::filesystem::u8path(argv[1])/"claude-default-output.sqlite").string();
    std::vector<Message> default_history;std::string default_registry;
    {
        PersistenceService store(default_database,imports);AgentSettings default_base;
        ProviderProfileRuntime runtime(store,default_base,policy(origin),1,8);
        const auto enrolled=runtime.save_profile("claude","anthropic.messages","fixture-claude",key("runtime-claude-fixture-key"),0,true);
        require(enrolled.revision==1&&enrolled.active=="claude"&&runtime.available(),"Default-output Claude must activate its real saved profile");
        store.create_session("claude-default","Synthetic default output-budget profile").get();
        Access access(store,runtime);httplib::Client client("127.0.0.1",access.port);client.set_read_timeout(5);
        const httplib::Headers authorized{{"Authorization","Bearer synthetic-profile-runtime-server-access-token"}};
        const auto admitted=client.Post("/v1/runs",authorized,R"({"id":"claude-default-run","session_id":"claude-default","prompt":"Claude profile default output fixture","provider_profile_id":"claude","expected_provider_revision":1})","application/json");
        require(admitted&&admitted->status==202,"Absent base output limit must admit a real authenticated Claude profile run");
        wait(store,"claude-default-run",RunState::completed);reply(store,"claude-default","Actual Claude fixture reply");provenance(store,"claude-default","claude",1,"fixture-claude");
        default_history=store.history("claude-default").get();default_registry=store.information("native-provider-profiles","registry").get();
        const auto response=Json::parse(default_history.back().json);
        require(response.at("usage").at("input_tokens")==3&&response.at("usage").at("output_tokens")==4&&response.at("provider_items").at(0).at("type")=="anthropic_content","Default Claude request must return actual native stream metrics and its original receipt");
        require(store.credentials("server").get().size()==1&&store.operations("claude-default-run").get().empty(),"Default output policy cannot rotate credentials or invent effects");
    }
    {
        PersistenceService store(default_database,imports);AgentSettings default_base;
        ProviderProfileRuntime runtime(store,default_base,policy(origin),1,8);
        require(runtime.configuration().revision==1&&runtime.configuration().active=="claude"&&store.credentials("server").get().size()==1&&store.information("native-provider-profiles","registry").get()==default_registry,"Default output policy must reopen the same saved registry/key without publication");
        const auto restored=store.history("claude-default").get();require(restored.size()==default_history.size(),"Default-output restart must preserve both conversation rows");
        for(std::size_t index=0;index<restored.size();++index)require(restored[index].sequence==default_history[index].sequence&&restored[index].role==default_history[index].role&&restored[index].json==default_history[index].json,"Default-output restart must preserve exact history bytes");
        store.create_session("claude-default-reopened","Synthetic reopened default output-budget profile").get();
        runtime.submit("claude-default-reopened-run","claude-default-reopened","Claude profile reopened default output fixture");
        wait(store,"claude-default-reopened-run",RunState::completed);reply(store,"claude-default-reopened","Actual Claude fixture reply");provenance(store,"claude-default-reopened","claude",1,"fixture-claude");
        require(store.operations("claude-default-reopened-run").get().empty()&&store.information("native-provider-profiles","registry").get()==default_registry,"Reopened default request cannot alter the saved provider or dispatch effects");
    }
    workspace_binding_contract(std::filesystem::u8path(argv[1]),imports,origin);
    std::cout<<"Native profile runtime passed actual OpenAI/Claude wire and key isolation, actual two-root workspace metadata/bound native reads/atomic CAS/restart rejection, explicit 64-token output override and saved-profile default4096 authenticated inference/reopen, SQL failure preservation, active/paused-graph ownership, active-profile update, restart and encrypted-reference migration; peers are synthetic, no live account or UI enrollment tested\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
