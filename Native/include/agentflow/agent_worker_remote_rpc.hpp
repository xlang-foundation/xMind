#pragma once
#include "agentflow/agent_worker_client_context.hpp"
#include "agentflow/agent_worker_remote_host.hpp"
namespace agentflow {
class AgentWorkerRemoteClient final:public AgentRemoteToolHost {
public:
    explicit AgentWorkerRemoteClient(AgentWorkerClientContext& peer):peer_(peer){}
    AgentRemoteToolResult invoke(const AgentRemoteToolInvocation&,std::stop_token={})override;
private:AgentWorkerClientContext& peer_;
};
// Only the durable backend host can bind this endpoint. A local remote host or
// worker-selected lease cannot bypass per-call and per-operation admission.
AgentWorkerRpcHandler bind_agent_worker_remote_rpc(AgentWorkerRemoteHost&,const AgentWorkerScope&);
AgentWorkerRpcHandler bind_agent_worker_remote_rpc(AgentRemoteToolHost&,const AgentWorkerLeaseSpec&,const AgentWorkerScope&);
}
