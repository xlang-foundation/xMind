#include "agentflow/mcp_oauth_callback.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <thread>
#include <charconv>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected native callback rejection did not occur");}
int finish(const std::string& mode){
    std::cout<<"Native callback fixture passed "<<mode<<"; no token exchange or persistence performed"<<std::endl;
    // Remain alive, with the receiver still constructed, while the independent
    // HTTP peer checks that receive() released its socket. Process exit alone
    // cannot establish this lifecycle property.
    require(std::cin.get()=='\n',"Independent live-process socket check was not acknowledged");
    return 0;
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try {
        const std::string mode=argv[1];
        McpOAuthLoopbackCallback receiver("/oauth/callback/native-test");
        const auto redirect=receiver.redirect_uri();
        const auto start=redirect.find(':',7)+1,end=redirect.find('/',start);unsigned port=0;
        require(std::from_chars(redirect.data()+start,redirect.data()+end,port).ec==std::errc{},"Fixture redirect must have a port");
        rejects<McpTransportError>([&]{McpOAuthLoopbackCallback duplicate("/oauth/callback/native-test",static_cast<std::uint16_t>(port));});
        for(const auto* path:{"relative","/bad?query","/regex.*","/fragment#","/bad%2Fpath"})rejects<std::invalid_argument>([&]{McpOAuthLoopbackCallback invalid(path);});
        McpOAuthDiscovery discovery{{"https://resource.example.test/mcp",{"https://issuer.example.test"},{}},{"https://issuer.example.test","https://issuer.example.test/authorize","https://issuer.example.test/token",{}, {},{"none"},true,false,true,true}};
        const auto expiry=std::chrono::steady_clock::now()+(mode=="owner-expiry"?150ms:5s);
        McpOAuthAuthorizationAttempt attempt(discovery,{discovery.authorization.issuer,"synthetic-public-client",mode=="binding"?redirect+"/different":redirect},{},expiry);
        std::cout<<nlohmann::json{{"redirect",redirect},{"authorization_url",attempt.authorization_url()},{"fixture",true}}.dump()<<std::endl;
        std::stop_source stop;
        std::jthread canceller;
        if(mode=="cancel")canceller=std::jthread([&]{std::this_thread::sleep_for(150ms);stop.request_stop();});
        if(mode=="pre-cancel")stop.request_stop();
        if(mode=="valid") {receiver.receive(attempt,expiry,stop.get_token());require(attempt.ready(),"Real socket callback must admit the native exchange");rejects<McpProtocolError>([&]{receiver.receive(attempt,expiry);});require(attempt.ready(),"Receiver reuse must not destroy a separately admitted exchange");attempt.cancel();}
        else if(mode=="denial") {try{receiver.receive(attempt,expiry);}catch(const McpOAuthAuthorizationDenied& error){require(error.reason=="access_denied","Native validated denial must keep exact allowlisted reason");require(!attempt.ready(),"Denial must clear authority");return finish(mode);}throw std::runtime_error("Expected actual browser denial");}
        else if(mode=="bad"||mode=="binding"||mode=="duplicate"||mode=="missing-issuer"||mode=="encoded-target")rejects<McpProtocolError>([&]{receiver.receive(attempt,expiry);});
        else if(mode=="cancel"||mode=="pre-cancel")rejects<McpTransportCancelled>([&]{receiver.receive(attempt,expiry,stop.get_token());});
        else if(mode=="timeout"||mode=="owner-expiry"||mode=="deadline")rejects<McpTransportTimeout>([&]{receiver.receive(attempt,mode=="deadline"?std::chrono::steady_clock::now():mode=="timeout"?std::chrono::steady_clock::now()+150ms:std::chrono::steady_clock::now()+5s);});
        else throw std::runtime_error("Unknown callback fixture");
        require(!attempt.ready(),"Finished callback fixture must leave no unconsumed code");
        return finish(mode);
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
