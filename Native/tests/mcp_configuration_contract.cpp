#include "agentflow/mcp_configuration.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <random>
using namespace agentflow;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected configuration rejection did not occur");}
struct Directory {
    std::filesystem::path parent=std::filesystem::temp_directory_path(),path=parent/("xmind-mcp-config-"+std::to_string(std::random_device{}()));
    Directory(){require(std::filesystem::create_directory(path),"Fixture directory must be newly created");}
    ~Directory(){if(path.parent_path()==parent && path.filename().string().starts_with("xmind-mcp-config-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try {
        Directory root;const auto database=(root.path/"state.sqlite").string();const std::vector<std::string> imports{argv[1],argv[2]};
        // Metadata-only executable path: this fixture never launches a peer.
        Json server{{"id","test-server"},{"transport","stdio"},{"executable",(root.path/"fixture.exe").string()},{"working_directory",root.path.string()},{"arguments",Json::array({"--labeled-fixture"})},{"credentials",Json::array({{{"name","test_key"},{"scope","server"},{"id","fixture-key"}}})}};
        const auto desired=[&]{return Json{{"servers",Json::array({server})}}.dump();};
        std::string bound_purpose;
        {
            PersistenceService store(database,imports);McpConfigurationStore configurations(store);require(configurations.load().empty(),"Absent MCP configuration must not invent a server");
            const auto first=configurations.apply(desired());require(first.size()==1 && first[0].revision==1 && first[0].credentials[0].name=="TEST_KEY","Backend must assign revision and normalize environment identity");
            bound_purpose=mcp_credential_purpose(first[0],"TEST_KEY");const std::string secret="labeled-private-MCP-credential";
            store.put_credential("server","fixture-key",bound_purpose,"labeled private MCP fixture",SecretBytes({reinterpret_cast<const std::uint8_t*>(secret.data()),secret.size()}),0).get();
            require(store.information("native-mcp","servers").get().find(secret)==std::string::npos,"Configuration must retain only credential references");
            require(configurations.apply(desired())[0].revision==1,"Identical configuration must not rotate revision");
            const auto unchanged=store.information("native-mcp","servers").get();
            auto invalid=server;invalid["revision"]=7;rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});
            invalid=server;invalid["environment"]={{"TEST_KEY",secret}};rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});
            invalid=server;invalid["credentials"][0]["scope"]="another-team";rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});
            invalid=server;invalid["credentials"].push_back({{"name","TEST_KEY"},{"scope","server"},{"id","other"}});rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});
            invalid=server;invalid["executable"]="relative.exe";rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});
            invalid=server;invalid["transport"]="http";rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});
            rejects<std::invalid_argument>([&]{configurations.apply(R"({"servers":[],"servers":[]})");});
            rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({server,server})}}.dump());});
            require(store.information("native-mcp","servers").get()==unchanged,"Rejected replacement must preserve the exact previous configuration");
            server["enabled"]=false;const auto disabled=configurations.apply(desired());require(disabled[0].revision==2 && !disabled[0].enabled,"Disable must be a persisted backend revision");
            require(mcp_credential_purpose(disabled[0],"test_key")==bound_purpose,"Non-command changes must not silently change credential purpose");
            server["arguments"].push_back("changed-command");const auto changed=configurations.apply(desired());require(changed[0].revision==3,"Command change must rotate revision");require(mcp_credential_purpose(changed[0],"TEST_KEY")!=bound_purpose,"Command change must require a newly bound credential");
            rejects<Conflict>([&]{store.resolve_credential("server","fixture-key",mcp_credential_purpose(changed[0],"TEST_KEY")).get();});store.close();
        }
        {
            PersistenceService reopened(database,imports);McpConfigurationStore configurations(reopened);const auto values=configurations.load();require(values.size()==1 && values[0].revision==3 && !values[0].enabled,"Settings and revision must survive backend reopen");
            require(reopened.resolve_credential("server","fixture-key",bound_purpose).get().view().size()>0,"Encrypted referenced credential must survive independently of public metadata");
            configurations.apply(R"({"servers":[]})");rejects<Conflict>([&]{configurations.apply(desired());});require(configurations.load().empty(),"Removed identities must stay retired");reopened.close();
        }
        std::cout<<"Native MCP configuration passed embedded-xlang3 persistence, backend revisions, encrypted purpose-bound credential references, invalid/duplicate/plain-env/scope rejection, exact replacement preservation, reopen and retired identities. No peer/model/UI activation claimed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
