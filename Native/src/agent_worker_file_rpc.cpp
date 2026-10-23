#include "agentflow/agent_worker_file_rpc.hpp"
namespace agentflow {
AgentFileToolResult AgentWorkerFileClient::invoke(const AgentFileToolInvocation& call,std::stop_token cancel){return peer_.services().file_invoke(call,cancel);}
}
