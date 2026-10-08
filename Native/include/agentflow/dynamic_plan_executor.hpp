#pragma once
#include "agentflow/agent_runner.hpp"
#include "agentflow/dynamic_plan.hpp"
#include "agentflow/native_child_executor.hpp"

namespace agentflow {
// Shared by capability finalization and actual child construction. Preset
// identity is backend-owned; no model field can widen these native settings.
AgentSettings dynamic_child_settings(const AgentSettings& parent,
    const DynamicPresetCapability& preset,const std::string& selected_model);
struct DynamicExecutionResult {
    Run root;
    std::string call_id,result_json;
    bool paused=false;
};
// Executes only durable dependency-ready agent/human frontiers under the
// ordinary root's shared budget. Provider continuation belongs to AgentRunner.
class DynamicPlanExecutor {
public:
    DynamicPlanExecutor(PersistenceService& store,std::shared_ptr<NativeChildExecutor> children);
    DynamicExecutionResult invoke(const std::string& parent,const ModelToolCall& call,
        const std::string& actual_assistant_json,const std::string& originating_model_attempt,
        const AgentSettings& frozen,const DynamicPlanCapabilities& capabilities,
        std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel={});
    DynamicExecutionResult resume(const std::string& parent,const std::string& accepted_call_id,
        const AgentSettings& frozen,const DynamicPlanCapabilities& capabilities,
        std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel={});
    bool healthy() const;
private:
    DynamicExecutionResult execute(const DynamicPlanCallRecord& accepted,const AgentSettings& frozen,
        const DynamicPlanCapabilities& capabilities,std::shared_ptr<RootExecutionBudget> budget,
        std::stop_token cancel);
    PersistenceService& store_;
    std::shared_ptr<NativeChildExecutor> children_;
};
}
