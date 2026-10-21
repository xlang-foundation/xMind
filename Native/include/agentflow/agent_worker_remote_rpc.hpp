#pragma once
#include "agentflow/agent_worker_rpc.hpp"
#include "agentflow/agent_worker_remote_host.hpp"
namespace agentflow {
class AgentWorkerRemoteClient final:public AgentRemoteToolHost {
public:
    explicit AgentWorkerRemoteClient(AgentWorkerRpcPeer& peer):peer_(peer){}
    AgentRemoteToolResult invoke(const AgentRemoteToolInvocation&,std::stop_token={})override;
private:AgentWorkerRpcPeer& peer_;
};
// Only the durable backend host can bind this endpoint. A local remote host or
// worker-selected lease cannot bypass per-call and per-operation admission.
AgentWorkerRpcHandler bind_agent_worker_remote_rpc(AgentWorkerRemoteHost&,const AgentWorkerScope&);
}
