#include "agentflow/provider_profile_runtime.hpp"
#include "agentflow/provider_profile_legacy_setup.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/http_server.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <thread>
#include <future>
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
}
int main(int argc,char** argv){if(argc!=5)return 2;try{
    const auto database=(std::filesystem::u8path(argv[1])/"profile-runtime.sqlite").string();const std::vector<std::string> imports{argv[2],argv[3]};const std::string origin=argv[4];
    AgentSettings base;base.max_output_tokens=64;
    {
        PersistenceService store(database,imports);GraphCatalogStore(store).apply(R"({"graphs":[{"id":"review","spec":{"nodes":[{"id":"review","type":"human","prompt":"Review fixture"}]}}]})");
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
        store.create_session("openai-first","Profile OpenAI fixture").get();runtime.submit("openai-first-run","openai-first","OpenAI profile fixture");
        rejects<Conflict>([&]{runtime.select_profile("claude",2);});wait(store,"openai-first-run",RunState::completed);reply(store,"openai-first","Actual OpenAI fixture reply");
        require(select(runtime,"claude",2).revision==3&&runtime.models()[0]=="fixture-claude","Selection must publish independently credentialed Claude execution");
        store.create_session("claude-first","Profile Claude fixture").get();runtime.submit("claude-first-run","claude-first","Claude profile fixture");wait(store,"claude-first-run",RunState::completed);reply(store,"claude-first","Actual Claude fixture reply");
        const auto preserved=store.information("native-provider-profiles","registry").get();
        {XlangSqlite inject(database,imports);inject.execute("CREATE TRIGGER reject_profile_runtime BEFORE UPDATE ON information WHEN NEW.category='native-provider-profiles' BEGIN SELECT RAISE(ABORT,'profile runtime fixture failure'); END");}
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);bool rejected=false;
        while(!rejected){try{runtime.select_profile("openai",3);}catch(const Conflict&){require(std::chrono::steady_clock::now()<deadline,"Profile runtime did not become idle");std::this_thread::sleep_for(std::chrono::milliseconds(5));}catch(const DatabaseError&){rejected=true;}}
        require(runtime.configuration().revision==3&&runtime.models()[0]=="fixture-claude"&&store.information("native-provider-profiles","registry").get()==preserved,"Actual SQL failure must retain previous service, selection and profile keys");
        {XlangSqlite inject(database,imports);inject.execute("DROP TRIGGER reject_profile_runtime");}
        store.create_session("graph-session","Profile graph fixture").get();runtime.submit_graph("graph-run","graph-session","review",1,"Review fixture");wait(store,"graph-run",RunState::paused);
        rejects<Conflict>([&]{runtime.select_profile("openai",3);});const auto graph=store.graph_run("graph-run").get();runtime.human_input("graph-run","review",R"({"approved":true})","fixture-controller",graph.checkpoint_revision);wait(store,"graph-run",RunState::completed);
        require(select(runtime,"openai",3).revision==4,"Completed graph must release profile ownership");
        configured=runtime.save_profile("openai","openai.chat","fixture-openai-updated",key(""),4);
        require(configured.revision==5&&configured.active=="openai"&&runtime.models()[0]=="fixture-openai-updated","Updating the active profile must replace its service even without a selection flag");
        store.create_session("openai-updated","Updated profile fixture").get();runtime.submit("openai-updated-run","openai-updated","Updated OpenAI fixture");wait(store,"openai-updated-run",RunState::completed);reply(store,"openai-updated","Actual OpenAI fixture reply");
    }
    {
        PersistenceService store(database,imports);ProviderProfileRuntime runtime(store,base,policy(origin),1,8);
        require(runtime.configuration().revision==5&&runtime.configuration().profiles.size()==2&&runtime.models()[0]=="fixture-openai-updated","Restart must restore profile policy, selection and independently encrypted keys");
        require(select(runtime,"claude",5).revision==6,"Restarted runtime must select the saved Claude key");store.create_session("claude-reopened","Reopened profile fixture").get();runtime.submit("claude-reopened-run","claude-reopened","Reopened Claude fixture");wait(store,"claude-reopened-run",RunState::completed);reply(store,"claude-reopened","Actual Claude fixture reply");
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
    for(const auto* name:{"startup-valid","startup-repair"}){
        PersistenceService store((std::filesystem::u8path(argv[1])/(std::string(name)+".sqlite")).string(),imports);
        ProviderRuntime legacy(store,base,1,8);legacy.configure("fixture-startup",key("runtime-openai-fixture-key"),0);
        if(std::string(name)=="startup-repair"){auto record=Json::parse(store.information("native-provider","active").get());record["model"]="sk-invalid-legacy-model";store.put_information("native-provider","active",record.dump()).get();}
    }
    std::cout<<"Native profile runtime passed actual OpenAI/Claude wire and key isolation, SQL failure preservation, active/paused-graph ownership, active-profile update, restart and encrypted-reference migration; peers are synthetic, no live account or UI enrollment tested\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
