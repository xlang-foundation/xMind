#pragma once
#include "agentflow/agent_worker_read_host.hpp"
#include "agentflow/context_records.hpp"
#include "agentflow/model_provider.hpp"
#include <mutex>

namespace agentflow {
class RepositoryInstructionContext;
struct AgentWorkerReadRequestPolicy {
    std::string instructions;
    // An empty catalogue captures an agent's explicit tools: [] policy. It
    // must remain empty; it never selects the backend's default capabilities.
    std::vector<ModelToolDefinition> tools;
    bool include_usage=false;
    std::optional<std::int64_t> max_output_tokens;
    std::string instruction_suffix;
    // Backend-only canonical session history owner and captured provider route.
    std::optional<ProviderHistoryOwner> history_owner;
    std::optional<ChatProviderConfig> history_provider;
};
// Backend-owned ordinary read context. Preparing a request works on copies of
// real native guidance state. Only admission of a claimed, exact model request
// updates delivered state; a preview never makes a scoped read ready.
class AgentWorkerReadContext final:public AgentReadToolHost {
public:
    AgentWorkerReadContext(AgentRunJournal&,PersistenceService&,AgentWorkerLeaseSpec,
        AgentReadToolOwnerContext,AgentWorkerReadRequestPolicy,std::stop_token owner_cancel={});
    ~AgentWorkerReadContext();
    ModelRequest prepare(std::stop_token={});
    void admit(const std::string& attempt,const ModelRequest&,std::stop_token={},const ModelRequest* full_window=nullptr);
    AgentReadToolResult invoke(const AgentWorkspaceInvocation&,std::stop_token={})override;
    const AgentWorkerLeaseSpec& lease()const noexcept{return lease_;}
private:
    friend class AgentWorkerCodingContext;
    struct Preview;
    Preview preview(std::stop_token);
    void check(std::stop_token);
    PersistenceService& store_;
    AgentWorkerLeaseSpec lease_;
    AgentReadToolOwnerContext owner_;
    AgentWorkerReadRequestPolicy policy_;
    std::stop_token owner_cancel_;
    std::vector<std::unique_ptr<RepositoryInstructionContext>> contexts_;
    std::unique_ptr<AgentWorkerReadHost> native_;
    std::string admitted_attempt_;
    std::mutex mutex_;
};
}
