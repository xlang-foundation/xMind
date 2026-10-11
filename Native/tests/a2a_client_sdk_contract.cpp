#include "agentflow/a2a_client.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
agentflow::SecretBytes secret(const std::string& value){return agentflow::SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
}
int main(int argc,char** argv){
    try{
        require(argc==3,"Expected the independent SDK AgentCard URL and fixture bearer");
        const std::string card=argv[1],token=argv[2];
        agentflow::A2aJsonRpcClient unauthenticated(card);bool rejected=false;
        try{(void)unauthenticated.discover(10s);}catch(const agentflow::TransportError&){rejected=true;}
        require(rejected&&!unauthenticated.ready(),"A peer requiring bearer authorization accepted an unauthenticated discovery");
        agentflow::A2aJsonRpcClient client(card,secret(token));
        const auto remote=client.discover(10s);require(remote.protocol_version=="1.0"&&remote.name=="Pinned official A2A SDK peer","Native discovery did not parse the independently served SDK AgentCard");
        const auto sent=Json::parse(client.send_message(R"({"message":{"messageId":"xmind-sdk-peer-1","role":"ROLE_USER","parts":[{"text":"native client SDK acceptance"}]}})",10s));
        require(sent.contains("id")&&sent.at("id").is_string()&&!sent.at("id").get_ref<const std::string&>().empty(),"SDK peer did not create a task for SendMessage");
        const auto id=sent.at("id").get<std::string>();
        const auto fetched=Json::parse(client.get_task(Json{{"id",id}}.dump(),10s));require(fetched.at("id")==id&&fetched.contains("status"),"SDK peer task was not retrievable through GetTask");
        std::vector<Json> stream_events;client.stream_message(R"({"message":{"messageId":"xmind-sdk-stream-1","role":"ROLE_USER","parts":[{"text":"native stream against the official SDK peer"}]}})",10s,[&](const std::string& event){stream_events.push_back(Json::parse(event));});require(stream_events.size()>=2&&stream_events.front().contains("task")&&stream_events.back().contains("statusUpdate"),"Official A2A SDK peer did not deliver wrapped task/status StreamResponse events to the native client");
        const auto cancelled=Json::parse(client.cancel_task(Json{{"id",id}}.dump(),10s));require(cancelled.at("id")==id&&cancelled.contains("status"),"SDK peer task was not cancellable through CancelTask");
        std::cout<<"Native outbound A2A client passed bearer rejection, independent official SDK 1.3.0 AgentCard discovery, SendMessage, GetTask, wrapped SendStreamingMessage task/status events and CancelTask. Runtime registration, durable remote-effect recovery, resubscription and full protocol compliance remain open.\n";
    }catch(const std::exception& error){std::cerr<<"Native outbound A2A SDK peer contract failed: "<<error.what()<<"\n";return 1;}
    return 0;
}
