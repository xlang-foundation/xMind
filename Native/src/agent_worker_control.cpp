#include "agentflow/agent_worker_control.hpp"
#include "agentflow/agent_worker_native_execution.hpp"

namespace agentflow {
std::shared_ptr<NativeExecutionClient> connect_agent_worker_execution(AgentWorkerProcess& worker,AgentWorkerChannel::Deadline deadline,std::stop_token stop){
    const auto access=worker.channel().receive_execution(deadline,stop);if(access.pid!=worker.pid())throw AgentWorkerProtocolError("Native execution rendezvous identity differs");
    auto client=std::make_shared<NativeExecutionClient>(NativeExecutionAccess{access.endpoint,access.ticket,worker.pid()},worker.channel().scope());if(stop.stop_requested())throw AgentWorkerChannelCancelled("Native execution rendezvous cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerChannelTimeout("Native execution rendezvous expired");return client;
}
}
