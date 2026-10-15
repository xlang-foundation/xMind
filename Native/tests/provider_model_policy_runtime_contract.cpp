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
    ProviderProfileRoute xai{"xai.responses","xai",origin+"/xai/responses","fixture","fixture:xai",ProviderWire::responses};
    ChatProviderConfig xai_config;xai_config.endpoint=xai.endpoint;xai_config.wire=xai.wire;xai_config.deadline=5s;xai_config.idle_timeout=3s;
    ProviderProfileExecutionPolicy xai_policy{xai,xai_config,ProviderCataloguePolicy{origin+"/xai/models",ProviderCatalogueFormat::openai}};
    xai_policy.model_capabilities=documented_xai_model_policy();result.push_back(std::move(xai_policy));
    ProviderProfileRoute mistral{"mistral.chat","mistral",origin+"/mistral/chat","fixture","fixture:mistral",ProviderWire::chat_completions};
    ChatProviderConfig mistral_config;mistral_config.endpoint=mistral.endpoint;mistral_config.wire=mistral.wire;mistral_config.chat_dialect=ChatDialect::mistral;mistral_config.deadline=5s;mistral_config.idle_timeout=3s;
    ProviderProfileExecutionPolicy mistral_policy{mistral,mistral_config,ProviderCataloguePolicy{origin+"/mistral/models",ProviderCatalogueFormat::openai}};
    mistral_policy.model_capabilities=documented_mistral_model_policy();result.push_back(std::move(mistral_policy));
    ProviderProfileRoute openrouter{"openrouter.chat","openrouter",origin+"/openrouter/chat","fixture","fixture:openrouter",ProviderWire::chat_completions};
    ChatProviderConfig openrouter_config;openrouter_config.endpoint=openrouter.endpoint;openrouter_config.wire=openrouter.wire;openrouter_config.tools=Capability::supported;openrouter_config.stream_usage=Capability::supported;openrouter_config.output_limit=Capability::supported;openrouter_config.deadline=5s;openrouter_config.idle_timeout=3s;
    result.push_back({openrouter,openrouter_config,ProviderCataloguePolicy{origin+"/openrouter/models",ProviderCatalogueFormat::openrouter}});
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
        auto post=[&](const std::string& path,const Json& body,int status){const auto r=client.Post(path,auth,body.dump(),"application/json");if(!r||r->status!=status)throw std::runtime_error("Unexpected native model policy HTTP status for "+path+": expected "+std::to_string(status)+", got "+(r?std::to_string(r->status):"no response"));return Json::parse(r->body);};
        const Json draft={{"id","openai"},{"route_id","openai.chat"},{"api_key",fixture_key},{"expected_revision",0}};
        const auto noauth=client.Post("/v1/provider/profiles/models",draft.dump(),"application/json");require(noauth&&noauth->status==401,"Catalogue requires actual native owner authentication");
        const auto chat=post("/v1/provider/profiles/models",draft,200);
        require(chat==Json{{"models",Json::array({{{"id","gpt-4.1"}},{{"id","gpt-6-sol"}}})}},"Chat workspace catalogue must retain known tools and exclude Responses-only, nontext and future models");
        auto response_draft=draft;response_draft["id"]="responses";response_draft["route_id"]="openai.responses";
        const auto responses=post("/v1/provider/profiles/models",response_draft,200);
        require(responses==Json{{"models",Json::array({{{"id","gpt-4.1"}},{{"id","gpt-6-astra"}},{{"id","gpt-6-sol"}},{{"id","gpt-6.1-sol"}}})}},"Responses catalogue must retain all documented current tools without granting future identities");
        const Json xai_draft={{"id","xai"},{"route_id","xai.responses"},{"api_key",fixture_key},{"expected_revision",0}};
        const auto xai_models=post("/v1/provider/profiles/models",xai_draft,200);
        require(xai_models==Json{{"models",Json::array({{{"id","grok-4.7"}}})}},"xAI discovery must expose only the exact natively declared tool-capable Grok frontier model");
        const Json mistral_draft={{"id","mistral"},{"route_id","mistral.chat"},{"api_key",fixture_key},{"expected_revision",0}};
        const auto mistral_models=post("/v1/provider/profiles/models",mistral_draft,200);
        require(mistral_models==Json{{"models",Json::array({{{"id","codestral-latest"}},{{"id","mistral-large-latest"}}})}},"Mistral discovery must filter account models through exact native tool-capability policy");
        const Json openrouter_draft={{"id","openrouter"},{"route_id","openrouter.chat"},{"api_key",fixture_key},{"expected_revision",0}};
        const auto openrouter_models=post("/v1/provider/profiles/models",openrouter_draft,200);
        require(openrouter_models==Json{{"models",Json::array({{{"id","google/gemini-3.8-flash"}},{{"id","openai/gpt-6-sol"}}})}},"OpenRouter discovery must retain only exact model identities declaring native tool support");
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
        require(runtime.save_profile("xai","xai.responses","grok-4.7",key(fixture_key),3,true).revision==4&&runtime.configuration().active=="xai","Reviewed xAI frontier model must activate on native Responses for execution");
        store.create_session("xai-read","Synthetic native xAI model policy read").get();
        post("/v1/runs",{{"id","xai-read-run"},{"session_id","xai-read"},{"prompt","Read marker.txt using Grok through the native Responses route."},{"provider_profile_id","xai"},{"expected_provider_revision",4}},202);
        const auto xai_until=std::chrono::steady_clock::now()+8s;
        for(;;){const auto run=store.run("xai-read-run").get();if(run.state==RunState::completed)break;require(run.state==RunState::queued||run.state==RunState::running,"Native xAI model policy read failed");require(std::chrono::steady_clock::now()<xai_until,"Native xAI model policy read timed out");std::this_thread::sleep_for(5ms);}
        const auto xai_history=store.history("xai-read").get();require(xai_history.size()==4&&xai_history[2].role=="tool"&&Json::parse(xai_history.back().json).at("content")=="Synthetic xAI native model policy read completed.","Actual native Responses tool loop must execute and retain the owned xAI profile read");require(xai_history[2].json.find("model-policy-file-marker")!=std::string::npos&&store.operations("xai-read-run").get().empty(),"Grok tool call must use the actual workspace file without inventing effects");
        require(runtime.save_profile("mistral","mistral.chat","mistral-large-latest",key(fixture_key),4,true).revision==5&&runtime.configuration().active=="mistral","Reviewed Mistral tool model must activate on its explicit native chat dialect");
        store.create_session("mistral-read","Synthetic native Mistral model policy read").get();
        post("/v1/runs",{{"id","mistral-read-run"},{"session_id","mistral-read"},{"prompt","Read marker.txt using Mistral through its native chat route."},{"provider_profile_id","mistral"},{"expected_provider_revision",5}},202);
        const auto mistral_until=std::chrono::steady_clock::now()+8s;
        for(;;){const auto run=store.run("mistral-read-run").get();if(run.state==RunState::completed)break;require(run.state==RunState::queued||run.state==RunState::running,"Native Mistral model policy read failed");require(std::chrono::steady_clock::now()<mistral_until,"Native Mistral model policy read timed out");std::this_thread::sleep_for(5ms);}
        const auto mistral_history=store.history("mistral-read").get();require(mistral_history.size()==4&&mistral_history[2].role=="tool"&&Json::parse(mistral_history.back().json).at("content")=="Synthetic native Mistral model policy read completed.","Actual native Mistral tool loop must retain the owned workspace read");require(mistral_history[2].json.find("model-policy-file-marker")!=std::string::npos&&store.operations("mistral-read-run").get().empty(),"Mistral tool request must call the real workspace file tool without dispatching effects");
        require(runtime.save_profile("openrouter","openrouter.chat","openai/gpt-6-sol",key(fixture_key),5,true).revision==6&&runtime.configuration().active=="openrouter","OpenRouter tool-capable model must activate on the native Chat Completions route");
        store.create_session("openrouter-read","Synthetic native OpenRouter model policy read").get();
        post("/v1/runs",{{"id","openrouter-read-run"},{"session_id","openrouter-read"},{"prompt","Read marker.txt using OpenRouter through the native Chat Completions route."},{"provider_profile_id","openrouter"},{"expected_provider_revision",6}},202);
        const auto openrouter_until=std::chrono::steady_clock::now()+8s;
        for(;;){const auto run=store.run("openrouter-read-run").get();if(run.state==RunState::completed)break;require(run.state==RunState::queued||run.state==RunState::running,"Native OpenRouter model policy read failed");require(std::chrono::steady_clock::now()<openrouter_until,"Native OpenRouter model policy read timed out");std::this_thread::sleep_for(5ms);}
        const auto openrouter_history=store.history("openrouter-read").get();require(openrouter_history.size()==4&&openrouter_history[2].role=="tool"&&Json::parse(openrouter_history.back().json).at("content")=="Synthetic native OpenRouter model policy read completed.","Actual native OpenRouter tool loop must retain the owned workspace read");require(openrouter_history[2].json.find("model-policy-file-marker")!=std::string::npos&&store.operations("openrouter-read-run").get().empty(),"OpenRouter tool request must call the actual workspace read without dispatching effects");
        registry=store.information("native-provider-profiles","registry").get();
        auto malformed=Json::parse(registry);for(auto& p:malformed["profiles"])if(p["id"]=="responses")p["model"]="gpt-image-1";
        store.put_information("native-provider-profiles","registry",malformed.dump()).get();
    }
    {
        PersistenceService store(database,imports);const auto original=store.information("native-provider-profiles","registry").get();const auto count=store.credentials("fixture").get().size();
        rejects<DatabaseError>([&]{ProviderProfileRuntime invalid(store,base,allowed,1,8);});
        require(store.information("native-provider-profiles","registry").get()==original&&store.credentials("fixture").get().size()==count,"Invalid stored inactive model must fail closed without changing keys or registry");
        store.put_information("native-provider-profiles","registry",registry).get();
        ProviderProfileRuntime reopened(store,base,allowed,1,8);require(reopened.available()&&reopened.configuration().revision==6,"Valid stored profiles must reopen without publication");
        const auto current=store.history("read").get();require(current.size()==history.size(),"Restart changed conversation size");for(std::size_t i=0;i<history.size();++i)require(current[i].json==history[i].json,"Restart changed original native tool/model history");
        require(reopened.discover_models("responses","openai.responses",key(""),6)==std::vector<std::string>{"gpt-4.1","gpt-6-astra","gpt-6-sol","gpt-6.1-sol"},"Reopened saved-key Responses discovery must retain exact route capabilities");
        require(reopened.discover_models("xai","xai.responses",key(""),6)==std::vector<std::string>{"grok-4.7"},"Reopened saved-key xAI discovery must retain the native Grok model policy");
        require(reopened.discover_models("mistral","mistral.chat",key(""),6)==std::vector<std::string>{"codestral-latest","mistral-large-latest"},"Reopened saved-key Mistral discovery must retain exact native tool models");
        require(reopened.discover_models("openrouter","openrouter.chat",key(""),6)==std::vector<std::string>{"google/gemini-3.8-flash","openai/gpt-6-sol"},"Reopened saved-key OpenRouter discovery must retain only tool-capable catalogue entries");
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
    std::cout<<"Native model policy passed authenticated route-filtered discovery, saved-key selection, actual native workspace reads through OpenAI Chat, OpenAI Responses, xAI Responses, Mistral and OpenRouter Chat, rejection before credentials/admission, atomic YAML/legacy import, inactive stored-model validation and exact history reopen; provider replies are synthetic.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
