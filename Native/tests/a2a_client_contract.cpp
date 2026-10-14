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
    httplib::Server peer;std::atomic<bool> accept_remote=false,legacy_only=false,bad_request=false,legacy_unavailable=false,unsupported_auth=false,streaming_supported=true,json_rpc_media_valid=true,saw_include_artifacts=false;std::atomic<int> discovery_count=0;int port=0;
    std::vector<std::pair<std::string,std::string>> calls;std::vector<std::string> authorizations,last_event_ids;std::mutex calls_mutex;
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
        if(unsupported_auth.load()){response.set_content(Json{{"name","Peer"},{"description","Loopback test peer"},{"version","0.1"},{"supportedInterfaces",Json::array({{{"url",endpoint},{"protocolBinding","JSONRPC"},{"protocolVersion","1.0"}}})},{"securitySchemes",{{"api_key",{{"apiKeySecurityScheme",{{"location","header"},{"name","X-Peer-Key"}}}}}}},{"securityRequirements",Json::array({{{"schemes",{{"api_key",{{"list",Json::array()}}}}}}})},{"skills",Json::array()}}.dump(),"application/json");return;}
            if(accept_remote.load())response.set_content(Json{{"name","Peer"},{"description","Loopback test peer"},{"version","0.1"},{"capabilities",{{"streaming",streaming_supported.load()}}},{"supportedInterfaces",Json::array({{{"url",endpoint},{"protocolBinding","JSONRPC"},{"protocolVersion","1.0"}}})},{"skills",Json::array()}}.dump(),"application/json");
            else response.set_content(Json{{"name","Peer"},{"description","Loopback test peer"},{"version","0.1"},{"supportedInterfaces",Json::array({{{"url","https://other.invalid/a2a"},{"protocolBinding","JSONRPC"},{"protocolVersion","1.0"}}})},{"skills",Json::array()}}.dump(),"application/json");
            return;
        }
        response.set_header("A2A-Version","0.3");response.set_content(Json{{"protocolVersion","0.3.0"},{"name","Legacy peer"},{"description","Loopback legacy peer"},{"version","0.1"},{"capabilities",{{"streaming",true}}},{"url","http://127.0.0.1:"+std::to_string(port)+"/a2a"},{"preferredTransport","JSONRPC"},{"skills",Json::array()}}.dump(),"application/json");
    };
    peer.Get("/.well-known/agent-card.json",serve_card);peer.Get("/custom/card.json",serve_card);
    peer.Post("/a2a",[&](const httplib::Request& request,httplib::Response& response){
        if(request.get_header_value("Content-Type")!="application/json")json_rpc_media_valid=false;
        const auto version=request.get_header_value("A2A-Version");Json body=Json::parse(request.body);
        {std::lock_guard lock(calls_mutex);calls.emplace_back(version,body.at("method").get<std::string>());authorizations.push_back(request.get_header_value("Authorization"));last_event_ids.push_back(request.get_header_value("Last-Event-ID"));}
        response.set_header("A2A-Version",version);const auto method=body.at("method").get<std::string>();const bool modern=version=="1.0";Json result;
        if(method==(modern?"SendMessage":"message/send"))result=modern?Json{{"task",{{"id","remote-task"},{"status",{{"state","TASK_STATE_SUBMITTED"}}}}}}:Json{{"kind","task"},{"id","remote-task"},{"status",{{"state","submitted"}}}};
        else if(method==(modern?"GetTask":"tasks/get")){
            if(modern&&body.at("params").value("includeArtifacts",false))saw_include_artifacts=true;
            if(body.at("params").value("id",std::string{})=="missing")response.set_content(Json{{"jsonrpc","2.0"},{"id",body.at("id")},{"error",{{"code",-32601},{"message","private peer text must not escape"}}}}.dump(),"application/json");
            else result=modern?Json{{"id","remote-task"},{"status",{{"state","TASK_STATE_WORKING"}}}}:Json{{"kind","task"},{"id","remote-task"},{"status",{{"state","working"}}}};
            if(body.at("params").value("id",std::string{})=="missing")return;
        }
        else if(method==(modern?"CancelTask":"tasks/cancel"))result=modern?Json{{"id","remote-task"},{"status",{{"state","TASK_STATE_CANCELED"}}}}:Json{{"kind","task"},{"id","remote-task"},{"status",{{"state","canceled"}}}};
        else if(method==(modern?"SendStreamingMessage":"message/stream")||method==(modern?"SubscribeToTask":"tasks/resubscribe")){
            const bool mismatch=method=="SendStreamingMessage"&&body["params"].value("test_mismatch",false);const auto stream_id=mismatch?"wrong-id":body.at("id").get<std::string>();const auto version_value=modern?"1.0":"0.3";
            const Json first=modern?Json{{"task",{{"id","remote-task"},{"status",{{"state","TASK_STATE_WORKING"}}}}}}:Json{{"kind","task"},{"id","remote-task"},{"status",{{"state","working"}}}};
            const Json last=modern?Json{{"statusUpdate",{{"taskId","remote-task"},{"status",{{"state","TASK_STATE_COMPLETED"}}},{"final",true}}}}:Json{{"kind","status-update"},{"taskId","remote-task"},{"status",{{"state","completed"}}},{"final",true}};
            const auto envelope=[&](const Json& event){return Json{{"jsonrpc","2.0"},{"id",stream_id},{"result",event}}.dump();};response.set_content((method==(modern?"SubscribeToTask":"tasks/resubscribe")?"id: peer-event-2\r\ndata: ":"id: peer-event-1\r\ndata: ")+envelope(first)+"\r\n\r\ndata: "+envelope(last)+"\n\n","text/event-stream");response.set_header("A2A-Version",version_value);return;
        }
        else {response.set_content(Json{{"jsonrpc","2.0"},{"id",body.at("id")},{"error",{{"code",-32601},{"message","private peer text must not escape"}}}}.dump(),"application/json");return;}
        response.set_content(Json{{"jsonrpc","2.0"},{"id",body.at("id")},{"result",std::move(result)}}.dump(),"application/json");
    });
    port=peer.bind_to_any_port("127.0.0.1");require(port>0,"Could not bind independent loopback peer");std::thread owner([&]{peer.listen_after_bind();});
    try{
        const auto card="http://127.0.0.1:"+std::to_string(port)+"/.well-known/agent-card.json";
        agentflow::A2aJsonRpcClient denied(card);bool rejected=false;try{(void)denied.discover(2s);}catch(const agentflow::TransportError&){rejected=true;}require(rejected,"A card must not redirect the configured peer to another origin");require(!denied.ready(),"Rejected peer metadata cannot make the client ready");
        accept_remote=true;agentflow::A2aJsonRpcClient client(card);const auto info=client.discover(2s);require(info.name=="Peer"&&info.agent_version=="0.1"&&info.protocol_version=="1.0"&&info.endpoint=="http://127.0.0.1:"+std::to_string(port)+"/a2a"&&info.streaming,"Native discovery must select the configured-origin JSON-RPC interface and its advertised stream capability");agentflow::A2aJsonRpcClient direct_card("http://127.0.0.1:"+std::to_string(port)+"/custom/card.json");require(direct_card.discover(2s).endpoint==info.endpoint,"An explicitly configured AgentCard path must be used directly");
        const auto sent=Json::parse(client.send_message(R"({"message":{"messageId":"stable-1","role":"ROLE_USER","parts":[{"text":"hello"}]}})",2s));require(sent.at("task").at("id")=="remote-task","SendMessage task result was not retained");
        const auto observed=Json::parse(client.get_task(R"({"id":"remote-task","includeArtifacts":true})",2s));require(observed.at("status").at("state")=="TASK_STATE_WORKING"&&saw_include_artifacts.load(),"GetTask must pass through the explicit A2A includeArtifacts request");
        const auto cancelled=Json::parse(client.cancel_task(R"({"id":"remote-task"})",2s));require(cancelled.at("status").at("state")=="TASK_STATE_CANCELED","CancelTask response was not returned");
        std::vector<agentflow::A2aStreamEvent> stream_events;client.stream_message(R"({"message":{"messageId":"stable-stream","role":"ROLE_USER","parts":[{"text":"stream hello"}]}})",2s,[&](const agentflow::A2aStreamEvent& event){stream_events.push_back(event);});require(stream_events.size()==2&&stream_events.front().event_id=="peer-event-1"&&stream_events.back().event_id.empty()&&Json::parse(stream_events.front().result_json).at("task").at("id")=="remote-task"&&Json::parse(stream_events.back().result_json).at("statusUpdate").at("final")==true,"A2A must associate cursors only with frames that explicitly carry an SSE id");std::vector<agentflow::A2aStreamEvent> resumed_events;client.resubscribe_to_task(R"({"id":"remote-task"})",2s,[&](const agentflow::A2aStreamEvent& event){resumed_events.push_back(event);},"prior-peer-event");require(resumed_events.size()==2&&resumed_events.front().event_id=="peer-event-2"&&resumed_events.back().event_id.empty()&&Json::parse(resumed_events.back().result_json).at("statusUpdate").at("status").at("state")=="TASK_STATE_COMPLETED","SubscribeToTask must not treat an inherited cursor as a new event identity");bool mismatched=false;try{client.stream_message(R"({"test_mismatch":true})",2s,[](const std::string&){});}catch(const agentflow::TransportError&){mismatched=true;}require(mismatched,"A streamed JSON-RPC response for another request id must be rejected");
        bool safe_error=false;try{(void)client.get_task(R"({"id":"missing"})",2s);}catch(const agentflow::A2aRemoteError& error){safe_error=error.code==-32601&&std::string(error.what()).find("private")==std::string::npos;}require(safe_error,"Remote JSON-RPC errors must retain the code and redact peer prose");
        {std::lock_guard lock(calls_mutex);require(calls.size()==7,"Unexpected outbound call count");for(const auto& call:calls)require(call.first=="1.0","A2A version header was not pinned on every native request");require(calls[0].second=="SendMessage"&&calls[1].second=="GetTask"&&calls[2].second=="CancelTask"&&calls[3].second=="SendStreamingMessage"&&calls[4].second=="SubscribeToTask"&&calls[5].second=="SendStreamingMessage"&&calls[6].second=="GetTask","Version-1 methods were not selected");}
        streaming_supported=false;agentflow::A2aJsonRpcClient nonstreaming_peer(card);require(!nonstreaming_peer.discover(2s).streaming,"AgentCard without streaming support must stay non-streaming");bool capability_refused=false;try{nonstreaming_peer.stream_message(R"({"message":{"messageId":"must-not-send","role":"ROLE_USER","parts":[{"text":"do not send"}]}})",2s,[](const std::string&){});}catch(const std::logic_error&){capability_refused=true;}require(capability_refused,"The outbound client must reject streaming before dispatch when capability is absent");streaming_supported=true;
        {std::lock_guard lock(calls_mutex);calls.clear();authorizations.clear();}
        const std::string unadvertised="fixture-bearer-for-a-public-peer";agentflow::A2aJsonRpcClient public_peer(card,agentflow::SecretBytes({reinterpret_cast<const std::uint8_t*>(unadvertised.data()),unadvertised.size()}));public_peer.discover(2s);(void)public_peer.get_task(R"({"id":"remote-task"})",2s);{std::lock_guard lock(calls_mutex);require(calls.size()==1&&authorizations.size()==1&&authorizations.front().empty(),"A configured credential must not be sent when the AgentCard does not advertise bearer authentication");calls.clear();authorizations.clear();}
        unsupported_auth=true;agentflow::A2aJsonRpcClient unsupported_auth_peer(card);bool auth_refused=false;try{(void)unsupported_auth_peer.discover(2s);}catch(const agentflow::TransportError&){auth_refused=true;}unsupported_auth=false;require(auth_refused&&!unsupported_auth_peer.ready(),"Unsupported AgentCard security requirements must be rejected before task dispatch");
        {std::lock_guard lock(calls_mutex);calls.clear();}legacy_only=true;bad_request=true;const auto before_bad_request=discovery_count.load();agentflow::A2aJsonRpcClient refused(card);bool no_downgrade=false;try{(void)refused.discover(2s);}catch(const agentflow::ProviderHttpError& error){no_downgrade=error.status==400&&!error.a2a_version_not_supported;}require(no_downgrade&&discovery_count.load()==before_bad_request+1,"A generic problem response must not trigger an A2A protocol downgrade");bad_request=false;legacy_unavailable=true;const auto before_unsupported=discovery_count.load();agentflow::A2aJsonRpcClient unsupported(card);bool no_legacy=false;try{(void)unsupported.discover(2s);}catch(const agentflow::ProviderHttpError& error){no_legacy=error.status==400&&error.a2a_version_not_supported&&!error.a2a_supports_legacy_03;}require(no_legacy&&discovery_count.load()==before_unsupported+1,"A version error without legacy support must not trigger downgrade");legacy_unavailable=false;agentflow::A2aJsonRpcClient legacy(card);const auto old=legacy.discover(2s);require(old.protocol_version=="0.3"&&old.endpoint=="http://127.0.0.1:"+std::to_string(port)+"/a2a","Legacy AgentCard fallback was not negotiated");
        const auto old_sent=Json::parse(legacy.send_message(R"({"message":{"messageId":"stable-legacy","role":"user","parts":[{"kind":"text","text":"hello"}]}})",2s));require(old_sent.at("kind")=="task","Legacy send result was not returned");
        (void)legacy.get_task(R"({"id":"remote-task"})",2s);(void)legacy.cancel_task(R"({"id":"remote-task"})",2s);std::vector<Json> legacy_events;legacy.stream_message(R"({"message":{"messageId":"stable-legacy-stream","role":"user","parts":[{"kind":"text","text":"hello"}]}})",2s,[&](const std::string& event){legacy_events.push_back(Json::parse(event));});legacy.resubscribe_to_task(R"({"id":"remote-task"})",2s,[&](const std::string& event){legacy_events.push_back(Json::parse(event));});require(legacy_events.size()==4,"Legacy A2A streaming methods must deliver their server-sent task events");
        {std::lock_guard lock(calls_mutex);require(calls.size()==5,"Unexpected legacy outbound call count");require(calls[0]==std::pair<std::string,std::string>{"0.3","message/send"}&&calls[1]==std::pair<std::string,std::string>{"0.3","tasks/get"}&&calls[2]==std::pair<std::string,std::string>{"0.3","tasks/cancel"}&&calls[3]==std::pair<std::string,std::string>{"0.3","message/stream"}&&calls[4]==std::pair<std::string,std::string>{"0.3","tasks/resubscribe"},"Legacy A2A wire was not selected");}
        require(discovery_count.load()==10,"Unexpected card discovery count");require(json_rpc_media_valid.load(),"A2A JSON-RPC requests must use the pinned binding's JSON content type");
    }catch(const std::exception& error){std::cerr<<"Native A2A client contract failed: "<<error.what()<<"\n";peer.stop();owner.join();return 1;}
    peer.stop();owner.join();
    std::cout<<"Native A2A client passed origin-bound AgentCard discovery, version negotiation, v1 and 0.3 JSON-RPC send/get/cancel/stream/resubscribe, SSE event bounds, response correlation and remote-error redaction. Integration, durable streamed-result recovery, input-required and non-loopback TLS peer acceptance remain open.\n";
}
