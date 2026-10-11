#include "agentflow/a2a_client.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){
    httplib::Server peer;std::atomic<bool> accept_remote=false,legacy_only=false,bad_request=false,legacy_unavailable=false,json_rpc_media_valid=true;std::atomic<int> discovery_count=0;int port=0;
    std::vector<std::pair<std::string,std::string>> calls;std::mutex calls_mutex;
    auto serve_card=[&](const httplib::Request& request,httplib::Response& response){
        ++discovery_count;
        if(request.get_header_value("A2A-Version")=="1.0"&&legacy_only.load()){
            response.status=400;
            if(bad_request.load())response.set_content(Json{{"type","https://a2a-protocol.org/errors/invalid-parameters"},{"status",400},{"detail","private protocol diagnostic"},{"supportedVersions",Json::array({"0.3"})}}.dump(),"application/problem+json");
            else response.set_content(Json{{"type","https://a2a-protocol.org/errors/version-not-supported"},{"status",400},{"detail","private protocol diagnostic"},{"supportedVersions",legacy_unavailable.load()?Json::array({"1.0"}):Json::array({"0.3"})}}.dump(),"application/problem+json");return;
        }
        if(request.get_header_value("A2A-Version")=="1.0"){
            response.set_header("A2A-Version","1.0");
            const auto endpoint="http://127.0.0.1:"+std::to_string(port)+"/a2a";
            if(accept_remote.load())response.set_content(Json{{"name","Peer"},{"description","Loopback test peer"},{"version","0.1"},{"supportedInterfaces",Json::array({{{"url",endpoint},{"protocolBinding","JSONRPC"},{"protocolVersion","1.0"}}})},{"skills",Json::array()}}.dump(),"application/json");
            else response.set_content(Json{{"name","Peer"},{"description","Loopback test peer"},{"version","0.1"},{"supportedInterfaces",Json::array({{{"url","https://other.invalid/a2a"},{"protocolBinding","JSONRPC"},{"protocolVersion","1.0"}}})},{"skills",Json::array()}}.dump(),"application/json");
            return;
        }
        response.set_header("A2A-Version","0.3");response.set_content(Json{{"protocolVersion","0.3.0"},{"name","Legacy peer"},{"description","Loopback legacy peer"},{"version","0.1"},{"url","http://127.0.0.1:"+std::to_string(port)+"/a2a"},{"preferredTransport","JSONRPC"},{"skills",Json::array()}}.dump(),"application/json");
    };
    peer.Get("/.well-known/agent-card.json",serve_card);peer.Get("/custom/card.json",serve_card);
    peer.Post("/a2a",[&](const httplib::Request& request,httplib::Response& response){
        if(request.get_header_value("Content-Type")!="application/json")json_rpc_media_valid=false;
        const auto version=request.get_header_value("A2A-Version");Json body=Json::parse(request.body);
        {std::lock_guard lock(calls_mutex);calls.emplace_back(version,body.at("method").get<std::string>());}
        response.set_header("A2A-Version",version);const auto method=body.at("method").get<std::string>();const bool modern=version=="1.0";Json result;
        if(method==(modern?"SendMessage":"message/send"))result=modern?Json{{"task",{{"id","remote-task"},{"status",{{"state","TASK_STATE_SUBMITTED"}}}}}}:Json{{"kind","task"},{"id","remote-task"},{"status",{{"state","submitted"}}}};
        else if(method==(modern?"GetTask":"tasks/get"))result=modern?Json{{"id","remote-task"},{"status",{{"state","TASK_STATE_WORKING"}}}}:Json{{"kind","task"},{"id","remote-task"},{"status",{{"state","working"}}}};
        else if(method==(modern?"CancelTask":"tasks/cancel"))result=modern?Json{{"id","remote-task"},{"status",{{"state","TASK_STATE_CANCELED"}}}}:Json{{"kind","task"},{"id","remote-task"},{"status",{{"state","canceled"}}}};
        else {response.set_content(Json{{"jsonrpc","2.0"},{"id",body.at("id")},{"error",{{"code",-32601},{"message","private peer text must not escape"}}}}.dump(),"application/json");return;}
        response.set_content(Json{{"jsonrpc","2.0"},{"id",body.at("id")},{"result",std::move(result)}}.dump(),"application/json");
    });
    port=peer.bind_to_any_port("127.0.0.1");require(port>0,"Could not bind independent loopback peer");std::thread owner([&]{peer.listen_after_bind();});
    try{
        const auto card="http://127.0.0.1:"+std::to_string(port)+"/.well-known/agent-card.json";
        agentflow::A2aJsonRpcClient denied(card);bool rejected=false;try{(void)denied.discover(2s);}catch(const agentflow::TransportError&){rejected=true;}require(rejected,"A card must not redirect the configured peer to another origin");require(!denied.ready(),"Rejected peer metadata cannot make the client ready");
        accept_remote=true;agentflow::A2aJsonRpcClient client(card);const auto info=client.discover(2s);require(info.name=="Peer"&&info.agent_version=="0.1"&&info.protocol_version=="1.0"&&info.endpoint=="http://127.0.0.1:"+std::to_string(port)+"/a2a","Native discovery must select the configured-origin JSON-RPC interface");agentflow::A2aJsonRpcClient direct_card("http://127.0.0.1:"+std::to_string(port)+"/custom/card.json");require(direct_card.discover(2s).endpoint==info.endpoint,"An explicitly configured AgentCard path must be used directly");
        const auto sent=Json::parse(client.send_message(R"({"message":{"messageId":"stable-1","role":"ROLE_USER","parts":[{"text":"hello"}]}})",2s));require(sent.at("task").at("id")=="remote-task","SendMessage task result was not retained");
        const auto observed=Json::parse(client.get_task(R"({"id":"remote-task"})",2s));require(observed.at("status").at("state")=="TASK_STATE_WORKING","GetTask response was not returned");
        const auto cancelled=Json::parse(client.cancel_task(R"({"id":"remote-task"})",2s));require(cancelled.at("status").at("state")=="TASK_STATE_CANCELED","CancelTask response was not returned");
        bool safe_error=false;try{(void)client.get_task(R"({"id":"missing"})",2s);}catch(const agentflow::A2aRemoteError& error){safe_error=error.code==-32601&&std::string(error.what()).find("private")==std::string::npos;}require(safe_error,"Remote JSON-RPC errors must retain the code and redact peer prose");
        {std::lock_guard lock(calls_mutex);require(calls.size()==4,"Unexpected outbound call count");for(const auto& call:calls)require(call.first=="1.0","A2A version header was not pinned on every native request");require(calls[0].second=="SendMessage"&&calls[1].second=="GetTask"&&calls[2].second=="CancelTask","Version-1 methods were not selected");}
        {std::lock_guard lock(calls_mutex);calls.clear();}legacy_only=true;bad_request=true;const auto before_bad_request=discovery_count.load();agentflow::A2aJsonRpcClient refused(card);bool no_downgrade=false;try{(void)refused.discover(2s);}catch(const agentflow::ProviderHttpError& error){no_downgrade=error.status==400&&!error.a2a_version_not_supported;}require(no_downgrade&&discovery_count.load()==before_bad_request+1,"A generic problem response must not trigger an A2A protocol downgrade");bad_request=false;legacy_unavailable=true;const auto before_unsupported=discovery_count.load();agentflow::A2aJsonRpcClient unsupported(card);bool no_legacy=false;try{(void)unsupported.discover(2s);}catch(const agentflow::ProviderHttpError& error){no_legacy=error.status==400&&error.a2a_version_not_supported&&!error.a2a_supports_legacy_03;}require(no_legacy&&discovery_count.load()==before_unsupported+1,"A version error without legacy support must not trigger downgrade");legacy_unavailable=false;agentflow::A2aJsonRpcClient legacy(card);const auto old=legacy.discover(2s);require(old.protocol_version=="0.3"&&old.endpoint=="http://127.0.0.1:"+std::to_string(port)+"/a2a","Legacy AgentCard fallback was not negotiated");
        const auto old_sent=Json::parse(legacy.send_message(R"({"message":{"messageId":"stable-legacy","role":"user","parts":[{"kind":"text","text":"hello"}]}})",2s));require(old_sent.at("kind")=="task","Legacy send result was not returned");
        (void)legacy.get_task(R"({"id":"remote-task"})",2s);(void)legacy.cancel_task(R"({"id":"remote-task"})",2s);
        {std::lock_guard lock(calls_mutex);require(calls.size()==3,"Unexpected legacy outbound call count");require(calls[0]==std::pair<std::string,std::string>{"0.3","message/send"}&&calls[1]==std::pair<std::string,std::string>{"0.3","tasks/get"}&&calls[2]==std::pair<std::string,std::string>{"0.3","tasks/cancel"},"Legacy A2A wire was not selected");}
        require(discovery_count.load()==7,"Unexpected card discovery count");require(json_rpc_media_valid.load(),"A2A JSON-RPC requests must use the pinned binding's JSON content type");
    }catch(...){peer.stop();owner.join();throw;}
    peer.stop();owner.join();
    std::cout<<"Native A2A client passed origin-bound AgentCard discovery, version negotiation, v1 JSON-RPC send/get/cancel, response correlation and remote-error redaction. Integration, durable dispatch recovery, streaming, input-required and non-loopback TLS peer acceptance remain open.\n";
}
