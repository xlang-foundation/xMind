#pragma once
#include "agentflow/agent_worker_client_context.hpp"
#include "agentflow/agent_worker_read_host.hpp"
namespace agentflow {
class AgentWorkerReadClient final:public AgentReadToolHost {
public:
    explicit AgentWorkerReadClient(AgentWorkerClientContext& peer):peer_(peer){}
    AgentReadToolResult invoke(const AgentWorkspaceInvocation&,std::stop_token={})override;
private:AgentWorkerClientContext& peer_;
};
AgentWorkerRpcHandler bind_agent_worker_read_rpc(AgentWorkerReadHost&,const AgentWorkerScope&);
AgentWorkerRpcHandler bind_agent_worker_read_rpc(AgentReadToolHost&,const AgentWorkerLeaseSpec&,const AgentWorkerScope&);
}
