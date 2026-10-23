#pragma once
#include "agentflow/agent_worker_client_context.hpp"
#include "agentflow/agent_worker_file_host.hpp"
namespace agentflow {
class AgentWorkerFileClient final:public AgentFileToolHost {
public:
    explicit AgentWorkerFileClient(AgentWorkerClientContext& peer):peer_(peer){}
    AgentFileToolResult invoke(const AgentFileToolInvocation&,std::stop_token={})override;
private:AgentWorkerClientContext& peer_;
};
// Only the durable backend host can bind this endpoint. A local file host or
// worker-selected lease cannot bypass per-call and per-operation admission.
AgentWorkerRpcHandler bind_agent_worker_file_rpc(AgentWorkerFileHost&,const AgentWorkerScope&);
AgentWorkerRpcHandler bind_agent_worker_file_rpc(AgentFileToolHost&,const AgentWorkerLeaseSpec&,const AgentWorkerScope&);
}
