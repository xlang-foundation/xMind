#include "agentflow/agent_worker_read_rpc.hpp"
namespace agentflow {
AgentReadToolResult AgentWorkerReadClient::invoke(const AgentWorkspaceInvocation& call,std::stop_token cancel){return peer_.services().read_invoke(call,cancel);}
}
