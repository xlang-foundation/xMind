#pragma once
#include "agentflow/agent_worker_read_owner.hpp"
#include "agentflow/agent_worker_retirement.hpp"
#include "agentflow/process_executor.hpp"

namespace agentflow {
// Backend lifetime owner for one actual product child. No restart or task replay.
// Keep this object, persistence and workspace alive until drain succeeds; join
// external execute/cancel callers before drain or destruction. Observation timeout
// retains this same process and lease for another observation.
class AgentWorkerSupervisor final {
public:
    AgentWorkerSupervisor(PersistenceService&,std::shared_ptr<const BoundProcessExecutable>,
        AgentWorkerScope,std::string provider_identity_json,AgentWorkerChannel::Deadline);
    ~AgentWorkerSupervisor();
    static std::unique_ptr<AgentWorkerSupervisor> prepare(PersistenceService&,std::shared_ptr<const BoundProcessExecutable>,
        AgentWorkerScope,std::string provider_identity_json,AgentWorkerChannel::Deadline);
    void launch();
    AgentWorkerSupervisor(const AgentWorkerSupervisor&)=delete;
    AgentWorkerSupervisor& operator=(const AgentWorkerSupervisor&)=delete;
    void start(AgentReadToolOwnerContext,AgentWorkerReadRequestPolicy,ChatProviderConfig,
        std::string assistant_provider_context_json,std::shared_ptr<AgentModelGateway>,std::stop_token={});
    void start_coding(AgentReadToolOwnerContext,AgentWorkerReadRequestPolicy,ChatProviderConfig,
        std::string assistant_provider_context_json,std::shared_ptr<AgentModelGateway>,std::stop_token={},std::shared_ptr<AgentProcessToolHost> process_owner={},std::optional<AgentRemoteToolOwnerContext> remote_owner={},std::optional<AgentCallableToolOwnerContext> callable_owner={});
    RunState execute();
    void cancel();
    AgentWorkerTerminalObservation drain_completed(AgentWorkerChannel::Deadline);
    AgentWorkerTerminalObservation drain_paused(const std::string& segment,AgentWorkerChannel::Deadline);
    AgentWorkerTerminalObservation drain_terminal(RunState,const std::string& reason,AgentWorkerChannel::Deadline);
    const AgentWorkerLeaseSpec& lease()const noexcept;
    bool alive()const;
    bool admitted()const;
    // Trusted backend handler observation, never a worker-selected terminal cause.
    bool outcome_unrecorded()const noexcept;
    // Exact physical observation only, including failed lease/startup recording.
    // Does not assert or write any durable terminal outcome.
    std::uint32_t drain_physical(AgentWorkerChannel::Deadline);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
    explicit AgentWorkerSupervisor(std::unique_ptr<Impl>);
};
}
