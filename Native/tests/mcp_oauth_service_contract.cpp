#include "agentflow/mcp_oauth_service.hpp"
#include "agentflow/mcp_oauth_credentials.hpp"
#include "agentflow/http_server.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <thread>

using namespace agentflow;
int main(int argc,char** argv){if(argc!=5)return 2;try{
    PersistenceService store(std::string(argv[1])+"/state.sqlite",{argv[2],argv[3]});
    const auto server=[&](const char* id,const char* credential,const char* suffix){return nlohmann::json{{"id",id},{"transport","http"},{"endpoint",std::string(argv[4])+suffix},{"oauth",{{"scope","server"},{"id",credential},{"issuer","https://issuer.example.test/tenant"},{"client_id","synthetic-public-client"}}}};};
    auto settings=McpConfigurationStore(store).apply(nlohmann::json{{"servers",nlohmann::json::array({server("oauth.peer","pending-grant","/mcp"),server("authorized-peer","existing-grant","/authorized")})}}.dump());
    const auto secret=[](std::string_view value){return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});};
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    McpOAuthCredentialStore(store).save(settings[1],"https://issuer.example.test/token",{secret("synthetic-access-not-live"),{},3600,{}},now,0);
    McpOAuthService service(store,settings);HttpServer http(store,"synthetic-oauth-service-owner-token",nullptr,nullptr,{}, {},{},nullptr,nullptr,nullptr,nullptr,&service);
    const auto port=http.bind(0);std::jthread listener([&]{http.listen();});
    std::cout<<nlohmann::json{{"port",port},{"fixture",true}}.dump()<<std::endl;
    std::string done;const bool acknowledged=bool(std::getline(std::cin,done))&&done=="service-fixture-done";
    http.stop();listener.join();service.stop();
    if(!acknowledged)throw std::runtime_error("Independent native service HTTP checks were not acknowledged");
    if(store.credentials("server").get().size()!=1)throw std::runtime_error("Failed/denied setup must not publish an encrypted grant");
    std::cout<<"Native OAuth service fixture finished; actual TLS-negative discovery/status/cancel and native HTTP/view authentication boundaries exercised. No trusted HTTPS login or successful token exchange verified\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
