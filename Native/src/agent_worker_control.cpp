#include "agentflow/agent_worker_control.hpp"
#include "agentflow/agent_worker_native_execution.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
[[noreturn]] void invalid(){throw AgentWorkerProtocolError("Invalid worker control reply");}
Json reply(const std::string& bytes){
    if(bytes.empty()||bytes.size()>256)invalid();
    try{
        std::set<std::string> keys;
        auto value=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& item){
            if(depth>1)invalid();
            if(event==Json::parse_event_t::key&&!keys.insert(item.get<std::string>()).second)invalid();
            return true;
        });
        if(!value.is_object()||value.size()!=2||!value.contains("version")||!value.at("version").is_number_integer()||value.at("version")!=1)invalid();
        return value;
    }catch(const Json::exception&){invalid();}
}
}
RunState decode_agent_worker_execution_intent(const std::string& bytes){
    const auto value=reply(bytes);
    if(!value.contains("outcome")||!value.at("outcome").is_string())invalid();
    const auto outcome=value.at("outcome").get<std::string>();
    if(outcome=="completed")return RunState::completed;
    if(outcome=="failed")return RunState::failed;
    if(outcome=="cancelled")return RunState::cancelled;
    invalid();
}
void connect_agent_worker_execution(AgentWorkerRpcPeer& peer,AgentWorkerProcess& worker,AgentWorkerChannel::Deadline deadline,std::stop_token stop){
    if(peer.scope()!=worker.channel().scope())throw AgentWorkerProtocolError("Native execution rendezvous scope differs");const auto ready=worker.channel().receive(deadline,stop);if(ready.operation!="worker.execution.ready"||ready.payload.size()>1024)throw AgentWorkerProtocolError("Invalid native execution rendezvous");
    const auto value=Json::parse(mcp_compact_object(ready.payload));if(value.size()!=4||!value.at("version").is_number_integer()||value.at("version")!=1||!value.at("endpoint").is_string()||!value.at("ticket").is_string()||!value.at("pid").is_number_unsigned()||value.at("pid").get<std::uint64_t>()!=worker.pid())throw AgentWorkerProtocolError("Native execution rendezvous identity differs");
    auto client=std::make_shared<NativeExecutionClient>(NativeExecutionAccess{value.at("endpoint").get<std::string>(),value.at("ticket").get<std::string>(),worker.pid()},peer.scope());if(stop.stop_requested())throw AgentWorkerChannelCancelled("Native execution rendezvous cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerChannelTimeout("Native execution rendezvous expired");peer.bind_native_execution(std::move(client));
}
RunState request_agent_worker_execution(AgentWorkerRpcPeer& peer){
    if(auto native=peer.native_execution())return native->execute(peer.execution_deadline());
    throw AgentWorkerRpcClosed("Native execution control is not bound");
}
void request_agent_worker_cancellation(AgentWorkerRpcPeer& peer){
    if(auto native=peer.native_execution()){native->cancel();return;}
    throw AgentWorkerRpcClosed("Native execution control is not bound");
}
}
