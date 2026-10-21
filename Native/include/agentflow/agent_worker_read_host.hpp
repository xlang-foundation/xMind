#pragma once
#include "agentflow/agent_read_tool_host.hpp"
#include "agentflow/agent_guidance_tool_host.hpp"
#include "agentflow/agent_worker_records.hpp"
#include <mutex>

namespace agentflow {
struct AgentWorkerReadOutcomeUnrecorded:std::runtime_error {using std::runtime_error::runtime_error;};
// Backend-only native read owner. The child supplies only the owned invocation;
// policy, guidance, real workspace handles and the full lease stay here.
class AgentWorkerReadHost final:public AgentReadToolHost {
public:
    AgentWorkerReadHost(AgentRunJournal&,PersistenceService&,AgentWorkerLeaseSpec,
        AgentReadToolOwnerContext,std::stop_token owner_cancel={});
    AgentReadToolResult invoke(const AgentWorkspaceInvocation&,std::stop_token={})override;
    const AgentWorkerLeaseSpec& lease()const noexcept{return lease_;}
private:
    PersistenceService& store_;
    AgentWorkerLeaseSpec lease_;
    AgentReadToolOwnerContext owner_;
    std::stop_token owner_cancel_;
    std::unique_ptr<AgentReadToolHost> native_;
    std::unique_ptr<AgentGuidanceToolHost> guidance_;
    std::mutex invocation_;
    std::stop_token active_cancel_;
};
}
