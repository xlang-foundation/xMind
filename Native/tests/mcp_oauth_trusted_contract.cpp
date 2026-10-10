#include "agentflow/mcp_oauth_service.hpp"
#include "agentflow/mcp_oauth_credentials.hpp"
#include "agentflow/mcp_client_factory.hpp"
#include "agentflow/http_server.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <thread>

using namespace agentflow;
namespace {
void require(bool valid){if(!valid)throw std::runtime_error("Native trusted OAuth fixture invariant failed");}
void verify(PersistenceService& store,const McpServerSetting& config){
    auto grant=McpOAuthCredentialStore(store).load(config);const auto text=[](const SecretBytes& secret){const auto bytes=secret.view();return std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size());};
    require(grant.revision==1&&text(grant.tokens.access_token)=="synthetic-positive-access-not-live"&&grant.tokens.refresh_token&&text(*grant.tokens.refresh_token)=="synthetic positive refresh not live");
    require(grant.expires_unix_ms&&*grant.expires_unix_ms==grant.acquired_unix_ms+600000&&grant.tokens.scopes==std::vector<std::string>{"tools.read"});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);auto client=connect_mcp_client(config,store,deadline);require(client->list_tools({},deadline).tools.empty());client->shutdown();
}
}
int main(int argc,char** argv){if(argc!=5)return 2;try{
    const auto database=std::string(argv[1])+"/state.sqlite",origin=std::string(argv[4]);
    {
        PersistenceService store(database,{argv[2],argv[3]});auto servers=nlohmann::json::array();
        for(const auto* kind:{"success","denied","bad-state","bad-issuer","bad-code","cancelled"})servers.push_back({{"id",std::string("oauth.")+kind},{"transport","http"},{"endpoint",origin+"/mcp/"+kind},{"oauth",{{"scope","server"},{"id",std::string("grant-")+kind},{"issuer",origin+"/issuer"},{"client_id","synthetic-public-client"}}}});
        const auto settings=McpConfigurationStore(store).apply(nlohmann::json{{"servers",servers}}.dump());McpOAuthService service(store,settings);HttpServer http(store,"synthetic-trusted-oauth-owner-token",nullptr,nullptr,{}, {},{},nullptr,nullptr,nullptr,nullptr,&service);
        const auto port=http.bind(0);std::jthread listener([&]{http.listen();});std::cout<<nlohmann::json{{"port",port},{"fixture",true}}.dump()<<std::endl;
        std::string instruction;const bool received=bool(std::getline(std::cin,instruction))&&instruction=="verify-success";
        // Stop the listener before any assertion can throw, so fixture failure
        // cannot leave the test waiting on a live HTTP thread.
        http.stop();listener.join();service.stop();require(received);require(store.credentials("server").get().size()==1);verify(store,settings.front());
        std::cout<<"native-grant-verified"<<std::endl;
    }
    {
        PersistenceService reopened(database,{argv[2],argv[3]});const auto settings=McpConfigurationStore(reopened).load();require(reopened.credentials("server").get().size()==1);verify(reopened,settings.front());
    }
    std::cout<<"native-grant-reopened"<<std::endl;return 0;
}catch(const std::exception&){std::cerr<<"Native trusted OAuth fixture failed; private grant material was not reflected\n";return 1;}}
