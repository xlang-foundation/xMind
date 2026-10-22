#pragma once
#include "agentflow/agent_worker_rpc.hpp"
#include "agentflow/records.hpp"

namespace agentflow {
// A child reply is an intent only. The backend must separately observe physical
// exit and canonical durable facts before retiring ownership or freeing capacity.
std::shared_ptr<NativeExecutionClient> connect_agent_worker_execution(AgentWorkerProcess&,AgentWorkerChannel::Deadline,std::stop_token={});
void connect_agent_worker_execution(AgentWorkerRpcPeer&,AgentWorkerProcess&,AgentWorkerChannel::Deadline,std::stop_token={});
RunState request_agent_worker_execution(AgentWorkerRpcPeer&);
void request_agent_worker_cancellation(AgentWorkerRpcPeer&);
}
