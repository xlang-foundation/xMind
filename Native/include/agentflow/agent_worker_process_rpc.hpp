#pragma once
#include "agentflow/agent_worker_client_context.hpp"
#include "agentflow/agent_worker_process_host.hpp"
namespace agentflow {
class AgentWorkerProcessClient final:public AgentProcessOwnerHost {
public:
    explicit AgentWorkerProcessClient(AgentWorkerClientContext& peer):peer_(peer){}
    AgentProcessToolResult invoke(const AgentProcessToolInvocation&,std::stop_token={})override;
private:AgentWorkerClientContext& peer_;
};
// Only the durable backend host can bind this endpoint. A local process host or
// worker-selected lease cannot bypass per-call and per-operation admission.
AgentWorkerRpcHandler bind_agent_worker_process_rpc(AgentWorkerProcessHost&,const AgentWorkerScope&);
AgentWorkerRpcHandler bind_agent_worker_process_rpc(AgentProcessOwnerHost&,const AgentWorkerLeaseSpec&,const AgentWorkerScope&);
}
