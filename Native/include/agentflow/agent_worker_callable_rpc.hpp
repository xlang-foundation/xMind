#pragma once
#include "agentflow/agent_worker_client_context.hpp"
#include "agentflow/agent_worker_callable_host.hpp"
namespace agentflow {
class AgentWorkerCallableClient final:public AgentCallableToolHost {
public:
    explicit AgentWorkerCallableClient(AgentWorkerClientContext& peer):peer_(peer){}
    AgentCallableToolResult invoke(const AgentCallableInvocation&,std::stop_token={})override;
private:AgentWorkerClientContext& peer_;
};

}
