#pragma once
#include "agentflow/agent_file_tool_host.hpp"
#include "agentflow/agent_worker_records.hpp"
#include <mutex>

namespace agentflow {
struct AgentWorkerFileOutcomeUnrecorded:std::runtime_error {using std::runtime_error::runtime_error;};
// Backend-owned native file effects. Construction retains actual workspace and
// guidance capabilities; invocation and approved operation claims stay durable.
// This port does not enable a worker RPC endpoint or grant dispatch by itself.
class AgentWorkerFileHost final:public AgentFileToolHost {
public:
    AgentWorkerFileHost(PersistenceService&,AgentWorkerLeaseSpec,AgentFileToolOwnerContext,
        std::chrono::steady_clock::time_point,std::stop_token owner_cancel={});
    AgentFileToolResult invoke(const AgentFileToolInvocation&,std::stop_token={})override;
    const AgentWorkerLeaseSpec& lease()const noexcept{return lease_;}
private:
    PersistenceService& store_;
    AgentWorkerLeaseSpec lease_;
    std::chrono::steady_clock::time_point deadline_;
    std::stop_token owner_cancel_;
    std::unique_ptr<AgentFileToolHost> native_;
    std::mutex invocation_;
};
}
