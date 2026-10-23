#include "agentflow/agent_worker_callable_rpc.hpp"
namespace agentflow {
AgentCallableToolResult AgentWorkerCallableClient::invoke(const AgentCallableInvocation& call,std::stop_token cancel){return peer_.services().callable_invoke(call,cancel);}
}
