#pragma once
#include "agentflow/agent_worker_callable_service.hpp"
#include "agentflow/agent_worker_records.hpp"
#include <mutex>
namespace agentflow {
struct AgentWorkerCallableOutcomeUnrecorded:AgentCallableOutcomeUnrecorded {using AgentCallableOutcomeUnrecorded::AgentCallableOutcomeUnrecorded;};
// Backend-only durable owner; callable bindings and authority never cross IPC.
class AgentWorkerCallableHost final:public AgentWorkerCallableService {
public:
    AgentWorkerCallableHost(PersistenceService&,AgentWorkerLeaseSpec,AgentCallableToolOwnerContext,
        std::chrono::steady_clock::time_point,std::stop_token owner_cancel={});
    AgentCallableToolResult invoke(const AgentCallableInvocation&,std::stop_token={})override;
    std::vector<std::string> tool_names(std::stop_token={})override{return names_;}
    const AgentWorkerLeaseSpec& lease()const noexcept{return lease_;}
private:
    PersistenceService& store_;AgentWorkerLeaseSpec lease_;
    std::chrono::steady_clock::time_point deadline_;std::stop_token owner_cancel_;
    std::vector<std::string> names_;
    std::unique_ptr<AgentCallableToolHost> native_;std::mutex invocation_;
};
}
