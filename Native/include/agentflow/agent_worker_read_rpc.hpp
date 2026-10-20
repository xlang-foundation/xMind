#pragma once
#include "agentflow/agent_worker_rpc.hpp"
#include "agentflow/agent_worker_read_host.hpp"
namespace agentflow {
class AgentWorkerReadClient final:public AgentReadToolHost {
public:
    explicit AgentWorkerReadClient(AgentWorkerRpcPeer& peer):peer_(peer){}
    AgentReadToolResult invoke(const AgentWorkspaceInvocation&,std::stop_token={})override;
private:AgentWorkerRpcPeer& peer_;
};
AgentWorkerRpcHandler bind_agent_worker_read_rpc(AgentWorkerReadHost&,const AgentWorkerScope&);
}
