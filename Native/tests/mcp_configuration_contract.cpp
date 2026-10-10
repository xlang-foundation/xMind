#include "agentflow/mcp_configuration.hpp"
#include "agentflow/mcp_client_factory.hpp"
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
        {
            const auto http_database=(root.path/"http-state.sqlite").string();Json server{{"id","http-fixture"},{"transport","http"},{"endpoint","https://fixture.example.test/mcp"},{"credential",{{"scope","server"},{"id","http-fixture-key"}}}};
            const auto desired=[&]{return Json{{"servers",Json::array({server})}}.dump();};std::string purpose;
            {
                PersistenceService store(http_database,imports);McpConfigurationStore configurations(store);const auto first=configurations.apply(desired());require(first[0].transport=="http" && first[0].revision==1 && first[0].executable.empty() && first[0].credentials.empty() && first[0].bearer->id=="http-fixture-key","HTTP configuration must keep endpoint/key references separate from process configuration");
                purpose=mcp_credential_purpose(first[0],"BEARER");const std::string secret="labeled-private-http-key";store.put_credential("server","http-fixture-key",purpose,"labeled HTTP key",SecretBytes({reinterpret_cast<const std::uint8_t*>(secret.data()),secret.size()}),0).get();
                const auto unchanged=store.information("native-mcp","servers").get();require(unchanged.find(secret)==std::string::npos,"HTTP metadata must never retain plaintext credentials");
                for(const auto& endpoint:{"http://remote.example.test/mcp","https://user:password@fixture.example.test/mcp","https://fixture.example.test/mcp#fragment","https://fixture.example.test/mcp\r\nInjected"}){auto invalid=server;invalid["endpoint"]=endpoint;rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});}
                for(const auto* key:{"executable","arguments","headers","credentials"}){auto invalid=server;invalid[key]="forbidden";rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});}
                auto invalid=server;invalid["credential"]["value"]=secret;rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});invalid=server;invalid["credential"]["scope"]="team";rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});
                require(store.information("native-mcp","servers").get()==unchanged,"Rejected HTTP replacements must preserve exact persisted metadata");
                server["enabled"]=false;const auto disabled=configurations.apply(desired());require(disabled[0].revision==2 && mcp_credential_purpose(disabled[0],"BEARER")==purpose,"Enablement changes must preserve endpoint credential purpose");
                rejects<std::invalid_argument>([&]{connect_mcp_client(disabled[0],store,std::chrono::steady_clock::now()+std::chrono::seconds(5));});
                std::stop_source cancellation;cancellation.request_stop();rejects<McpTransportCancelled>([&]{connect_mcp_client(first[0],store,std::chrono::steady_clock::now()+std::chrono::seconds(5),cancellation.get_token());});rejects<McpTransportTimeout>([&]{connect_mcp_client(first[0],store,std::chrono::steady_clock::now());});
                server["endpoint"]="https://other.example.test/mcp";const auto changed=configurations.apply(desired());require(changed[0].revision==3 && mcp_credential_purpose(changed[0],"BEARER")!=purpose,"HTTP endpoint changes must rotate config and require newly bound keys");rejects<Conflict>([&]{store.resolve_credential("server","http-fixture-key",mcp_credential_purpose(changed[0],"BEARER")).get();});store.close();
            }
            PersistenceService reopened(http_database,imports);McpConfigurationStore configurations(reopened);const auto values=configurations.load();require(values.size()==1 && values[0].transport=="http" && values[0].revision==3 && values[0].endpoint=="https://other.example.test/mcp","HTTP metadata/revisions must survive SQLite reopen");require(!reopened.resolve_credential("server","http-fixture-key",purpose).get().view().empty(),"Original encrypted key must survive without accepting a changed endpoint purpose");configurations.apply(R"({"servers":[]})");rejects<Conflict>([&]{configurations.apply(desired());});reopened.close();
        }
        std::cout<<"Native stdio/HTTP configuration passed embedded-xlang3 persistence, backend revisions, encrypted endpoint/command purpose binding, invalid mixed/plaintext/URL/scope rejection, exact replacement preservation, reopen and retired identities. No peer/model/UI activation claimed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
