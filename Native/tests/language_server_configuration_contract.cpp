#include "agentflow/language_server_configuration.hpp"
#include "agentflow/process.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

using namespace agentflow;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Exception,class Fn>void rejects(Fn action){try{action();}catch(const Exception&){return;}throw std::runtime_error("Expected trusted language-server configuration rejection did not occur");}
struct TempDirectory {
    std::filesystem::path base=std::filesystem::temp_directory_path(),path=base/("xmind-language-server-config-"+std::to_string(std::random_device{}()));
    TempDirectory(){require(std::filesystem::create_directory(path),"Temporary fixture directory must be unique");}
    ~TempDirectory(){if(path.parent_path()==base&&path.filename().string().starts_with("xmind-language-server-config-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
std::string utf8(const std::filesystem::path& path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
}
int main(int argc,char** argv){if(argc!=4)return 2;try{
    TempDirectory temp;const auto executable=temp.path/"registered-server.exe";std::filesystem::copy_file(std::filesystem::u8path(argv[1]),executable);
    const auto imports=std::vector<std::string>{argv[2],argv[3]};const auto database=utf8(temp.path/"profiles.sqlite");
    Json p={{"id","rust"},{"executable",utf8(executable)},{"language_id","rust"},{"extensions",Json::array({".rs"})},{"arguments",Json::array()},{"environment",{{"PATH","trusted-path"}}},{"request_timeout_ms",30000}};
    const auto desired=[&]{return Json{{"profiles",Json::array({p})}}.dump();};
    const auto yaml=[&]{return std::string("profiles:\n  - id: rust\n    executable: ")+Json(utf8(executable)).dump()+"\n    language_id: rust\n    extensions: [.rs]\n    arguments: []\n    environment:\n      PATH: trusted-path\n    request_timeout_ms: 30000\n";};
    std::string first_identity;
    {
        PersistenceService store(database,imports);LanguageServerConfigurationStore config(store);
        require(config.load().empty(),"An absent trusted language-server registry must remain empty");
        auto first=config.apply(yaml());require(first.size()==1&&first[0].revision==1,"YAML must register one trusted server profile");first_identity=first[0].executable_id;require(first_identity==ForegroundProcess::executable_identity(utf8(executable)),"Configuration must bind the actual executable bytes");require(first[0].environment.size()==1&&first[0].environment[0].second=="trusted-path"&&!first[0].environment_credential_id.empty(),"Trusted environment must be usable in memory and reference an encrypted credential");
        const auto metadata=store.information("native-language-servers","profiles").get();require(metadata.find("trusted-path")==std::string::npos&&metadata.find("environment_credential_id")!=std::string::npos,"Profile metadata must contain only an opaque encrypted-environment reference");
        const auto credential=store.resolve_credential("server",first[0].environment_credential_id,"language-server.environment.rust").get();const auto credential_bytes=credential.view();const std::string credential_payload(reinterpret_cast<const char*>(credential_bytes.data()),credential_bytes.size());require(credential_payload.find("trusted-path")!=std::string::npos,"Configured environment must round-trip through the encrypted credential repository");
        require(config.apply(desired())[0].revision==1,"Identical JSON and YAML must preserve profile revision");const auto before=store.information("native-language-servers","profiles").get();
        auto relative=p;relative["executable"]="rust-analyzer.exe";rejects<std::invalid_argument>([&]{config.apply(Json{{"profiles",Json::array({relative})}}.dump());});
        auto spoof=p;spoof["executable_id"]="untrusted";rejects<std::invalid_argument>([&]{config.apply(Json{{"profiles",Json::array({spoof})}}.dump());});
        rejects<std::invalid_argument>([&]{config.apply(Json{{"profiles",Json::array({p,p})}}.dump());});
        rejects<std::invalid_argument>([&]{config.apply("profiles:\n  - id: duplicate\n    id: second\n");});
        require(store.information("native-language-servers","profiles").get()==before,"Rejected configuration must not change the durable registry");
        p["request_timeout_ms"]=45000;auto changed=config.apply(desired());require(changed[0].revision==2&&changed[0].executable_id==first_identity,"Policy changes must rotate revision while preserving file binding");
        {std::ofstream mutation(executable,std::ios::binary|std::ios::app);mutation<<"changed bytes";require(bool(mutation),"Executable fixture must mutate successfully");}
        rejects<std::invalid_argument>([&]{config.load();});auto rebound=config.apply(desired());require(rebound[0].revision==3&&rebound[0].executable_id!=first_identity,"Explicit reimport must detect changed executable bytes");store.close();
    }
    {
        PersistenceService store(database,imports);LanguageServerConfigurationStore config(store);auto restored=config.load();require(restored.size()==1&&restored[0].revision==3,"Trusted profile metadata and revision must survive SQLite reopen");require(restored[0].environment.size()==1&&restored[0].environment[0].second=="trusted-path","Encrypted trusted environment must decrypt after SQLite reopen");
        config.apply(R"({"profiles":[]})");rejects<Conflict>([&]{config.apply(desired());});require(config.load().empty(),"Retired language-server profile IDs cannot be reused");store.close();
    }
    std::cout<<"Trusted language-server YAML/JSON configuration passed xlang3 SQLite reopen, strict field validation, executable identity binding/reimport, revision and permanent retirement contracts. No model-controlled executable accepted.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
