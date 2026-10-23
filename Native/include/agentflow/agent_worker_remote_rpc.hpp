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

}
