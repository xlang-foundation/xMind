#include "agentflow/connection_profiles.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <random>
using namespace agentflow;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class E,class F>void rejects(F action){try{action();}catch(const E&){return;}throw std::runtime_error("Expected connection profile rejection did not occur");}
struct TemporaryDatabase{
    std::filesystem::path path=std::filesystem::temp_directory_path()/("xmind-connections-"+std::to_string(std::random_device{}())+".sqlite");
    ~TemporaryDatabase(){for(const auto* suffix:{"","-wal","-shm"}){auto file=path;file+=suffix;std::error_code ignored;std::filesystem::remove(file,ignored);}}
};
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
    TemporaryDatabase database;const std::vector<std::string> imports{argv[1],argv[2]};ConnectionProfileCatalog saved;
    {
        PersistenceService store(database.path.string(),imports);ConnectionProfiles profiles(store);require(profiles.load().revision==0,"Absent connection registry must not be fabricated before explicit initialization");
        auto current=profiles.load_or_initialize();require(current.revision==1&&current.active_id=="local"&&current.profiles.size()==1&&current.profiles[0].name=="Local","First initialization must persist one active Local profile");
        const auto yaml=std::string("profiles:\n  - id: local\n    name: Local\n    kind: local\n    origin: \"\"\n  - id: company\n    name: CantorAI Nexus\n    kind: nexus\n    origin: https://nexus.example.com/xmind\n");
        current=profiles.apply(yaml,current.revision);require(current.revision==2&&current.profiles.size()==2&&current.active_id=="local"&&current.profiles[1].revision==1,"Adding a Nexus endpoint must retain Local selection and assign backend revisions");
        auto unchanged=profiles.apply(Json{{"profiles",Json::array({{{"id","local"},{"name","Local"},{"kind","local"},{"origin",""}},{{"id","company"},{"name","CantorAI Nexus"},{"kind","nexus"},{"origin","https://nexus.example.com/xmind"}}})}}.dump(),2);
        require(unchanged.revision==2,"Equivalent JSON/YAML input must be idempotent");
        for(const auto* value:{"https://user@nexus.example.com","http://nexus.example.com","https://nexus.example.com?token=x","https://nexus.example.com/","https://nexus.example.com/#fragment","https://nexus.example.com:","https://nexus.example.com:0","https://nexus.example.com:65536","https://nexus.example.com:abc","https://bad-.example.com","https://bad..example.com","https://2001:db8::1"}){
            auto invalid=Json{{"profiles",Json::array({{{"id","local"},{"name","Local"},{"kind","local"}},{{"id","bad"},{"name","Bad"},{"kind","nexus"},{"origin",value}}})}}.dump();rejects<std::invalid_argument>([&]{profiles.apply(invalid,2);});
        }
        auto secret_field=Json{{"profiles",Json::array({{{"id","local"},{"name","Local"},{"kind","local"}},{{"id","company"},{"name","CantorAI Nexus"},{"kind","nexus"},{"origin","https://nexus.example.com/xmind"},{"access_token","must-not-be-accepted"}}})}}.dump();rejects<std::invalid_argument>([&]{profiles.apply(secret_field,2);});
        const auto before_selection=store.information("native-connections","profiles").get();
        rejects<Conflict>([&]{profiles.select("company",1);});
        rejects<Conflict>([&]{profiles.select("company",2);});
        rejects<NotFound>([&]{profiles.select("missing",2);});
        require(store.information("native-connections","profiles").get()==before_selection,"Unauthenticated or missing connection selection must not change persisted metadata");
        current=profiles.select("local",2);require(current.active_id=="local"&&current.revision==2,"Selecting current Local must remain idempotent");
        auto changed=yaml;const auto at=changed.find("CantorAI Nexus");changed.replace(at,14,"Engineering Nexus");current=profiles.apply(changed,2);require(current.revision==3&&current.profiles[1].revision==2,"Profile metadata changes must rotate profile and catalog revisions");
        rejects<Conflict>([&]{profiles.select("company",3);});
        require(profiles.load().active_id=="local"&&profiles.load().revision==3,"Remote metadata edits must not authenticate or select a connection");
        current=profiles.apply(R"({"profiles":[{"id":"local","name":"Local","kind":"local"}]})",3);require(current.revision==4&&current.retired_ids==std::vector<std::string>{"company"},"Removed Nexus IDs must be retired");
        rejects<Conflict>([&]{profiles.apply(yaml,4);});const auto encoded=store.information("native-connections","profiles").get();require(encoded.find("must-not-be-accepted")==std::string::npos,"Connection metadata must not persist client-supplied credentials");saved=current;store.close();
    }
    {
        PersistenceService store(database.path.string(),imports);ConnectionProfiles profiles(store);const auto restored=profiles.load();require(restored.revision==saved.revision&&restored.active_id=="local"&&restored.profiles.size()==1&&restored.retired_ids==saved.retired_ids,"SQLite reopen must preserve Local selection and retired remote identities");store.close();
    }
    std::cout<<"Native xlang3 connection-profile registry passed first-use Local selection, YAML/JSON idempotence, HTTPS endpoint checks, unknown credential-field rejection, revision conflicts, profile retirement and SQLite reopen. Nexus authentication, command routing, UI selection and remote execution remain unimplemented by this checkpoint.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
