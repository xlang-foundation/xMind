#pragma once
#include "agentflow/agent_worker_channel.hpp"
#include "agentflow/records.hpp"

namespace agentflow {
class NativeExecutionClient;
// A child reply is an intent only. The backend must separately observe physical
// exit and canonical durable facts before retiring ownership or freeing capacity.
std::shared_ptr<NativeExecutionClient> connect_agent_worker_execution(AgentWorkerProcess&,AgentWorkerChannel::Deadline,std::stop_token={});
}
