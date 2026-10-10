// Actual native service, HTTP routes and embedded-xlang3 persistence. Credentials
// belong to an independent synthetic HTTPS authority, never a real account.
#include "agentflow/mcp_oauth_service.hpp"
#include "agentflow/mcp_oauth_credentials.hpp"
#include "agentflow/http_server.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <thread>
#include <fstream>
using namespace agentflow;
namespace {
void require(bool value){if(!value)throw std::runtime_error("Native renewal fixture invariant failed");}
SecretBytes secret(std::string_view value){return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
std::string_view text(const SecretBytes& value){return {reinterpret_cast<const char*>(value.view().data()),value.view().size()};}
void verify(PersistenceService& store,const std::vector<McpServerSetting>& configs,bool negative){
    McpOAuthService observer(store,configs);
    for(const auto& config:configs){
        const auto mode=config.id.substr(8);const auto receipt=store.mcp_oauth_refresh("renew-"+mode).get();
        const bool committed=!negative&&(mode=="rotate"||mode=="retain");
        require(receipt.state==(negative?"cancelled":committed?"committed":"uncertain"));
        auto grant=McpOAuthCredentialStore(store).load(config);require(grant.revision==(committed?2:1));
        require(text(grant.tokens.access_token)==(committed?"synthetic-renewed-access":"synthetic-old-access"));
        require(grant.tokens.refresh_token&&text(*grant.tokens.refresh_token)==(committed&&mode=="rotate"?"synthetic rotated refresh +&=":"synthetic refresh +&="));
        require(grant.tokens.scopes==(committed?std::vector<std::string>{"tools.read"}:std::vector<std::string>{"tools.read","tools.list"}));
        const auto observed=observer.status(receipt.binding.request_id);
        require(observed.state==(negative?"cancelled":committed?"connected":"failed")&&observed.authorization_url.empty());
        require(observer.renew(config.id,1,1,receipt.binding.request_id).credential_revision==(committed?2:1));
        require(observer.cancel(receipt.binding.request_id).state==observed.state);
    }
}
}
int main(int argc,char** argv){if(argc!=5)return 2;try{
    const auto database=std::string(argv[1])+"/renewal.sqlite",origin=std::string(argv[4]);
    const std::vector<std::string> imports{argv[2],argv[3]};std::vector<McpServerSetting> configs;bool negative=false;
    {
        PersistenceService store(database,imports);auto definitions=nlohmann::json::array();
        for(const auto* mode:{"rotate","retain","scope-expanded","bad-json","http-failure","lost-reply","redirect","cancel","publish-fault"})definitions.push_back({{"id",std::string("renewal.")+mode},{"transport","http"},{"endpoint",origin+"/renew-mcp/"+mode},{"oauth",{{"scope","server"},{"id",std::string("renewal-grant-")+mode},{"issuer",origin+"/renew-issuer/"+mode},{"client_id","synthetic-public-client"}}}});
        configs=McpConfigurationStore(store).apply(nlohmann::json{{"servers",definitions}}.dump());
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        for(const auto& config:configs)McpOAuthCredentialStore(store).save(config,origin+"/renew-token/"+config.id.substr(8),{secret("synthetic-old-access"),secret("synthetic refresh +&="),600,{"tools.read","tools.list"}},now,0);
        XlangSqlite inspect(database,imports);
        inspect.execute("CREATE TRIGGER reject_renewal_publish BEFORE UPDATE ON information WHEN OLD.category='native-mcp-oauth-refresh' AND OLD.id='renew-publish-fault' AND instr(NEW.payload,'\"state\":\"committed\"')>0 BEGIN SELECT RAISE(ABORT,'synthetic renewal publication fault'); END");
        McpOAuthService service(store,configs);HttpServer http(store,"synthetic-renewal-service-owner-token",nullptr,nullptr,{}, {},{},nullptr,nullptr,nullptr,nullptr,&service);
        const auto port=http.bind(0);std::jthread listener([&]{http.listen();});std::cout<<nlohmann::json{{"port",port},{"fixture",true}}.dump()<<std::endl;
        std::string instruction;const bool received=bool(std::getline(std::cin,instruction));
        http.stop();listener.join();service.stop();require(received&&(instruction=="verify-renewals"||instruction=="verify-negative"));negative=instruction=="verify-negative";
        verify(store,configs,negative);std::cout<<"native-renewals-verified"<<std::endl;
    }
    {PersistenceService reopened(database,imports);verify(reopened,configs,negative);}
    std::cout<<"native-renewals-reopened"<<std::endl;return 0;
}catch(const std::exception& error){std::ofstream diagnostic(std::string(argv[1])+"/synthetic-failure.txt");diagnostic<<error.what();std::cerr<<"Native renewal fixture failed; private grants were not reflected\n";return 1;}}
