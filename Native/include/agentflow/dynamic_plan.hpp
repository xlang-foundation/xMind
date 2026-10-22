#pragma once
#include "agentflow/dynamic_plan_records.hpp"
#include "agentflow/model_provider.hpp"

namespace agentflow {
// Canonical backend catalogue assembly shared by local and process owners.
// Preserves schema text and requires exact discovered approval bindings.
std::string seal_dynamic_tool_catalogue(const std::vector<ModelToolDefinition>&,
    const std::string& approval_bindings_json);
// Intersect the native offered catalogue with the fixed preset boundary. An
// empty result is a model-only child, never a request for default tools.
std::vector<std::string> dynamic_preset_tools(const DynamicPresetCapability&,const std::vector<ModelToolDefinition>&);
// These functions validate model data and reduce a repository-owned snapshot.
// They grant no execution authority and never claim, settle or replay a child.
DynamicPlanChange parse_dynamic_plan_change(const std::string& raw_json,bool initial);
DynamicPlanCandidate reduce_dynamic_plan(const DynamicPlanRecord& observed,
    const DynamicPlanChange& change,const DynamicPlanCapabilities& frozen);

struct DynamicPlanDecision {
    std::vector<std::string> ready,blocked,claimed,waiting_human;
    bool finished=false,report_ready=false,halted=false;
};
struct DynamicPreparedNode {
    DynamicNodeRecord node;
    // Exact observed JSON is carried as an explicitly labelled data string.
    // Dependency observations cannot change a preset or approve an effect.
    std::string dependency_outputs_json;
};
// Immutable view of a durable plan. Repository owns every actual transition.
class DynamicPlanCoordinator {
public:
    explicit DynamicPlanCoordinator(DynamicPlanRecord observed);
    const DynamicPlanRecord& record() const{return observed_;}
    DynamicPlanDecision inspect() const;
    DynamicPreparedNode prepare(const std::string& label) const;
    std::string report() const;
private:
    DynamicPlanRecord observed_;
};
DynamicPlanDecision inspect_dynamic_plan(const DynamicPlanRecord& observed);
DynamicPreparedNode prepare_dynamic_node(const DynamicPlanRecord& observed,const std::string& label);
std::string dynamic_plan_report(const DynamicPlanRecord& observed);
// Model-visible definitions expose registered agent/human tasks and exact
// schema-bound deterministic, MCP and synchronous process tool nodes. This
// function grants no authority; execution rechecks the frozen root bindings.
std::vector<ModelToolDefinition> dynamic_plan_tool_definitions(const DynamicPlanCapabilities& frozen);
// Exact registered schema for idle context measurement; this does not finalize
// a catalogue, create a plan or grant an execution capability.
std::vector<ModelToolDefinition> context_dynamic_plan_tool_definitions(const DynamicPlanCapabilities& policy);
}
