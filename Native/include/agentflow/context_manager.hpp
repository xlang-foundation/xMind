#pragma once
#include "agentflow/context_selection.hpp"
#include "agentflow/model_provider.hpp"
#include "agentflow/root_execution_budget.hpp"
#include <functional>
#include <map>

namespace agentflow {
std::string context_route_identity(const ChatProviderConfig&,const std::string& provider_identity_json);
struct ContextRuntimePolicy {
    ContextPolicy compaction;
    std::int64_t buffer_tokens=8000;
    // Registered by the backend for the exact selected route/model. Account
    // model-list IDs never imply a capacity, tokenizer or compaction grant.
    std::map<std::string,VerifiedContextCapacity> model_capacities;
};
struct ContextOwner {
    ContextScope scope;
    ContextBinding binding;
    std::string root_run_id,owner_run_id;
    ModelCallRole role=ModelCallRole::parent;
    std::optional<std::string> planning_call_id;
};
struct PreparedModelContext {
    ModelRequest request;
    ModelCallReservation inference;
    std::string inference_step_id;
    ContextSnapshot snapshot;
    std::optional<ContextInputMeasure> measure;
};
// Native execution component. It loads indexed original groups through the
// repository, calls actual provider adapters, and journals private outcomes.
// Current trusted instructions/tools are supplied independently of history.
class ContextManager {
public:
    using MessageDecoder=std::function<ModelMessage(const Message&)>;
    ContextManager(PersistenceService&,ChatProviderConfig,ContextRuntimePolicy,MessageDecoder);
    PreparedModelContext prepare(const ContextOwner&,const ModelRequest& trusted_current,
        RootExecutionBudget&,const SecretBytes* bearer,std::stop_token cancel={});
    PreparedModelContext rebuild(const ContextOwner&,const ModelRequest& trusted_current,
        const PreparedModelContext& failed,RootExecutionBudget&,const SecretBytes* bearer,
        std::stop_token cancel={});
    ContextProjection compact_idle(const ContextScope&,const ContextBinding&,
        const std::string& owner_id,const ModelRequest& trusted_current,
        const SecretBytes* bearer,std::stop_token cancel={},
        std::optional<std::chrono::steady_clock::time_point> owner_deadline={});
private:
    PersistenceService& store_;
    ChatProviderConfig provider_;
    ContextRuntimePolicy policy_;
    MessageDecoder decode_;
};
}
