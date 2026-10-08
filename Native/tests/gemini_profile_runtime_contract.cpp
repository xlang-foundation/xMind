#include "agentflow/provider_profile_runtime.hpp"
#include "agentflow/http_server.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>

using namespace agentflow;using Json=nlohmann::json;
namespace {
constexpr const char* fixture_key="synthetic-gemini-profile-key-not-live";
constexpr const char* second_key="synthetic-gemini-profile-second-key-not-live";
constexpr const char* race_key="synthetic-gemini-profile-race-key-not-live";
constexpr const char* owner_token="synthetic-gemini-profile-owner-token";
constexpr const char* file_text="Actual enrolled native file bytes: \"quoted\" and Unicode 雪\n";
constexpr const char* final_answer="Synthetic Gemini peer checked the enrolled native file read.";
constexpr const char* reopened_answer="Synthetic Gemini peer accepted the reopened enrolled conversation.";
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class F>void rejects(F call){try{call();}catch(const Error&){return;}throw std::runtime_error("Expected Gemini profile runtime rejection");}
SecretBytes key(const std::string& value){return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
std::vector<ProviderProfileExecutionPolicy> policy(const std::string& origin){
    ProviderProfileRoute route{"gemini.generate-content","gemini",origin+"/v1beta","fixture","fixture:gemini",ProviderWire::gemini_generate_content};
    ChatProviderConfig provider;provider.endpoint=route.endpoint;provider.wire=route.wire;
    provider.tools=Capability::unknown;provider.stream_usage=Capability::supported;provider.output_limit=Capability::supported;
    provider.deadline=std::chrono::seconds(8);provider.idle_timeout=std::chrono::seconds(5);
    // Catalogue generation methods do not declare function-call support. These
    // two synthetic model overrides are backend-owned native execution policy.
    return {{route,provider,ProviderCataloguePolicy{origin+"/v1beta/models",ProviderCatalogueFormat::gemini},
        {{"models/fixture-gemini",Capability::supported},{"fixture-gemini-second",Capability::supported}}}};
}
struct Access {
    HttpServer server;int port;std::thread thread;
    Access(PersistenceService& store,ProviderProfileRuntime& runtime):server(store,owner_token,&runtime,nullptr,{},{},{},nullptr,&runtime,&runtime),port(server.bind(0)),thread([this]{server.listen();}){}
    ~Access(){server.stop();if(thread.joinable())thread.join();}
};
Run wait(PersistenceService& store,const std::string& id,RunState expected){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    for(;;){const auto run=store.run(id).get();if(run.state==expected)return run;
        require(run.state==RunState::queued||run.state==RunState::running,"Enrolled native Gemini run ended unexpectedly");
        require(std::chrono::steady_clock::now()<deadline,"Enrolled native Gemini run did not settle");std::this_thread::sleep_for(std::chrono::milliseconds(5));}
}
ProviderProfileRuntimeMetadata select_idle(ProviderProfileRuntime& runtime,const std::string& id,std::int64_t revision){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for(;;){try{return runtime.select_profile(id,revision);}catch(const Conflict&){require(std::chrono::steady_clock::now()<deadline,"Native Gemini workers did not retire");std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
}
void public_only(const std::string& source){
    for(const auto* forbidden:{fixture_key,second_key,race_key,owner_token,"credential_id","credential_scope","credential_purpose","endpoint","api_key"})
        require(source.find(forbidden)==std::string::npos,"Public Gemini profile metadata must exclude keys, credential references and destinations");
}
void same_history(const std::vector<Message>& before,const std::vector<Message>& after){
    require(before.size()==after.size(),"Enrolled SQLite reopen must preserve every conversation row");
    for(std::size_t index=0;index<before.size();++index)require(before[index].sequence==after[index].sequence&&before[index].role==after[index].role&&before[index].json==after[index].json,"Enrolled SQLite reopen must retain exact durable receipt/result bytes");
}
void provenance(const Json& value,const std::string& model="models/fixture-gemini"){
    require(value.at("provider_context")==Json{{"profile_id","gemini"},{"profile_revision",1},{"route_id","gemini.generate-content"},{"provider","gemini"},{"wire","gemini-generate-content"},{"model_id",model}},"Actual run must retain its admitted public profile version and resource identity");
}
const Json coding_models={{"models",Json::array({{{"id","models/fixture-gemini"}},{{"id","models/fixture-gemini-second"}}})}};
}
int main(int argc,char** argv){if(argc!=6)return 2;try{
    const auto root=std::filesystem::u8path(argv[1]);const auto database=(root/"profile.sqlite").string();
    const std::vector<std::string> imports{argv[2],argv[3]};const std::string origin=argv[5];
    AgentSettings base;base.workspace=argv[4];base.max_output_tokens=64;base.run_timeout=std::chrono::seconds(12);
    base.instructions="Synthetic Gemini enrollment contract: use only observed native file results.";
    const httplib::Headers auth{{"Authorization",std::string("Bearer ")+owner_token}};std::vector<Message> persisted;
    {
        PersistenceService store(database,imports);
        auto mismatch=policy(origin);mismatch[0].catalogue->format=ProviderCatalogueFormat::openai;
        rejects<std::invalid_argument>([&]{ProviderProfileRuntime invalid(store,base,mismatch,1,8);});
        mismatch=policy(origin);mismatch[0].route.wire=ProviderWire::responses;mismatch[0].provider.wire=ProviderWire::responses;
        rejects<std::invalid_argument>([&]{ProviderProfileRuntime invalid(store,base,mismatch,1,8);});
        auto invalid=policy(origin);invalid[0].route.endpoint+="?key=forbidden";invalid[0].provider.endpoint=invalid[0].route.endpoint;
        rejects<std::invalid_argument>([&]{ProviderProfileRuntime bad_base(store,base,invalid,1,8);});
        invalid=policy(origin);invalid[0].model_tools["models/fixture-gemini"]=static_cast<Capability>(99);
        rejects<std::invalid_argument>([&]{ProviderProfileRuntime bad_capability(store,base,invalid,1,8);});
        invalid=policy(origin);invalid[0].model_tools["fixture-gemini"]=Capability::supported;
        rejects<std::invalid_argument>([&]{ProviderProfileRuntime duplicate_alias(store,base,invalid,1,8);});
        invalid=policy(origin);invalid[0].model_tools.clear();for(int index=0;index<257;++index)invalid[0].model_tools["models/fixture-"+std::to_string(index)]=Capability::supported;
        rejects<std::invalid_argument>([&]{ProviderProfileRuntime excessive_policy(store,base,invalid,1,8);});
        ProviderProfileRuntime runtime(store,base,policy(origin),1,8);Access access(store,runtime);
        httplib::Client client("127.0.0.1",access.port);client.set_read_timeout(8);
        require(!runtime.available()&&runtime.models().empty()&&runtime.configuration().revision==0,"An empty Gemini registry must remain unconfigured");
        const auto anonymous=client.Get("/v1/provider/profiles");require(anonymous&&anonymous->status==401,"Profile metadata requires native server authentication");
        const auto metadata=client.Get("/v1/provider/profiles",auth);require(metadata&&metadata->status==200,"Authenticated Gemini profile metadata must be available");public_only(metadata->body);
        const auto initial=Json::parse(metadata->body);require(initial.at("profiles").empty()&&initial.at("routes")==Json::array({{{"id","gemini.generate-content"},{"provider","gemini"},{"wire","gemini-generate-content"},{"discovery",true}}}),"Native profile API must publish the actual Gemini route and independent discovery capability");
        const auto draft=Json{{"id","gemini"},{"route_id","gemini.generate-content"},{"api_key",fixture_key},{"expected_revision",0}};
        const auto anonymous_discovery=client.Post("/v1/provider/profiles/models",draft.dump(),"application/json");require(anonymous_discovery&&anonymous_discovery->status==401,"Discovery must authenticate before touching a provider key");
        const auto discovery=client.Post("/v1/provider/profiles/models",auth,draft.dump(),"application/json");require(discovery&&discovery->status==200&&Json::parse(discovery->body)==coding_models,"Workspace discovery must follow real Gemini pages while withholding unknown-tool and embedding models");public_only(discovery->body);
        require(store.credentials("fixture").get().empty()&&runtime.configuration().revision==0,"Draft discovery must not persist a key or profile");
        auto enrollment=draft;enrollment["model"]="models/fixture-gemini";enrollment["activate"]=true;
        auto reflected_id=enrollment;reflected_id["id"]=fixture_key;const auto reflected_profile=client.Post("/v1/provider/profiles",auth,reflected_id.dump(),"application/json");require(reflected_profile&&reflected_profile->status==400,"A draft credential cannot become the public Gemini profile ID");
        auto spoof=enrollment;spoof["endpoint"]="https://unapproved.invalid";const auto injected=client.Post("/v1/provider/profiles",auth,spoof.dump(),"application/json");require(injected&&injected->status==400,"Views cannot change the native Gemini destination");
        const std::vector<std::string> invalid_models{"","models/","models/.","models/..","models/fixture/escape","models/models/fixture","fixture:streamGenerateContent","models/"+std::string(129,'a'),"models/fixture-unknown-tools"};
        for(const auto& model:invalid_models){auto rejected=enrollment;rejected["model"]=model;const auto result=client.Post("/v1/provider/profiles",auth,rejected.dump(),"application/json");require(result&&result->status==400,"Invalid resources and unknown-tool coding models must fail before enrollment");}
        require(store.credentials("fixture").get().empty()&&runtime.configuration().revision==0,"Rejected Gemini models cannot leave encrypted key candidates or publish execution");
        const auto saved=client.Post("/v1/provider/profiles",auth,enrollment.dump(),"application/json");require(saved&&saved->status==200&&Json::parse(saved->body).at("revision")==1,"Authenticated enrollment must publish the native Gemini execution service");public_only(saved->body);
        require(runtime.available()&&runtime.models()==std::vector<std::string>{"models/fixture-gemini"}&&store.credentials("fixture").get().size()==1,"Committed Gemini profile must own exactly one encrypted key");
        auto saved_discovery=draft;saved_discovery.erase("api_key");saved_discovery["expected_revision"]=1;
        const auto using_saved=client.Post("/v1/provider/profiles/models",auth,saved_discovery.dump(),"application/json");require(using_saved&&using_saved->status==200&&Json::parse(using_saved->body)==coding_models,"Saved-key discovery must resolve the owned encrypted Gemini key");
        auto second=enrollment;second["id"]="second";second["model"]="models/fixture-gemini-second";second["api_key"]=second_key;second["expected_revision"]=1;second["activate"]=false;
        const auto inactive=client.Post("/v1/provider/profiles",auth,second.dump(),"application/json");require(inactive&&inactive->status==200&&runtime.configuration().revision==2&&runtime.configuration().active=="gemini","Inactive Gemini enrollment must preserve active execution");public_only(inactive->body);
        auto selection=Json{{"id","second"},{"expected_revision",2}};const auto selected=client.Post("/v1/provider/profiles/select",auth,selection.dump(),"application/json");require(selected&&selected->status==200&&runtime.models()==std::vector<std::string>{"models/fixture-gemini-second"},"Selection must resolve a canonical discovered model against the bare native policy alias");public_only(selected->body);
        selection={{"id","gemini"},{"expected_revision",3}};const auto restored=client.Post("/v1/provider/profiles/select",auth,selection.dump(),"application/json");require(restored&&restored->status==200&&runtime.configuration().revision==4,"Selection must restore the first encrypted Gemini profile");
        const auto stale_selection=client.Post("/v1/provider/profiles/select",auth,selection.dump(),"application/json");require(stale_selection&&stale_selection->status==409,"Stale selection must preserve registry revision CAS");
        enrollment["expected_revision"]=3;const auto stale_save=client.Post("/v1/provider/profiles",auth,enrollment.dump(),"application/json");require(stale_save&&stale_save->status==409,"Stale enrollment must fail before encrypted candidate persistence");
        saved_discovery["expected_revision"]=3;const auto stale_discovery=client.Post("/v1/provider/profiles/models",auth,saved_discovery.dump(),"application/json");require(stale_discovery&&stale_discovery->status==409,"Stale saved-key discovery must fail before network access");
        const auto registry=store.information("native-provider-profiles","registry").get();
        {XlangSqlite sql(database,imports);sql.execute("CREATE TRIGGER reject_gemini_profile BEFORE UPDATE ON information WHEN NEW.category='native-provider-profiles' BEGIN SELECT RAISE(ABORT,'synthetic Gemini profile CAS failure'); END");}
        rejects<DatabaseError>([&]{runtime.select_profile("second",4);});require(runtime.configuration().revision==4&&runtime.configuration().active=="gemini"&&store.information("native-provider-profiles","registry").get()==registry&&store.credentials("fixture").get().size()==2,"Actual xlang3 SQLite CAS failure must preserve the selected service, registry and owned keys");
        {XlangSqlite sql(database,imports);sql.execute("DROP TRIGGER reject_gemini_profile");}
        store.create_session("enrolled","Actual native Gemini tool execution after profile enrollment").get();
        auto admission=Json{{"id","enrolled-run"},{"session_id","enrolled"},{"prompt","Read the enrolled fixture file"},{"model_id","models/fixture-gemini"},{"provider_profile_id","gemini"},{"expected_provider_revision",3}};
        const auto denied=client.Post("/v1/runs",auth,admission.dump(),"application/json");require(denied&&denied->status==409&&store.runs("enrolled").get().empty()&&store.history("enrolled").get().empty(),"Stale profile admission cannot persist a run or prompt");
        admission["expected_provider_revision"]=4;const auto admitted=client.Post("/v1/runs",auth,admission.dump(),"application/json");require(admitted&&admitted->status==202,"The enrolled profile must admit real native AgentRunner execution");provenance(Json::parse(admitted->body));
        const auto completed=wait(store,"enrolled-run",RunState::completed);provenance(Json{{"provider_context",Json::parse(completed.provider_context_json)}});
        persisted=store.history("enrolled").get();require(persisted.size()==4&&persisted[0].role=="user"&&persisted[1].role=="assistant"&&persisted[2].role=="tool"&&persisted[3].role=="assistant","The enrolled runtime must persist its actual read-tool continuation");
        const auto turn=Json::parse(persisted[1].json),result=Json::parse(persisted[2].json),answer=Json::parse(persisted[3].json);const auto native_id=turn.at("tool_calls").at(0).at("id").get<std::string>();
        require(native_id.starts_with("gm_")&&native_id!="provider-profile-read"&&result.at("tool_call_id")==native_id&&Json::parse(result.at("content").get<std::string>())==Json{{"path","README.md"},{"content",file_text}},"Actual native file bytes must be correlated through native call identities");
        require(turn.at("provider_items").at(0).at("parts_json").is_string()&&answer.at("content")==final_answer&&answer.at("usage").at("prompt_tokens")==13&&answer.at("usage").at("completion_tokens")==4,"Native execution must retain signed receipts and real supplied provider reply metrics");
        std::size_t started=0,finished=0,done=0;for(const auto& event:store.events("enrolled-run").get()){public_only(event.json);if(event.kind=="tool.started")++started;if(event.kind=="tool.completed")++finished;if(event.kind=="model.done")++done;}
        require(started==1&&finished==1&&done==2,"Enrollment contract must observe one actual native read and two model turns");
        for(const auto& message:persisted){public_only(message.json);if(message.role!="tool")provenance(Json::parse(message.json));}
        store.create_session("cancelled","Synthetic held Gemini socket for actual native cancellation").get();admission["id"]="cancelled-run";admission["session_id"]="cancelled";admission["prompt"]="Cancel enrolled native run";
        const auto held=client.Post("/v1/runs",auth,admission.dump(),"application/json");require(held&&held->status==202,"Cancellation contract must admit an actual socket request");
        std::string signal;require(static_cast<bool>(std::getline(std::cin,signal))&&signal=="fixture-run-started","Independent peer must acknowledge the actual in-flight model request");
        rejects<Conflict>([&]{runtime.select_profile("second",4);});require(runtime.configuration().revision==4&&store.credentials("fixture").get().size()==2,"An in-flight agent must retain native profile execution ownership");
        const auto cancelled=client.Post("/v1/runs/cancelled-run/cancel",auth,"{}","application/json");require(cancelled&&cancelled->status==202,"Authenticated cancel must reach the native running agent");wait(store,"cancelled-run",RunState::cancelled);
        const auto cancelled_history=store.history("cancelled").get();require(cancelled_history.size()==1&&cancelled_history[0].role=="user"&&store.operations("cancelled-run").get().empty(),"Cancelled model transport cannot invent a completed assistant or authorize effects");
        require(select_idle(runtime,"gemini",4).revision==5,"Retired native agents must release profile ownership after cancellation");
    }
    {
        PersistenceService store(database,imports);ProviderProfileRuntime runtime(store,base,policy(origin),1,8);Access access(store,runtime);httplib::Client client("127.0.0.1",access.port);client.set_read_timeout(8);
        require(runtime.configuration().revision==5&&runtime.configuration().active=="gemini"&&runtime.configuration().profiles.size()==2&&runtime.models()==std::vector<std::string>{"models/fixture-gemini"}&&store.credentials("fixture").get().size()==2,"SQLite restart must restore selection, native capability policy and independent encrypted keys");same_history(persisted,store.history("enrolled").get());
        const auto metadata=client.Get("/v1/provider/profiles",auth);require(metadata&&metadata->status==200,"Reopened profile metadata must be served through native authentication");public_only(metadata->body);
        const auto discovered=client.Post("/v1/provider/profiles/models",auth,R"({"id":"gemini","route_id":"gemini.generate-content","expected_revision":5})","application/json");require(discovered&&discovered->status==200&&Json::parse(discovered->body)==coding_models&&store.credentials("fixture").get().size()==2,"Reopened discovery must reuse the original encrypted profile key");
        runtime.submit_profile("reopened-run","enrolled","Continue enrolled run after reopen",{},ProviderProfileAdmission{"gemini",5});wait(store,"reopened-run",RunState::completed);
        const auto after=store.history("enrolled").get();require(after.size()==6&&Json::parse(after.back().json).at("content")==reopened_answer,"Reopened profile must run its actual durable signed conversation");same_history(persisted,std::vector<Message>(after.begin(),after.begin()+persisted.size()));
        for(const auto& event:store.events("reopened-run").get()){public_only(event.json);require(event.kind!="tool.started"&&event.kind!="tool.completed","Profile reopen cannot re-execute prior file calls");}
    }
    {
        PersistenceService store((root/"key-only.sqlite").string(),imports);store.put_credential("fixture","imported-key","fixture:gemini","Synthetic imported Gemini key",key(fixture_key),0).get();
        {ProviderProfileRuntime runtime(store,base,policy(origin),1,8);
            rejects<std::invalid_argument>([&]{runtime.import_existing_profile(fixture_key,"gemini.generate-content","models/fixture-gemini","imported-key",9);});
            rejects<std::invalid_argument>([&]{runtime.import_existing_profile("gemini","gemini.generate-content","models/fixture/escape","imported-key",9);});
            rejects<std::invalid_argument>([&]{runtime.import_existing_profile("gemini","gemini.generate-content","models/fixture-unknown-tools","imported-key",9);});
            rejects<NotFound>([&]{store.information("native-provider-profiles","registry").get();});
            const auto imported=runtime.import_existing_profile("gemini","gemini.generate-content","","imported-key",9);require(imported.revision==9&&!runtime.available()&&runtime.models().empty()&&store.credentials("fixture").get().size()==1,"Trusted key-only import must retain its encrypted reference without executable inference");}
        {ProviderProfileRuntime runtime(store,base,policy(origin),1,8);require(!runtime.available()&&runtime.configuration().profiles[0].model.empty(),"Reopened key-only Gemini migration must remain unconfigured");
            require(runtime.discover_models("gemini","gemini.generate-content",key(""),9)==std::vector<std::string>{"models/fixture-gemini","models/fixture-gemini-second"},"Key-only migration must discover using the exact saved encrypted reference");
            rejects<std::invalid_argument>([&]{runtime.save_profile("gemini","gemini.generate-content","models/fixture-unknown-tools",key(""),9,true);});require(store.credentials("fixture").get().size()==1&&runtime.configuration().revision==9,"Unknown-tool migration repair cannot persist a candidate or publish execution");
            require(runtime.save_profile("gemini","gemini.generate-content","fixture-gemini",key(""),9,true).revision==10&&runtime.available(),"A validated bare Gemini model must resolve the canonical native tools policy during repair");}
        ProviderProfileRuntime reopened(store,base,policy(origin),1,8);require(reopened.models()==std::vector<std::string>{"fixture-gemini"},"Reopen must revalidate the repaired bare model through the same native binder and tools resolver");
    }
    {
        PersistenceService store((root/"text-only.sqlite").string(),imports);auto text=base;text.workspace.reset();
        {ProviderProfileRuntime runtime(store,text,policy(origin),1,8);
            for(const auto& model:{std::string(fixture_key),"models/"+std::string(fixture_key),"models/prefix-"+std::string(fixture_key)+"-suffix"})rejects<std::invalid_argument>([&]{runtime.save_profile("text","gemini.generate-content",model,key(fixture_key),0,true);});
            rejects<std::invalid_argument>([&]{runtime.save_profile(std::string("profile-")+fixture_key,"gemini.generate-content","models/fixture-unknown-tools",key(fixture_key),0,true);});
            require(store.credentials("fixture").get().empty()&&runtime.configuration().revision==0,"Resource-prefixed and substring-reflected draft credentials cannot enter profile persistence");
            require(runtime.discover_models("text","gemini.generate-content",key(fixture_key),0)==std::vector<std::string>{"models/fixture-gemini","models/fixture-gemini-second","models/fixture-unknown-tools"},"Text-only discovery must retain eligible models without inventing tool capability");
            runtime.save_profile("text","gemini.generate-content","models/fixture-unknown-tools",key(fixture_key),0,true);
            rejects<std::invalid_argument>([&]{runtime.save_profile("text","gemini.generate-content","models/"+std::string(fixture_key),key(""),1,true);});
            require(runtime.configuration().revision==1&&store.credentials("fixture").get().size()==1&&runtime.models()==std::vector<std::string>{"models/fixture-unknown-tools"},"Saved-key reflection rejection must preserve the committed profile and encrypted key without orphan candidates");
            store.create_session("text-only","Actual enrolled text-only native agent").get();runtime.submit("text-run","text-only","Text-only enrolled model");wait(store,"text-run",RunState::completed);
            const auto history=store.history("text-only").get();require(history.size()==2&&Json::parse(history.back().json).at("content")=="Synthetic text-only enrolled Gemini reply.","Unknown-tool text model must run through the real native adapter with no tool declarations");}
        rejects<DatabaseError>([&]{ProviderProfileRuntime incompatible_workspace(store,base,policy(origin),1,8);});
    }
    {
        PersistenceService store((root/"reflected-import.sqlite").string(),imports);auto text=base;text.workspace.reset();store.put_credential("fixture","imported-key","fixture:gemini","Synthetic reflected import",key(fixture_key),0).get();ProviderProfileRuntime runtime(store,text,policy(origin),1,8);
        rejects<std::invalid_argument>([&]{runtime.import_existing_profile("text","gemini.generate-content","models/"+std::string(fixture_key),"imported-key",9);});
        rejects<std::invalid_argument>([&]{runtime.import_existing_profile(fixture_key,"gemini.generate-content","models/fixture-unknown-tools","imported-key",9);});
        require(runtime.configuration().revision==0&&store.credentials("fixture").get().size()==1,"Rejected imported reflection must leave its existing encrypted key and empty registry unchanged");rejects<NotFound>([&]{store.information("native-provider-profiles","registry").get();});
    }
    {
        PersistenceService store((root/"invalid-saved-resource.sqlite").string(),imports);ProviderProfileRuntime runtime(store,base,policy(origin),1,8);
        runtime.save_profile("gemini","gemini.generate-content","models/fixture-gemini",key(fixture_key),0,true);runtime.save_profile("second","gemini.generate-content","models/fixture-gemini-second",key(second_key),1);
        auto corrupted=Json::parse(store.information("native-provider-profiles","registry").get());corrupted["profiles"][1]["model"]="models/fixture/escape";const auto source=corrupted.dump();store.put_information("native-provider-profiles","registry",source).get();
        rejects<std::invalid_argument>([&]{runtime.select_profile("second",2);});require(runtime.configuration().revision==2&&runtime.configuration().active=="gemini"&&store.information("native-provider-profiles","registry").get()==source&&store.credentials("fixture").get().size()==2,"Malformed persisted selection must fail before registry CAS or service publication");
        rejects<DatabaseError>([&]{ProviderProfileRuntime invalid_reopen(store,base,policy(origin),1,8);});
        corrupted["profiles"][1]["model"]="models/fixture-unknown-tools";store.put_information("native-provider-profiles","registry",corrupted.dump()).get();
        {ProviderProfileRuntime compatible_inactive(store,base,policy(origin),1,8);require(compatible_inactive.available()&&compatible_inactive.configuration().profiles[1].model=="models/fixture-unknown-tools"&&compatible_inactive.models()==std::vector<std::string>{"models/fixture-gemini"},"A valid unknown-tool inactive text profile must not prevent reopening the active coding profile");
            rejects<std::invalid_argument>([&]{compatible_inactive.select_profile("second",2);});require(compatible_inactive.configuration().revision==2&&compatible_inactive.configuration().active=="gemini","Selecting the incompatible inactive profile must still fail before publication");}
        corrupted["profiles"][1]["model"]="models/"+std::string(second_key);store.put_information("native-provider-profiles","registry",corrupted.dump()).get();rejects<DatabaseError>([&]{ProviderProfileRuntime reflected_inactive(store,base,policy(origin),1,8);});
    }
    {
        PersistenceService store((root/"discovery-race.sqlite").string(),imports);ProviderProfileRuntime runtime(store,base,policy(origin),1,8);runtime.save_profile("gemini","gemini.generate-content","models/fixture-gemini",key(fixture_key),0,true);
        auto discovery=std::async(std::launch::async,[&]{return runtime.discover_models("gemini","gemini.generate-content",key(race_key),1);});
        std::string signal;require(static_cast<bool>(std::getline(std::cin,signal))&&signal=="fixture-discovery-started","Independent peer must acknowledge the actual in-flight Gemini catalogue request");
        require(runtime.select_profile("gemini",1).revision==2,"Discovery network I/O cannot hold the execution admission lock");std::cout<<"fixture-discovery-committed\n"<<std::flush;
        rejects<Conflict>([&]{discovery.get();});require(runtime.configuration().revision==2&&store.credentials("fixture").get().size()==1,"Discovery completion after selection must reject its stale revision without changing keys or registry");
    }
    std::cout<<"Native Gemini profile runtime contract passed authenticated paginated draft/saved-key discovery, native model/capability validation before key persistence, selection/CAS and cancellation ownership, actual file-tool execution, encrypted xlang3 SQLite restart, key-only migration, text-only enrollment and stale discovery rejection. Provider socket replies and keys are synthetic; no live account inference or UI enrollment tested.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
