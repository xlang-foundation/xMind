#pragma once
#include "agentflow/agent_process_tool_host.hpp"
#include "agentflow/agent_worker_records.hpp"
#include <mutex>
namespace agentflow {
// Durable foreground command ownership. Native executable, approval and guidance
// capabilities remain in the backend; this host does not expose an RPC endpoint.
class AgentWorkerProcessHost final:public AgentProcessOwnerHost {
public:
    AgentWorkerProcessHost(PersistenceService&,AgentWorkerLeaseSpec,AgentProcessToolOwnerContext,
        std::chrono::steady_clock::time_point,std::stop_token owner_cancel={});
    AgentProcessToolResult invoke(const AgentProcessToolInvocation&,std::stop_token={})override;
    const AgentWorkerLeaseSpec& lease()const noexcept{return lease_;}
private:
    PersistenceService& store_;AgentWorkerLeaseSpec lease_;
    std::chrono::steady_clock::time_point deadline_;std::stop_token owner_cancel_;
    std::unique_ptr<AgentProcessOwnerHost> native_;std::mutex invocation_;
};
}
