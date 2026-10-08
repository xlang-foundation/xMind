#include "agentflow/provider_profiles.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/execution_platform.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <filesystem>
#include <future>
#include <iostream>
#include <random>
#include <barrier>
using namespace agentflow;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class Error,class Function>void rejects(Function call){try{call();}catch(const Error&){return;}throw std::runtime_error("Expected profile rejection did not occur");}
struct Directory {std::filesystem::path path;Directory(){std::random_device random;for(int i=0;i<32;++i){auto value=std::filesystem::temp_directory_path()/("xmind-profiles-"+std::to_string(random())+"-"+std::to_string(random()));if(std::filesystem::create_directory(value)){path=std::move(value);return;}}throw std::runtime_error("Cannot create profile test directory");}~Directory(){std::error_code error;std::filesystem::remove_all(path,error);}};
SecretBytes key(const std::string& value){return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
bool same(const SecretBytes& secret,const std::string& expected){const auto bytes=secret.view();return bytes.size()==expected.size()&&std::equal(bytes.begin(),bytes.end(),expected.begin());}
const std::string openai="profile-fixture-openai-key",claude="profile-fixture-claude-key";
std::vector<ProviderProfileRoute> policy(){return {{"openai.chat","openai","https://api.openai.com/v1/chat/completions","server","fixture:openai:chat",ProviderWire::chat_completions},{"openai.responses","openai","https://api.openai.com/v1/responses","server","fixture:openai:responses",ProviderWire::responses},{"anthropic.messages","anthropic","https://api.anthropic.com/v1/messages","server","fixture:anthropic:messages",ProviderWire::anthropic_messages}};}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try{
        Directory directory;const auto database=(directory.path/"profiles.sqlite").string();const std::vector<std::string> imports{argv[1],argv[2]};
        {
            PersistenceService store(database,imports);ProviderProfiles profiles(store,policy());require(profiles.snapshot().revision==0&&profiles.snapshot().profiles.empty(),"New registry must be empty without altering legacy setup");
            store.put_information("native-provider","active",R"({"legacy":"preserved"})").get();
            auto value=profiles.save("openai","openai.chat","fixture-openai",key(openai),0,true);require(value.revision==1&&value.active=="openai"&&value.profiles.size()==1,"First encrypted profile publication failed");
            value=profiles.save("claude","anthropic.messages","fixture-claude",key(claude),1);require(value.revision==2&&value.active=="openai"&&value.profiles.size()==2,"Second provider must preserve first profile and active selection");
            require(same(profiles.credential("openai"),openai)&&same(profiles.credential("claude"),claude),"Keys must resolve only through their own profile purpose");
            const auto source=store.information("native-provider-profiles","registry").get();require(source.find(openai)==std::string::npos&&source.find(claude)==std::string::npos,"Profile metadata cannot contain plaintext keys");
            const auto credentials=store.credentials("server").get();require(credentials.size()==2,"Both keys must use encrypted credential storage");
            for(const auto& credential:credentials){rejects<Conflict>([&]{store.resolve_credential("server",credential.id,"wrong-profile-purpose").get();});}
            rejects<std::invalid_argument>([&]{profiles.save("new-claude","anthropic.messages","fixture-claude",key(""),2);});
            rejects<std::invalid_argument>([&]{profiles.save("openai","anthropic.messages","fixture-claude",key(""),2);});
            rejects<std::invalid_argument>([&]{profiles.save("unknown","outside-route","fixture",key(openai),2);});
            rejects<std::invalid_argument>([&]{profiles.save("bad-model","openai.chat",openai,key(openai),2);});
            require(profiles.snapshot().revision==2&&store.credentials("server").get().size()==2,"Prepublication rejection must preserve both profiles and keys");
            value=profiles.select("claude",2);require(value.revision==3&&value.active=="claude"&&value.profiles[1].revision==1,"Selection must not rotate keys or rewrite profile revision");
            ProviderProfiles other(store,policy());value=profiles.save("openai","openai.responses","fixture-openai-next",key(""),3);
            require(value.revision==4&&value.active=="claude"&&value.profiles[0].revision==2&&value.profiles[0].route_id=="openai.responses"&&same(profiles.credential("openai"),openai),"Same-provider route change must privately rebind its own saved key");
            rejects<Conflict>([&]{other.save("claude","anthropic.messages","fixture-claude-next",key(""),3);});
            std::future<bool> left=std::async(std::launch::async,[&]{try{profiles.save("concurrent-left","openai.chat","fixture-left",key(openai),4);return true;}catch(const Conflict&){return false;}});
            std::future<bool> right=std::async(std::launch::async,[&]{try{other.save("concurrent-right","anthropic.messages","fixture-right",key(claude),4);return true;}catch(const Conflict&){return false;}});
            require(static_cast<int>(left.get())+static_cast<int>(right.get())==1,"Concurrent profile writers require exactly one CAS winner");
            require(profiles.snapshot().revision==5&&profiles.snapshot().profiles.size()==3&&same(profiles.credential("claude"),claude),"Losing writer must not overwrite another profile");
            const auto preserved=store.information("native-provider-profiles","registry").get();
            {XlangSqlite inject(database,imports);inject.execute("CREATE TRIGGER fail_profile_publish BEFORE UPDATE ON information WHEN NEW.category='native-provider-profiles' BEGIN SELECT RAISE(ABORT,'profile publication fixture failure'); END");}
            rejects<DatabaseError>([&]{profiles.save("claude","anthropic.messages","fixture-rejected",key("fixture-new-candidate"),5,true);});
            require(store.information("native-provider-profiles","registry").get()==preserved&&profiles.snapshot().active=="claude"&&same(profiles.credential("claude"),claude),"Actual SQLite publication rollback must preserve registry, selection and active key");
            {XlangSqlite inject(database,imports);inject.execute("DROP TRIGGER fail_profile_publish");}
            require(store.information("native-provider","active").get()==R"({"legacy":"preserved"})","Registry component must not overwrite existing product configuration");
            store.compare_information("cas-fixture","entry",R"({"v":1})",{}).get();rejects<Conflict>([&]{store.compare_information("cas-fixture","entry",R"({"v":2})",{}).get();});
            rejects<Conflict>([&]{store.compare_information("cas-fixture","entry",R"({"v":2})",R"({"v":0})").get();});
            store.compare_information("cas-fixture","entry",R"({"v":2})",R"({"v":1})").get();require(store.information("cas-fixture","entry").get()==R"({"v":2})","Durable CAS must preserve exact expected-payload ownership");
        }
        {PersistenceService store(database,imports);ProviderProfiles reopened(store,policy());const auto value=reopened.snapshot();require(value.revision==5&&value.active=="claude"&&value.profiles.size()==3&&same(reopened.credential("openai"),openai)&&same(reopened.credential("claude"),claude),"Restart must preserve independent encrypted provider identities");auto changed=policy();changed[0].endpoint="https://unapproved.invalid";changed[1].endpoint="https://unapproved.invalid";ProviderProfiles mismatch(store,changed);rejects<DatabaseError>([&]{mismatch.snapshot();});}
        const auto migration_database=(directory.path/"migration.sqlite").string();
        {
            PersistenceService store(migration_database,imports);ProviderProfiles profiles(store,policy());
            store.put_credential("server","legacy-key","fixture:openai:chat","Legacy provider fixture",key(openai),0).get();
            const auto legacy=Json{{"provider","openai"},{"model","fixture-legacy"},{"endpoint",policy()[0].endpoint},{"credential_id","legacy-key"},{"revision",7}}.dump();
            store.put_information("native-provider","active",legacy).get();
            std::unique_ptr<ExecutionPlatform> candidate;
            auto prepare=[&](const SavedProviderProfile& profile,const ProviderProfileRoute& route,bool invalid){
                require(same(store.resolve_credential(route.credential_scope,profile.credential_id,route.credential_purpose).get(),openai),"Candidate must resolve the exact encrypted profile key");
                AgentSettings settings;settings.provider.model=profile.model;settings.provider.endpoint=route.endpoint;settings.provider.wire=route.wire;
                settings.credential=CredentialReference{route.credential_scope,profile.credential_id,route.credential_purpose};if(invalid)settings.max_turns=0;
                candidate=std::make_unique<ExecutionPlatform>(store,std::move(settings),1,8);
            };
            rejects<Conflict>([&]{profiles.import_existing("openai","openai.responses","fixture-legacy","legacy-key",7);});
            rejects<std::invalid_argument>([&]{profiles.import_existing("openai","openai.chat",openai,"legacy-key",7);});
            rejects<std::invalid_argument>([&]{profiles.import_existing("openai","openai.chat","fixture-legacy","legacy-key",0);});
            rejects<std::invalid_argument>([&]{profiles.import_existing("openai","openai.chat","fixture-legacy","legacy-key",7,[&](const auto& profile,const auto& route){prepare(profile,route,true);});});
            require(profiles.snapshot().revision==0&&store.credentials("server").get().size()==1&&store.information("native-provider","active").get()==legacy,"Rejected migration must retain legacy record/key without publishing a registry");
            const auto imported=profiles.import_existing("openai","openai.chat","fixture-legacy","legacy-key",7,[&](const auto& profile,const auto& route){require(profiles.snapshot().revision==0,"Candidate must precede migration publication");prepare(profile,route,false);});
            require(candidate&&candidate->available()&&candidate->models()==std::vector<std::string>{"fixture-legacy"},"Migration validator must prepare a real native execution service");
            require(imported.revision==7&&imported.active=="openai"&&imported.profiles[0].revision==7&&imported.profiles[0].credential_id=="legacy-key"&&store.credentials("server").get().size()==1,"Migration must preserve revision and encrypted credential reference without rotation");
            candidate.reset();const auto preserved=store.information("native-provider-profiles","registry").get();
            rejects<Conflict>([&]{profiles.import_existing("other","openai.chat","fixture-other","legacy-key",7);});
            rejects<std::invalid_argument>([&]{profiles.save("openai","openai.chat","fixture-next",key(""),7,true,[&](const auto& profile,const auto& route){prepare(profile,route,true);});});
            rejects<std::invalid_argument>([&]{profiles.select("openai",7,[&](const auto& profile,const auto& route){prepare(profile,route,true);});});
            require(store.information("native-provider-profiles","registry").get()==preserved&&same(profiles.credential("openai"),openai),"Failed candidate service creation must not change committed selection or key");
            require(store.information("native-provider","active").get()==legacy,"Migration and profile validation must preserve the legacy source record");
        }
        {PersistenceService store(migration_database,imports);ProviderProfiles reopened(store,policy());const auto restored=reopened.snapshot();require(restored.revision==7&&restored.profiles[0].credential_id=="legacy-key"&&same(reopened.credential("openai"),openai),"Migration must survive restart with original key reference");}
        {
            PersistenceService store((directory.path/"migration-race.sqlite").string(),imports);ProviderProfiles left(store,policy()),right(store,policy());
            store.put_credential("server","legacy-key","fixture:openai:chat","Concurrent migration fixture",key(openai),0).get();
            std::barrier ready(2);
            auto migrate=[&](ProviderProfiles& profiles,const std::string& id){try{profiles.import_existing(id,"openai.chat","fixture-legacy","legacy-key",7,[&](const auto&,const auto&){ready.arrive_and_wait();});return true;}catch(const Conflict&){return false;}};
            auto first=std::async(std::launch::async,[&]{return migrate(left,"legacy-left");});auto second=std::async(std::launch::async,[&]{return migrate(right,"legacy-right");});
            require(static_cast<int>(first.get())+static_cast<int>(second.get())==1,"Concurrent initial migration must have exactly one CAS winner");
            require(left.snapshot().profiles.size()==1&&left.snapshot().revision==7&&store.credentials("server").get().size()==1,"Migration race cannot duplicate or rotate the legacy key");
        }
        std::cout<<"Native provider profiles passed encrypted key isolation, restart, route binding, concurrent CAS, actual SQLite rollback, candidate service validation and original credential migration through xlang3; no provider inference or product enrollment tested\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
