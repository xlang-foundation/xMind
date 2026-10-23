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

}
