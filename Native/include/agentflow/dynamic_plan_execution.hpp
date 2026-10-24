#pragma once
#include "agentflow/dynamic_plan_records.hpp"
#include "agentflow/model_provider.hpp"
#include "agentflow/instruction_precondition.hpp"
#include <functional>
#include <memory>
#include <stop_token>

namespace agentflow {
struct AgentSettings;
class RootExecutionBudget;
class NativeChildSlot;
struct DynamicExecutionResult {
    Run root;
    std::string call_id,result_json;
    bool paused=false;
};
// Backend-only child dispatch; return only after exact physical drain.
using DynamicAgentExecution=std::function<Run(const AgentSettings&,const std::string&,std::shared_ptr<RootExecutionBudget>,std::shared_ptr<NativeChildSlot>,bool,std::stop_token)>;
using DynamicInstructionPrecondition = std::function<InstructionPrecondition(const std::string&,std::stop_token)>;
// Trusted execution port used by the agent loop. The local implementation owns
// durable plan scheduling; a worker adapter must resolve the same backend owner.
// Frozen settings, capabilities, budgets and guidance are native capabilities,
// never client-selected wire data or authority supplied by a worker.
class DynamicPlanExecution {
public:
    virtual ~DynamicPlanExecution()=default;
    virtual std::string pending(const std::string&,std::stop_token={}){return {};}
    virtual DynamicExecutionResult invoke(const std::string& parent,const ModelToolCall& call,
        const std::string& actual_assistant_json,const std::string& originating_model_attempt,
        const AgentSettings& frozen,const DynamicPlanCapabilities& capabilities,
        std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel={},DynamicInstructionPrecondition guidance={})=0;
    virtual DynamicExecutionResult resume(const std::string& parent,const std::string& accepted_call_id,
        const AgentSettings& frozen,const DynamicPlanCapabilities& capabilities,
        std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel={},DynamicInstructionPrecondition guidance={})=0;
    virtual bool healthy() const=0;
};
}
