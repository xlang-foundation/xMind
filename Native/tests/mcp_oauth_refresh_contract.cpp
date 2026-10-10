#include "agentflow/mcp_oauth.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <iostream>
using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value){if(!value)throw std::runtime_error("Native refresh fixture invariant failed");}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected native refresh rejection did not occur");}
SecretBytes secret(std::string_view value){return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
std::string text(const SecretBytes& value){const auto bytes=value.view();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
constexpr std::string_view refresh="synthetic refresh +&=";
const std::vector<std::string> scopes{"tools.read","tools.list"};
McpOAuthDiscovery discovery(std::string origin,std::string mode){return {{origin+"/mcp/success",{origin+"/issuer"},scopes},{origin+"/issuer",origin+"/authorize",origin+"/refresh/"+mode,{},scopes,{"none"},true,false,true,true}};}
McpOAuthRefreshAttempt attempt(const McpOAuthDiscovery& value){return {value,"synthetic-public-client",value.authorization.token_endpoint,secret(refresh),scopes,std::chrono::steady_clock::now()+10s};}
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
    const std::string mode=argv[1],origin=argv[2];const auto value=discovery(origin,mode);
    if(mode=="unit"){
        const auto form=mcp_oauth_refresh_grant_form("synthetic +&é= client","https://resource.example.test/mcp?tenant=a%20b&mode=read",secret(refresh),scopes);
        const auto no_scope=mcp_oauth_refresh_grant_form("synthetic-public-client",value.resource.resource,secret(refresh),{});
        rejects<McpProtocolError>([&]{mcp_oauth_refresh_grant_form("client","http://127.0.0.1:1/mcp",secret(refresh),scopes);});
        for(const auto bad:{"","bad\r\ntoken","bad\ttoken"})rejects<McpProtocolError>([&]{mcp_oauth_refresh_grant_form("client",value.resource.resource,secret(bad),scopes);});
        rejects<McpProtocolError>([&]{mcp_oauth_refresh_grant_form("client",value.resource.resource,secret(refresh),{"tools.read","tools.read"});});
        auto rotated=mcp_oauth_refresh_response(secret(R"({"access_token":"synthetic-refreshed-access","token_type":"Bearer","refresh_token":"synthetic rotated refresh +&=","expires_in":600,"scope":"tools.read"})"),secret(refresh),scopes);
        require(text(rotated.access_token)=="synthetic-refreshed-access"&&text(*rotated.refresh_token)=="synthetic rotated refresh +&="&&rotated.expires_in==600&&rotated.scopes==std::vector<std::string>{"tools.read"});
        auto retained=mcp_oauth_refresh_response(secret(R"({"access_token":"synthetic-refreshed-access","token_type":"Bearer"})"),secret(refresh),scopes);
        require(text(*retained.refresh_token)==refresh&&retained.scopes==scopes);
        for(const auto* bad:{R"({"access_token":"access","token_type":"Bearer","scope":"tools.admin"})",R"({"access_token":"access","access_token":"duplicate","token_type":"Bearer"})",R"({"access_token":"access","token_type":"Bearer","refresh_token":""})",R"({"error":"invalid_grant","access_token":"access","token_type":"Bearer"})"})rejects<McpProtocolError>([&]{mcp_oauth_refresh_response(secret(bad),secret(refresh),scopes);});
        rejects<McpProtocolError>([&]{mcp_oauth_refresh_response(secret(R"({"access_token":"access","token_type":"Bearer","scope":"tools.read"})"),secret(refresh),{});});
        for(const auto* change:{"endpoint","issuer","resource","auth"}){auto invalid=value;if(std::string(change)=="endpoint")invalid.authorization.token_endpoint+="/different";if(std::string(change)=="issuer")invalid.authorization.issuer+="/different";if(std::string(change)=="resource")invalid.resource.resource="http://127.0.0.1:1/mcp";if(std::string(change)=="auth")invalid.authorization.token_auth_methods={"client_secret_basic"};rejects<McpProtocolError>([&]{McpOAuthRefreshAttempt invalid_owner(invalid,"client",value.authorization.token_endpoint,secret(refresh),scopes,std::chrono::steady_clock::now()+10s);});}
        rejects<McpTransportTimeout>([&]{McpOAuthRefreshAttempt invalid(value,"client",value.authorization.token_endpoint,secret(refresh),scopes,std::chrono::steady_clock::now());});
        auto cancelled=attempt(value);require(cancelled.ready());cancelled.cancel();require(!cancelled.ready());rejects<McpProtocolError>([&]{cancelled.exchange(std::chrono::steady_clock::now()+1s);});
        auto stopped=attempt(value);std::stop_source stop;stop.request_stop();rejects<McpTransportCancelled>([&]{stopped.exchange(std::chrono::steady_clock::now()+1s,stop.get_token());});require(!stopped.ready());rejects<McpProtocolError>([&]{stopped.exchange(std::chrono::steady_clock::now()+1s);});
        auto deadline=attempt(value);rejects<McpTransportTimeout>([&]{deadline.exchange(std::chrono::steady_clock::now());});require(!deadline.ready());
        auto original=attempt(value);auto moved=std::move(original);require(!original.ready()&&moved.ready());moved.cancel();
        std::cout<<nlohmann::json{{"form",text(form)},{"no_scope",text(no_scope)},{"fixture",true}}.dump()<<std::endl;return 0;
    }
    auto owner=attempt(value);require(owner.ready());
    if(mode=="rotate"||mode=="retain"){
        auto result=owner.exchange(std::chrono::steady_clock::now()+5s);require(text(result.access_token)=="synthetic-refreshed-access"&&result.expires_in==600&&result.scopes==std::vector<std::string>{"tools.read"});require(result.refresh_token&&text(*result.refresh_token)==(mode=="rotate"?"synthetic rotated refresh +&=":refresh));
    }else if(mode=="scope-expanded"||mode=="bad-json")rejects<McpProtocolError>([&]{owner.exchange(std::chrono::steady_clock::now()+5s);});
    else rejects<McpTransportError>([&]{owner.exchange(std::chrono::steady_clock::now()+5s);});
    require(!owner.ready());rejects<McpProtocolError>([&]{owner.exchange(std::chrono::steady_clock::now()+5s);});
    std::cout<<"Native single-use refresh protocol fixture passed "<<mode<<"; no persistence, automatic refresh or tool-call replay claimed"<<std::endl;return 0;
}catch(const std::exception&){std::cerr<<"Native refresh protocol fixture failed; private response material was not reflected\n";return 1;}}
