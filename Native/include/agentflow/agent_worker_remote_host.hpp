#pragma once
#include "agentflow/agent_remote_tool_host.hpp"
#include "agentflow/agent_worker_records.hpp"
#include <mutex>
namespace agentflow {
struct AgentWorkerRemoteOutcomeUnrecorded:std::runtime_error {using std::runtime_error::runtime_error;};
// Backend-only durable owner; peer registries and authority never cross IPC.
class AgentWorkerRemoteHost final:public AgentRemoteToolHost {
public:
    AgentWorkerRemoteHost(PersistenceService&,AgentWorkerLeaseSpec,AgentRemoteToolOwnerContext,
        std::chrono::steady_clock::time_point,std::stop_token owner_cancel={});
    AgentRemoteToolResult invoke(const AgentRemoteToolInvocation&,std::stop_token={})override;
    const AgentWorkerLeaseSpec& lease()const noexcept{return lease_;}
private:
    PersistenceService& store_;AgentWorkerLeaseSpec lease_;
    std::chrono::steady_clock::time_point deadline_;std::stop_token owner_cancel_;
    std::unique_ptr<AgentRemoteToolHost> native_;std::mutex invocation_;
};
}
