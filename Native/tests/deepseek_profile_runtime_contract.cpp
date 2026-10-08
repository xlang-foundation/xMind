// Real native profile/HTTP/AgentRunner and encrypted-credential xlang3 SQLite
// boundaries. The owned YAML, catalogue, inference, reasoning and keys are
// synthetic. This fixture never reads the user's configuration or calls a live model.
#include "agentflow/provider_profile_runtime.hpp"
#include "agentflow/deepseek_model_policy.hpp"
#include "agentflow/http_server.hpp"
#include "agentflow/xlang_sqlite.hpp"
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
const char* stage="entry";
const std::string fixture_key="synthetic-deepseek-profile-key-not-live";
const std::string rejected_key="synthetic-deepseek-rejected-profile-key-not-live";
const std::string owner_token="synthetic-deepseek-profile-owner-token";
const std::string reasoning="Synthetic enrolled DeepSeek reasoning, retained exactly.";
const std::string answer="Synthetic DeepSeek enrolled native inference completed.";
const std::string prompt="Inspect the enrolled native DeepSeek profile.";
const Json coding_models={{"models",Json::array({{{"id","deepseek-flash"}},{{"id","deepseek-v4-pro"}}})}};
const Json supplied_usage={{"prompt_tokens",17},{"completion_tokens",9},{"total_tokens",26},
    {"prompt_cache_hit_tokens",7},{"prompt_cache_miss_tokens",10},
    {"prompt_tokens_details",{{"cached_tokens",7}}},{"completion_tokens_details",{{"reasoning_tokens",4}}}};
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class E,class F>void rejects(F action){try{action();}catch(const E&){return;}throw std::runtime_error("Expected native DeepSeek profile rejection missing");}
template<class F>void eventually(F action,const char* message){const auto until=std::chrono::steady_clock::now()+10s;while(!action()){require(std::chrono::steady_clock::now()<until,message);std::this_thread::sleep_for(5ms);}}
std::vector<ProviderProfileExecutionPolicy> policy(const std::string& origin){
    ProviderProfileRoute route{"deepseek.chat","deepseek",origin+"/chat/completions","fixture","fixture:deepseek",ProviderWire::chat_completions};
    ChatProviderConfig provider;provider.endpoint=route.endpoint;provider.wire=route.wire;provider.chat_dialect=ChatDialect::deepseek;
    provider.tools=Capability::unknown;provider.stream_usage=Capability::supported;provider.output_limit=Capability::supported;provider.reasoning=Capability::supported;
    provider.deadline=8s;provider.idle_timeout=5s;
    return {{route,provider,ProviderCataloguePolicy{origin+"/models",ProviderCatalogueFormat::openai},deepseek_documented_tool_policy()}};
}
struct Access{
    HttpServer server;int port;std::thread thread;
    Access(PersistenceService& store,ProviderProfileRuntime& runtime):server(store,owner_token,&runtime,nullptr,{},{},{},nullptr,&runtime,&runtime),port(server.bind(0)){
        require(port>0,"Owned native profile HTTP listener did not bind");thread=std::thread([this]{server.listen();});
    }
    ~Access(){server.stop();if(thread.joinable())thread.join();}
};
struct Http{
    httplib::Client client;httplib::Headers auth{{"Authorization","Bearer "+owner_token}};
    explicit Http(int port):client("127.0.0.1",port){client.set_connection_timeout(2,0);client.set_read_timeout(8,0);client.set_follow_location(false);
        eventually([&]{const auto result=client.Get("/v1/health",auth);return result&&result->status==200;},"Owned native profile HTTP listener did not start");}
    Json get(const std::string& path,int status=200,const httplib::Headers* headers=nullptr){const auto result=client.Get(path,headers?*headers:auth);require(result&&result->status==status,"Unexpected native DeepSeek profile GET status");return Json::parse(result->body);}
    Json post(const std::string& path,const Json& body,int status=200,const httplib::Headers* headers=nullptr){const auto result=client.Post(path,headers?*headers:auth,body.dump(),"application/json");require(result&&result->status==status,"Unexpected native DeepSeek profile POST status");return Json::parse(result->body);}
};
void public_only(const Json& value){const auto bytes=value.dump();for(const auto* name:{"credential_id","credential_scope","credential_purpose","endpoint","api_key","provider_items","reasoning_content"})require(bytes.find(name)==std::string::npos,"Public profile metadata disclosed private configuration/receipt fields");
    for(const auto& secret:{fixture_key,rejected_key,owner_token,reasoning})require(bytes.find(secret)==std::string::npos,"Public profile metadata disclosed synthetic credential/receipt material");}
void unchanged(PersistenceService& store,ProviderProfileRuntime& runtime,std::int64_t revision,const std::string& registry,std::size_t credentials){
    require(runtime.configuration().revision==revision&&store.information("native-provider-profiles","registry").get()==registry&&store.credentials("fixture").get().size()==credentials,"Rejected native profile operation changed registry or encrypted credential candidates");
}
void exact_history(const std::vector<Message>& a,const std::vector<Message>& b){require(a.size()==b.size(),"Reopen changed original conversation size");for(std::size_t i=0;i<a.size();++i)require(a[i].sequence==b[i].sequence&&a[i].role==b[i].role&&a[i].json==b[i].json,"Reopen changed exact original model receipt/history bytes");}
Json provenance(){return {{"profile_id","deepseek"},{"profile_revision",2},{"route_id","deepseek.chat"},{"provider","deepseek"},{"wire","chat-completions"},{"model_id","deepseek-flash"}};}
void verify_ciphertext(const std::string& database,const std::vector<std::string>& imports){
    XlangSqlite sql(database,imports);const auto rows=sql.execute("SELECT ciphertext FROM credentials WHERE scope=?",{std::string("fixture")}).rows;
    require(rows.size()==2,"Key-only import plus known-model enrollment must retain their actual encrypted candidates");
    for(const auto& row:rows){require(row.size()==1&&std::holds_alternative<SqlBytes>(row[0]),"Actual provider credential must persist as a ciphertext BLOB");const auto& bytes=std::get<SqlBytes>(row[0]);require(!bytes.empty()&&std::search(bytes.begin(),bytes.end(),fixture_key.begin(),fixture_key.end())==bytes.end(),"Stored provider credential disclosed plaintext synthetic key bytes");}
}
}
int main(int argc,char** argv){if(argc!=6)return 2;try{
    const auto root=std::filesystem::u8path(argv[1]);const auto database=(root/"deepseek-profile.sqlite").string();const std::vector<std::string> imports{argv[2],argv[3]};const std::string origin=argv[5];
    AgentSettings base;base.workspace=argv[4];base.max_output_tokens=64;base.run_timeout=15s;base.instructions="Synthetic DeepSeek enrollment fixture: use native authority and actual supplied response evidence.";
    std::vector<Message> history;std::string retained_registry;
    {
        PersistenceService store(database,imports);stage="policy";const auto allowed=policy(origin);
        require(allowed[0].provider.chat_dialect==ChatDialect::deepseek&&allowed[0].provider.tools==Capability::unknown&&allowed[0].model_tools==std::map<std::string,Capability>{{"deepseek-flash",Capability::supported},{"deepseek-v4-pro",Capability::supported}},"Dated default policy must identify exact supported models without guessing future capabilities");
        auto foreign=allowed;foreign[0].route.wire=ProviderWire::responses;foreign[0].provider.wire=ProviderWire::responses;rejects<std::invalid_argument>([&]{ProviderProfileRuntime invalid(store,base,foreign,1,8);});
        auto invalid=allowed;invalid[0].provider.chat_dialect=static_cast<ChatDialect>(99);rejects<std::invalid_argument>([&]{ProviderProfileRuntime bad(store,base,invalid,1,8);});
        ProviderProfileRuntime runtime(store,base,allowed,1,8);Access access(store,runtime);Http http(access.port);httplib::Headers unauthenticated;
        require(!runtime.available()&&runtime.models().empty()&&runtime.configuration().revision==0&&store.credentials("fixture").get().empty(),"Unconfigured native DeepSeek runtime cannot invent a model or key");
        http.get("/v1/provider/profiles",401,&unauthenticated);const auto initial=http.get("/v1/provider/profiles");public_only(initial);
        require(initial==Json{{"revision",0},{"active",""},{"profiles",Json::array()},{"routes",Json::array({{{"id","deepseek.chat"},{"provider","deepseek"},{"wire","chat-completions"},{"discovery",true}}})}},"Public profile metadata must reflect the genuine registered native route only");
        stage="draft_discovery";const Json draft={{"id","deepseek"},{"route_id","deepseek.chat"},{"api_key",fixture_key},{"expected_revision",0}};
        http.post("/v1/provider/profiles/models",draft,401,&unauthenticated);const auto discovered=http.post("/v1/provider/profiles/models",draft);require(discovered==coding_models,"Workspace catalogue must filter unknown future tool capability and retain both actual known model IDs");public_only(discovered);
        require(runtime.configuration().revision==0&&runtime.configuration().profiles.empty()&&store.credentials("fixture").get().empty(),"Draft native catalogue discovery must not persist a key or activate a profile");
        const Json unsupported={{"id","unavailable"},{"route_id","deepseek.chat"},{"model","deepseek-future-unknown"},{"api_key",rejected_key},{"expected_revision",0},{"activate",true}};
        http.post("/v1/provider/profiles",unsupported,400);require(runtime.configuration().revision==0&&store.credentials("fixture").get().empty(),"Unknown workspace model enrollment must reject before encryption/registry publication");rejects<NotFound>([&]{store.information("native-provider-profiles","registry").get();});
        stage="yaml_key_only";const auto yaml=root/"synthetic-providers.yaml";{std::ofstream output(yaml,std::ios::binary);output<<"version: 1\nprofiles:\n  deepseek:\n    route: deepseek.chat\n    api_key: '"<<fixture_key<<"'\n";require(static_cast<bool>(output),"Owned synthetic YAML fixture write failed");}
        auto configured=runtime.import_yaml_configuration(yaml,0);require(configured.revision==1&&configured.active.empty()&&configured.profiles.size()==1&&configured.profiles[0].model.empty()&&!runtime.available()&&runtime.models().empty()&&store.credentials("fixture").get().size()==1,"Key-only native import must encrypt the key while leaving model/activation honestly unconfigured");
        const auto key_only_registry=store.information("native-provider-profiles","registry").get();configured=runtime.import_yaml_configuration(yaml,1);unchanged(store,runtime,1,key_only_registry,1);require(configured.revision==1&&configured.active.empty(),"Identical startup import cannot rotate credentials/revisions or select a guessed model");public_only(http.get("/v1/provider/profiles"));
        const Json saved_key={{"id","deepseek"},{"route_id","deepseek.chat"},{"expected_revision",1}};const auto saved_discovery=http.post("/v1/provider/profiles/models",saved_key);require(saved_discovery==coding_models,"Native catalogue must resolve the actual encrypted key-only profile without a replacement key");public_only(saved_discovery);
        auto rejected=unsupported;rejected["id"]="deepseek";rejected["expected_revision"]=1;http.post("/v1/provider/profiles",rejected,400);unchanged(store,runtime,1,key_only_registry,1);
        stage="selection";const Json enrollment={{"id","deepseek"},{"route_id","deepseek.chat"},{"model","deepseek-flash"},{"expected_revision",1},{"activate",false}};
        const auto enrolled=http.post("/v1/provider/profiles",enrollment);public_only(enrolled);require(enrolled["revision"]==2&&runtime.configuration().active.empty()&&!runtime.available()&&store.credentials("fixture").get().size()==2,"Known-model enrollment must remain inactive until an authenticated explicit selection");
        const auto inactive_registry=store.information("native-provider-profiles","registry").get();const Json selection={{"id","deepseek"},{"expected_revision",2}};
        http.post("/v1/provider/profiles/select",selection,401,&unauthenticated);auto stale=selection;stale["expected_revision"]=1;http.post("/v1/provider/profiles/select",stale,409);unchanged(store,runtime,2,inactive_registry,2);
        const auto selected=http.post("/v1/provider/profiles/select",selection);public_only(selected);require(selected["revision"]==3&&selected["active"]=="deepseek"&&runtime.available()&&runtime.models()==std::vector<std::string>{"deepseek-flash"},"Authenticated current profile selection must publish the actual native DeepSeek service");
        retained_registry=store.information("native-provider-profiles","registry").get();http.post("/v1/provider/profiles/select",selection,409);http.post("/v1/provider/profiles",enrollment,409);http.post("/v1/provider/profiles/models",saved_key,409);unchanged(store,runtime,3,retained_registry,2);
        public_only(http.get("/v1/models"));verify_ciphertext(database,imports);
        stage="inference";store.create_session("enrolled","Actual enrolled DeepSeek native inference").get();Json admission={{"id","enrolled-run"},{"session_id","enrolled"},{"prompt",prompt},{"model_id","deepseek-flash"},{"provider_profile_id","deepseek"},{"expected_provider_revision",2}};
        http.post("/v1/runs",admission,409);require(store.runs("enrolled").get().empty()&&store.history("enrolled").get().empty(),"Stale profile admission cannot persist a Run or prompt");
        admission["expected_provider_revision"]=3;const auto admitted=http.post("/v1/runs",admission,202);public_only(admitted);require(admitted["provider_context"]==provenance(),"Native admission must retain its actual profile/model version");
        eventually([&]{const auto run=store.run("enrolled-run").get();require(run.state==RunState::queued||run.state==RunState::running||run.state==RunState::completed,"Actual enrolled DeepSeek inference failed");return run.state==RunState::completed;},"Actual enrolled native inference did not complete");
        history=store.history("enrolled").get();require(history.size()==2&&history[0].role=="user"&&history[1].role=="assistant","One actual inference must append exactly its original user and actual assistant response");const auto actual=Json::parse(history.back().json);
        require(actual["content"]==answer&&actual["model"]=="deepseek-flash"&&actual["usage"]==supplied_usage&&actual["provider_context"]==provenance()&&actual["elapsed_ms"].is_number_integer()&&actual["first_token_ms"].is_number_integer(),"Actual enrolled response must retain supplied metrics/model/provenance and native timings");
        require(actual["provider_items"]==Json::array({{{"type","deepseek_assistant"},{"model","deepseek-flash"},{"message",{{"role","assistant"},{"content",answer},{"reasoning_content",reasoning}}}}}),"The model-tagged reasoning continuation must remain an exact private native receipt");
        int done=0,usage=0,reasons=0,tools=0;for(const auto& event:store.events("enrolled-run").get()){if(event.kind=="model.done")++done;if(event.kind=="model.usage"){++usage;require(Json::parse(event.json)==supplied_usage,"Actual streamed usage must match its saved receipt");}if(event.kind=="model.reasoning")++reasons;if(event.kind=="tool.started"||event.kind=="tool.completed")++tools;}
        require(done==1&&usage==1&&reasons==2&&tools==0&&store.operations("enrolled-run").get().empty()&&store.owned_children("enrolled-run").get().empty(),"One actual response cannot invent tools, effects or children");const auto budget=store.root_budget("enrolled-run").get();require(budget.model_calls_reserved==1&&budget.parent_model_calls_reserved==1&&budget.parent_calls_held==0&&budget.children_admitted==0,"Enrolled ordinary Agent must charge exactly one native physical parent call");
    }
    stage="reopen";{
        PersistenceService store(database,imports);ProviderProfileRuntime runtime(store,base,policy(origin),1,8);Access access(store,runtime);Http http(access.port);
        require(runtime.available()&&runtime.configuration().revision==3&&runtime.configuration().active=="deepseek"&&runtime.models()==std::vector<std::string>{"deepseek-flash"}&&store.credentials("fixture").get().size()==2,"SQLite reopen must restore actual native selected model and owned encrypted keys");exact_history(history,store.history("enrolled").get());unchanged(store,runtime,3,retained_registry,2);public_only(http.get("/v1/provider/profiles"));
        const auto discovered=http.post("/v1/provider/profiles/models",{{"id","deepseek"},{"route_id","deepseek.chat"},{"expected_revision",3}});require(discovered==coding_models,"Reopened discovery must use the selected saved key and exact native model policy");public_only(discovered);exact_history(history,store.history("enrolled").get());
        require(store.run("enrolled-run").get().state==RunState::completed&&store.root_budget("enrolled-run").get().model_calls_reserved==1,"Reopen/discovery cannot replay or remint the finished native inference");
    }
    std::cout<<"Native DeepSeek profile runtime passed synthetic catalogue/YAML/SSE, three actual catalogue sockets and one native inference, authenticated policy/selection/CAS, encrypted credentials and exact SQLite history reopen; no live provider or compaction acceptance.\n";return 0;
}catch(const std::exception& error){try{std::ofstream log(std::filesystem::u8path(argv[1])/"private-native-contract-error.log",std::ios::binary);log<<std::string(error.what()).substr(0,65536);}catch(...){}std::cerr<<"Native DeepSeek profile contract failed stage="<<stage<<"; owned private diagnostics retained.\n";return 1;}}
