#pragma once
#include "agentflow/dynamic_plan_records.hpp"
#include "agentflow/model_provider.hpp"

namespace agentflow {
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
// Only agent/human tasks and backend-registered presets are offered. Dynamic
// deterministic tools remain absent until their real native owner exists.
std::vector<ModelToolDefinition> dynamic_plan_tool_definitions(const DynamicPlanCapabilities& frozen);
}
