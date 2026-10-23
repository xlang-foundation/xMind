#pragma once
#include "agentflow/agent_worker_read_context.hpp"
#include "agentflow/agent_worker_channel.hpp"
#include "agentflow/agent_worker_model_host.hpp"
#include "agentflow/agent_worker_journal_host.hpp"

namespace agentflow {
struct NativeWorkerModelAccess;
// Backend-side lifetime owner for one captured ordinary worker. Retain this
// object, the workspace and persistence until the peer has drained and joined.
// Provider configuration and policy are copied once; the child cannot replace
// them or select a different lease. Process scheduling/retirement stays outside.
class AgentWorkerReadOwner final {
public:
    AgentWorkerReadOwner(PersistenceService&,AgentWorkerLeaseSpec,const AgentWorkerScope&,
        AgentReadToolOwnerContext,AgentWorkerReadRequestPolicy,ChatProviderConfig,
        std::string assistant_provider_context_json,std::shared_ptr<AgentModelGateway>,
        AgentWorkerChannel::Deadline,std::stop_token owner_cancel={},std::function<void(AgentWorkerJournalReadKind,std::stop_token)> journal_read={});
    ~AgentWorkerReadOwner();
    NativeWorkerModelAccess native_model_access()const;
    bool outcome_unrecorded()const noexcept;
    AgentWorkerReadOwner(const AgentWorkerReadOwner&)=delete;
    AgentWorkerReadOwner& operator=(const AgentWorkerReadOwner&)=delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
