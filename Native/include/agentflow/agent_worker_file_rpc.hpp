#pragma once
#include "agentflow/agent_worker_rpc.hpp"
#include "agentflow/agent_worker_file_host.hpp"
namespace agentflow {
class AgentWorkerFileClient final:public AgentFileToolHost {
public:
    explicit AgentWorkerFileClient(AgentWorkerRpcPeer& peer):peer_(peer){}
    AgentFileToolResult invoke(const AgentFileToolInvocation&,std::stop_token={})override;
private:AgentWorkerRpcPeer& peer_;
};
// Only the durable backend host can bind this endpoint. A local file host or
// worker-selected lease cannot bypass per-call and per-operation admission.
AgentWorkerRpcHandler bind_agent_worker_file_rpc(AgentWorkerFileHost&,const AgentWorkerScope&);
AgentWorkerRpcHandler bind_agent_worker_file_rpc(AgentFileToolHost&,const AgentWorkerLeaseSpec&,const AgentWorkerScope&);
}
