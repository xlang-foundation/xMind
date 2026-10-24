#include "agentflow/remote_connection_credentials.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
using namespace agentflow;
using Json=nlohmann::json;
namespace {
void require(bool value){if(!value)throw std::runtime_error("Remote credential contract failed");}
template<class E,class F>void rejects(F action){try{action();}catch(const E&){return;}throw std::runtime_error("Expected remote credential refusal missing");}
SecretBytes secret(const std::string& value){return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
bool equal(const SecretBytes& a,const SecretBytes& b){return std::ranges::equal(a.view(),b.view());}
RemoteNegotiatedGrant grant(std::string server="server-a"){RemoteNegotiationRequirements requirements;requirements.now_unix_seconds=1000;return validate_remote_negotiation(Json{{"server_id",server},{"protocol_version",1},{"grant_id","grant-a"},{"grant_revision",1},{"expires_at",2000},{"authorized_scopes",Json::array({{{"id","workspace-a"},{"actions",Json::array({"runs.submit"})}}})},{"capabilities",{{"runs",1}}}}.dump(),requirements);}
}
int main(int argc,char** argv){if(argc!=3)return 2;std::filesystem::path file;try{
    file=std::filesystem::temp_directory_path()/("xmind-remote-credentials-"+std::to_string(std::random_device{}())+".sqlite");require(!std::filesystem::exists(file));const std::vector<std::string> imports{argv[1],argv[2]};ConnectionProfile original,current;
    const auto source=[](const std::string& origin){return Json{{"profiles",Json::array({{{"id","local"},{"name","Local"},{"kind","local"}},{{"id","remote-a"},{"name","Remote A"},{"kind","nexus"},{"origin",origin}}})}}.dump();};
    {
        PersistenceService store(file.string(),imports);ConnectionProfiles profiles(store);profiles.load_or_initialize();const auto catalog=profiles.apply(source("https://remote.example.invalid"),1);original=catalog.profiles.at(1);RemoteConnectionCredentials credentials(store,profiles);
        auto first=secret("isolated-remote-secret-first");auto second=secret("isolated-remote-secret-second");const auto observed=grant();const auto saved=credentials.save(original,observed,first,0,1000);require(saved.revision==1&&profiles.load().active_id=="local");
        auto loaded=credentials.load(original);require(loaded.revision==1&&loaded.grant.server_id=="server-a"&&equal(loaded.credential,first));
        rejects<Conflict>([&]{credentials.save(original,observed,second,0,1000);});
        rejects<RemoteNegotiationError>([&]{credentials.save(original,observed,second,1,2000);});
        rejects<Conflict>([&]{credentials.save(original,grant("server-b"),second,1,1000);});
        auto forged=original;++forged.revision;rejects<Conflict>([&]{credentials.save(forged,observed,second,1,1000);});forged=original;forged.origin="https://other.example.invalid";rejects<Conflict>([&]{credentials.load(forged);});
        loaded=credentials.load(original);require(loaded.revision==1&&equal(loaded.credential,first));const auto rotated=credentials.save(original,observed,second,1,1000);require(rotated.revision==2);rejects<Conflict>([&]{credentials.save(original,observed,first,1,1000);});
        const auto metadata=store.credentials("server").get();require(metadata.size()==1&&metadata[0].revision==2&&metadata[0].label=="Remote connection authorization");
        const auto public_catalog=store.information("native-connections","profiles").get();require(public_catalog.find("isolated-remote-secret")==std::string::npos&&public_catalog.find("grant-a")==std::string::npos&&public_catalog.find("server-a")==std::string::npos);store.close();
    }
    {
        PersistenceService store(file.string(),imports);ConnectionProfiles profiles(store);RemoteConnectionCredentials credentials(store,profiles);auto loaded=credentials.load(original);require(loaded.revision==2&&equal(loaded.credential,secret("isolated-remote-secret-second"))&&loaded.grant.grant_id=="grant-a"&&profiles.load().active_id=="local");
        auto catalog=profiles.load();catalog=profiles.apply(source("https://replacement.example.invalid"),catalog.revision);current=catalog.profiles.at(1);rejects<Conflict>([&]{credentials.load(original);});rejects<Conflict>([&]{credentials.load(current);});rejects<Conflict>([&]{credentials.save(original,grant(),secret("must-not-save"),2,1000);});
        require(credentials.save(current,grant("server-b"),secret("isolated-remote-secret-third"),2,1000).revision==3);require(credentials.load(current).grant.server_id=="server-b");rejects<Conflict>([&]{profiles.select("remote-a",catalog.revision);});require(profiles.load().active_id=="local");store.close();
    }
    {
        PersistenceService store(file.string(),imports);ConnectionProfiles profiles(store);RemoteConnectionCredentials credentials(store,profiles);const auto loaded=credentials.load(current);require(loaded.revision==3&&loaded.grant.server_id=="server-b"&&equal(loaded.credential,secret("isolated-remote-secret-third")));store.close();
    }
    for(const auto* suffix:{"","-wal","-shm"}){auto candidate=file;candidate+=suffix;if(std::filesystem::exists(candidate)){std::ifstream stream(candidate,std::ios::binary);const std::string content((std::istreambuf_iterator<char>(stream)),{});require(content.find("isolated-remote-secret")==std::string::npos&&content.find("grant-a")==std::string::npos);}}
    std::cout<<"Native xlang3/SQLite remote credentials preserve encrypted combined grant/token revisions across two database reopens; stale/duplicate/expired/server-mismatch writes refuse without replacement, changed origins require explicit new save, public catalog and database bytes expose no plaintext credential/grant. Local remains selected and remote activation refuses. No network authentication claim.\n";
    for(const auto* suffix:{"","-wal","-shm"}){auto candidate=file;candidate+=suffix;std::error_code ignored;std::filesystem::remove(candidate,ignored);}return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"; retained fixture "<<file.string()<<'\n';return 1;}}
