#pragma once
#include "agentflow/agent_worker_rpc.hpp"
#include "agentflow/agent_worker_callable_host.hpp"
namespace agentflow {
class AgentWorkerCallableClient final:public AgentCallableToolHost {
public:
    explicit AgentWorkerCallableClient(AgentWorkerRpcPeer& peer):peer_(peer){}
    AgentCallableToolResult invoke(const AgentCallableInvocation&,std::stop_token={})override;
private:AgentWorkerRpcPeer& peer_;
};
// Only the durable backend host can bind this endpoint. A local callable host or
// worker-selected lease cannot bypass per-call and per-operation admission.
AgentWorkerRpcHandler bind_agent_worker_callable_rpc(AgentWorkerCallableHost&,const AgentWorkerScope&);
AgentWorkerRpcHandler bind_agent_worker_callable_rpc(AgentWorkerCallableService&,const AgentWorkerLeaseSpec&,const AgentWorkerScope&);
}
