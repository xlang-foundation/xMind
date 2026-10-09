// Real native runtime, filesystem, HTTP authentication and embedded-xlang3
// SQLite. Provider replies and credentials are independent synthetic fixtures.
#include "agentflow/provider_profile_runtime.hpp"
#include "agentflow/http_server.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace agentflow;
using Json=nlohmann::json;
using namespace std::chrono_literals;
namespace {
const std::string fixture_key="synthetic-native-model-policy-key";
const std::string rejected_key="synthetic-rejected-model-policy-key";
const std::string owner="synthetic-native-model-policy-owner";
void require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
template<class E,class F>void rejects(F fn){try{fn();}catch(const E&){return;}throw std::runtime_error("Expected model policy rejection was missing");}
SecretBytes key(const std::string& value){return SecretBytes(std::vector<std::uint8_t>(value.begin(),value.end()));}
std::vector<ProviderProfileExecutionPolicy> policies(const std::string& origin){
    std::vector<ProviderProfileExecutionPolicy> result;
    for(const auto wire:{ProviderWire::chat_completions,ProviderWire::responses}){
        ProviderProfileRoute route{wire==ProviderWire::responses?"openai.responses":"openai.chat","openai",origin+(wire==ProviderWire::responses?"/responses":"/chat"),"fixture",wire==ProviderWire::responses?"fixture:responses":"fixture:chat",wire};
        ChatProviderConfig config;config.endpoint=route.endpoint;config.wire=wire;config.deadline=5s;config.idle_timeout=3s;
        ProviderProfileExecutionPolicy policy{route,config,ProviderCataloguePolicy{origin+"/models",ProviderCatalogueFormat::openai}};
        policy.model_capabilities=documented_openai_model_policy(wire);result.push_back(std::move(policy));
    }
    return result;
}
void unchanged(PersistenceService& store,ProviderProfileRuntime& runtime,std::int64_t revision,const std::string& registry,std::size_t credentials){
    require(runtime.configuration().revision==revision&&store.information("native-provider-profiles","registry").get()==registry&&store.credentials("fixture").get().size()==credentials,"Rejected policy changed registry or encrypted candidates");
}
struct Access{
    HttpServer server;int port;std::thread thread;
    Access(PersistenceService& store,ProviderProfileRuntime& runtime):server(store,owner,&runtime,nullptr,{},{},{},nullptr,&runtime,&runtime),port(server.bind(0)),thread([this]{server.listen();}){require(port>0,"Native listener did not bind");}
    ~Access(){server.stop();if(thread.joinable())thread.join();}
};
}
int main(int argc,char** argv){if(argc!=6)return 2;try{
    const auto root=std::filesystem::u8path(argv[1]),config=root/"providers.yaml";
    const std::vector<std::string> imports{argv[2],argv[3]};const std::string origin=argv[5];
    AgentSettings base;base.workspace=argv[4];base.max_output_tokens=64;base.run_timeout=10s;
    const auto allowed=policies(origin);const auto database=(root/"policy.sqlite").string();
    std::string registry;std::vector<Message> history;
    {
        PersistenceService store(database,imports);ProviderProfileRuntime runtime(store,base,allowed,1,8);Access access(store,runtime);
        httplib::Client client("127.0.0.1",access.port);client.set_read_timeout(5);
        const httplib::Headers auth{{"Authorization","Bearer "+owner}};
        auto post=[&](const std::string& path,const Json& body,int status){const auto r=client.Post(path,auth,body.dump(),"application/json");require(r&&r->status==status,"Unexpected native model policy HTTP status");return Json::parse(r->body);};
        const Json draft={{"id","openai"},{"route_id","openai.chat"},{"api_key",fixture_key},{"expected_revision",0}};
        const auto noauth=client.Post("/v1/provider/profiles/models",draft.dump(),"application/json");require(noauth&&noauth->status==401,"Catalogue requires actual native owner authentication");
        const auto chat=post("/v1/provider/profiles/models",draft,200);
        require(chat==Json{{"models",Json::array({{{"id","gpt-4.1"}},{{"id","gpt-6-sol"}}})}},"Chat workspace catalogue must retain known tools and exclude Responses-only, nontext and future models");
        auto response_draft=draft;response_draft["id"]="responses";response_draft["route_id"]="openai.responses";
        const auto responses=post("/v1/provider/profiles/models",response_draft,200);
        require(responses==Json{{"models",Json::array({{{"id","gpt-4.1"}},{{"id","gpt-6-astra"}},{{"id","gpt-6-sol"}},{{"id","gpt-6.1-sol"}}})}},"Responses catalogue must retain all documented current tools without granting future identities");
        require(runtime.configuration().revision==0&&store.credentials("fixture").get().empty(),"Discovery must not publish credentials or configuration");
        {std::ofstream out(config);out<<"version: 1\nprofiles:\n  openai:\n    route: openai.chat\n    api_key: "<<fixture_key<<"\n";}
        require(runtime.import_yaml_configuration(config,0).revision==1&&!runtime.available(),"Key-only YAML profile must remain unconfigured");
        const auto before=store.information("native-provider-profiles","registry").get();const auto keys=store.credentials("fixture").get().size();
        require(runtime.discover_models("openai","openai.chat",key(""),1)==std::vector<std::string>{"gpt-4.1","gpt-6-sol"},"Saved-key discovery must apply the same model policy");
        rejects<std::invalid_argument>([&]{runtime.discover_models("openai","openai.responses",key(""),1);});unchanged(store,runtime,1,before,keys);
        for(const auto* model:{"gpt-6.1-sol","gpt-6-astra","gpt-image-1","text-embedding-3-small","gpt-6.2-future"}){
            post("/v1/provider/profiles",{{"id","rejected"},{"route_id","openai.chat"},{"model",model},{"api_key",rejected_key},{"expected_revision",1},{"activate",false}},400);
            unchanged(store,runtime,1,before,keys);
        }
        {std::ofstream out(config);out<<"version: 1\nprofiles:\n  candidate:\n    route: openai.responses\n    api_key: "<<rejected_key<<"\n    model: gpt-6.1-sol\n  invalid:\n    route: openai.chat\n    api_key: "<<rejected_key<<"\n    model: gpt-image-1\n";}
        rejects<std::invalid_argument>([&]{runtime.import_yaml_configuration(config,1);});unchanged(store,runtime,1,before,keys);
        const auto ready=runtime.save_profile("openai","openai.chat","gpt-6-sol",key(""),1,true);
        require(ready.revision==2&&runtime.available(),"Known Chat model must prepare required tool reasoning using saved key");
        store.create_session("read","Synthetic native model policy read").get();
        post("/v1/runs",{{"id","rejected-model-run"},{"session_id","read"},{"prompt","Synthetic rejected request"},{"model_id","gpt-6.1-sol"}},400);
        require(store.runs("read").get().empty()&&store.history("read").get().empty(),"Wrong run model must reject before run/history admission");
        post("/v1/runs",{{"id","read-run"},{"session_id","read"},{"prompt","Read marker.txt using the actual native tool."},{"provider_profile_id","openai"},{"expected_provider_revision",2}},202);
        const auto until=std::chrono::steady_clock::now()+8s;
        for(;;){const auto run=store.run("read-run").get();if(run.state==RunState::completed)break;require(run.state==RunState::queued||run.state==RunState::running,"Native model policy read failed");require(std::chrono::steady_clock::now()<until,"Native model policy read timed out");std::this_thread::sleep_for(5ms);}
        history=store.history("read").get();require(history.size()==4&&history[2].role=="tool"&&Json::parse(history.back().json).at("content")=="Synthetic native model policy read completed.","Actual native tool read must be correlated and retained in history");
        require(history[2].json.find("model-policy-file-marker")!=std::string::npos,"Native tool must read the real owned workspace file");
        require(store.operations("read-run").get().empty(),"Read-only tool execution cannot dispatch effects");
        for(;;){try{runtime.save_profile("responses","openai.responses","gpt-6.1-sol",key(fixture_key),2,false);break;}catch(const Conflict&){require(std::chrono::steady_clock::now()<until,"Native worker did not retire");std::this_thread::sleep_for(5ms);}}
        require(runtime.configuration().revision==3&&runtime.configuration().active=="openai","Responses enrollment must preserve the selected Chat owner");
        registry=store.information("native-provider-profiles","registry").get();
        auto malformed=Json::parse(registry);for(auto& p:malformed["profiles"])if(p["id"]=="responses")p["model"]="gpt-image-1";
        store.put_information("native-provider-profiles","registry",malformed.dump()).get();
    }
    {
        PersistenceService store(database,imports);const auto original=store.information("native-provider-profiles","registry").get();const auto count=store.credentials("fixture").get().size();
        rejects<DatabaseError>([&]{ProviderProfileRuntime invalid(store,base,allowed,1,8);});
        require(store.information("native-provider-profiles","registry").get()==original&&store.credentials("fixture").get().size()==count,"Invalid stored inactive model must fail closed without changing keys or registry");
        store.put_information("native-provider-profiles","registry",registry).get();
        ProviderProfileRuntime reopened(store,base,allowed,1,8);require(reopened.available()&&reopened.configuration().revision==3,"Valid stored profiles must reopen without publication");
        const auto current=store.history("read").get();require(current.size()==history.size(),"Restart changed conversation size");for(std::size_t i=0;i<history.size();++i)require(current[i].json==history[i].json,"Restart changed original native tool/model history");
        require(reopened.discover_models("responses","openai.responses",key(""),3)==std::vector<std::string>{"gpt-4.1","gpt-6-astra","gpt-6-sol","gpt-6.1-sol"},"Reopened saved-key Responses discovery must retain exact route capabilities");
    }
    {
        PersistenceService store((root/"plain.sqlite").string(),imports);ProviderProfileRuntime plain(store,AgentSettings{},allowed,1,8);
        require(plain.save_profile("text","openai.chat","gpt-6.1-sol",key(fixture_key),0,true).revision==1&&plain.available(),"Tool-free generic Chat remains supported for Responses-only tool models");
        rejects<std::invalid_argument>([&]{plain.save_profile("image","openai.chat","gpt-image-1",key(rejected_key),1,false);});
        PersistenceService legacy((root/"legacy.sqlite").string(),imports);const auto legacy_key=legacy.put_credential("fixture","legacy-owned","fixture:chat","Synthetic legacy key",key(fixture_key),0).get();
        ProviderProfileRuntime migration(legacy,base,allowed,1,8);rejects<std::invalid_argument>([&]{migration.import_existing_profile("bad","openai.chat","gpt-6.1-sol",legacy_key.id,1);});
        require(migration.configuration().revision==0&&legacy.credentials("fixture").get().size()==1,"Trusted legacy import cannot publish an incompatible model");
        auto bad=allowed;bad[0].model_tools.emplace("gpt-6-sol",Capability::supported);rejects<std::invalid_argument>([&]{ProviderProfileRuntime conflict(store,AgentSettings{},bad,1,8);});
        bad=allowed;bad[0].model_capabilities->wire=ProviderWire::responses;rejects<std::invalid_argument>([&]{ProviderProfileRuntime wrong_wire(store,AgentSettings{},bad,1,8);});
        bad=allowed;bad[0].provider.reasoning_effort=ReasoningEffort::high;ProviderProfileRuntime explicit_effort(legacy,base,bad,1,8);
        rejects<std::invalid_argument>([&]{explicit_effort.save_profile("bad","openai.chat","gpt-6-sol",key(rejected_key),0,true);});
        require(explicit_effort.configuration().revision==0&&legacy.credentials("fixture").get().size()==1,"Incompatible explicit reasoning must reject before encryption");
    }
    std::cout<<"Native model policy passed authenticated route-filtered discovery, saved-key selection, actual native workspace read with required Chat none reasoning, rejection before credentials/admission, atomic YAML/legacy import, inactive stored-model validation and exact history reopen; provider replies are synthetic.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
