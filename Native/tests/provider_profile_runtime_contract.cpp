#include "agentflow/provider_profile_runtime.hpp"
#include "agentflow/xlang_sqlite.hpp"
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
        rejects<RunUnavailable>([&]{runtime.submit("unconfigured","missing","fixture");});
        require(runtime.discover_models("openai","openai.chat",key("runtime-openai-fixture-key"),0)==std::vector<std::string>{"fixture-openai","fixture-openai-updated"},"New-key discovery must use the actual OpenAI catalogue");
        require(store.credentials("server").get().empty()&&runtime.configuration().revision==0,"Discovery cannot persist a key or enroll a profile");
        auto configured=runtime.save_profile("openai","openai.chat","fixture-openai",key("runtime-openai-fixture-key"),0,true);
        require(configured.revision==1&&runtime.available()&&runtime.models()==std::vector<std::string>{"fixture-openai"},"Committed first profile must activate the real execution service");
        configured=runtime.save_profile("claude","anthropic.messages","fixture-claude",key("runtime-claude-fixture-key"),1);
        require(configured.revision==2&&configured.active=="openai"&&configured.profiles.size()==2&&runtime.models()[0]=="fixture-openai","Inactive profile enrollment must preserve active execution");
        rejects<Conflict>([&]{runtime.select_profile("claude",1);});
        rejects<std::invalid_argument>([&]{runtime.save_profile("openai","anthropic.messages","fixture-claude",key(""),2,true);});
        require(runtime.discover_models("openai","openai.chat",key(""),2)==std::vector<std::string>{"fixture-openai","fixture-openai-updated"},"Saved-key OpenAI discovery must retain profile ownership");
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
        ProviderProfileRuntime runtime(store,base,policy(origin),1,8);const auto migrated=runtime.import_existing_profile("openai","openai.chat","fixture-openai","legacy-owned-key",9);
        require(migrated.revision==9&&runtime.available()&&store.credentials("server").get().size()==1,"Native profile migration must activate the original encrypted key without rotation");
        rejects<Conflict>([&]{runtime.import_existing_profile("other","openai.chat","fixture-openai","legacy-owned-key",9);});
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
    std::cout<<"Native profile runtime passed actual OpenAI/Claude wire and key isolation, SQL failure preservation, active/paused-graph ownership, active-profile update, restart and encrypted-reference migration; peers are synthetic, no live account or UI enrollment tested\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
