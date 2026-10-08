#pragma once
#include "agentflow/records.hpp"
#include <optional>
#include <vector>

namespace agentflow {
struct RootBudgetExhausted : Conflict {using Conflict::Conflict;};
// Backend-owned execution policy and public immutable identity. No credentials
// or endpoints may enter these durable records or client observation DTOs.
struct RootBudgetSpec {
    std::string policy_id;
    std::int64_t policy_revision=1;
    std::string workspace_identity,provider_identity_json;
    std::int64_t max_children=8,max_parallel=2,max_model_calls=32,wall_limit_ms=600000;
};
struct RootBudgetRecord {
    std::string root_run_id;
    RootBudgetSpec spec;
    std::int64_t children_admitted=0,model_calls_reserved=0,parent_calls_held=0,revision=1;
    std::int64_t planned_children_reserved=0,parent_model_calls_reserved=0;
};
enum class ModelCallRole {parent,leaf};
enum class ModelCallPurpose {inference,context_compaction};
struct ModelCallReservation {
    std::string root_run_id,attempt_id,owner_run_id;
    ModelCallRole role=ModelCallRole::parent;
    std::string state;
    ModelCallPurpose purpose=ModelCallPurpose::inference;
    std::string inference_step_id;
};
struct DelegationTaskSpec {
    std::string task_id,node_id,child_run_id,prompt_json,objective;
};
struct DelegationBatchSpec {
    std::string id,parent_run_id,provider_tool_call_id,arguments_json,parent_assistant_json,preset_id="workspace.inspect";
    std::int64_t preset_revision=1,expected_budget_revision=0;
    std::vector<DelegationTaskSpec> tasks;
};
struct DelegationTaskRecord {
    Run run;
    std::string batch_id,task_id,preset_id;
    std::int64_t preset_revision=0;
    std::string outcome_json;
    std::optional<std::int64_t> settled_event_seq;
};
struct DelegationBatchRecord {
    std::string id,parent_run_id,root_run_id,provider_tool_call_id,arguments_json,parent_assistant_json,preset_id;
    std::int64_t preset_revision=0;
    std::string state,result_json;
    std::vector<DelegationTaskRecord> tasks;
    bool created=false;
};
enum class ChildAdmissionKind {graph_agent,graph_tool,delegated_leaf,dynamic_agent};
struct ChildAdmissionRecord {
    ChildAdmissionKind kind=ChildAdmissionKind::graph_tool;
    std::string root_run_id,batch_id,preset_id;
    std::int64_t preset_revision=0;
    std::string plan_id,node_label,claim_id;
    std::int64_t definition_revision=0,claim_revision=0;
    std::string backend_identity; // Private; never a public observation DTO field.
};
struct OwnedChildRecord {
    Run run;
    std::string kind,batch_id,task_id,preset_id;
    std::int64_t preset_revision=0;
    std::string plan_id,node_label,claim_id;
    std::int64_t definition_revision=0,claim_revision=0;
};
}
