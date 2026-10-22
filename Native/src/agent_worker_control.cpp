#include "agentflow/agent_worker_control.hpp"
#include "agentflow/agent_worker_native_execution.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"

namespace agentflow {
using Json=nlohmann::json;
std::shared_ptr<NativeExecutionClient> connect_agent_worker_execution(AgentWorkerProcess& worker,AgentWorkerChannel::Deadline deadline,std::stop_token stop){
    const auto ready=worker.channel().receive(deadline,stop);if(ready.operation!="worker.execution.ready"||ready.payload.size()>1024)throw AgentWorkerProtocolError("Invalid native execution rendezvous");
    const auto value=Json::parse(mcp_compact_object(ready.payload));if(value.size()!=4||!value.at("version").is_number_integer()||value.at("version")!=1||!value.at("endpoint").is_string()||!value.at("ticket").is_string()||!value.at("pid").is_number_unsigned()||value.at("pid").get<std::uint64_t>()!=worker.pid())throw AgentWorkerProtocolError("Native execution rendezvous identity differs");
    auto client=std::make_shared<NativeExecutionClient>(NativeExecutionAccess{value.at("endpoint").get<std::string>(),value.at("ticket").get<std::string>(),worker.pid()},worker.channel().scope());if(stop.stop_requested())throw AgentWorkerChannelCancelled("Native execution rendezvous cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerChannelTimeout("Native execution rendezvous expired");return client;
}
}
