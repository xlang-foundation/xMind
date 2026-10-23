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

}
