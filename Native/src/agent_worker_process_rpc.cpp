#include "agentflow/agent_worker_process_rpc.hpp"
namespace agentflow {
AgentProcessToolResult AgentWorkerProcessClient::invoke(const AgentProcessToolInvocation& call,std::stop_token cancel){return peer_.services().process_invoke(call,cancel);}
}
