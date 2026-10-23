#include "agentflow/agent_worker_remote_rpc.hpp"
namespace agentflow {
AgentRemoteToolResult AgentWorkerRemoteClient::invoke(const AgentRemoteToolInvocation& call,std::stop_token cancel){return peer_.services().remote_invoke(call,cancel);}
}
