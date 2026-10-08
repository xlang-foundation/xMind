#pragma once
#include "agentflow/delegation_records.hpp"
#include "agentflow/context_records.hpp"
#include "agentflow/run_executor.hpp"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace agentflow {
struct DynamicPlanUnavailable : RunUnavailable {using RunUnavailable::RunUnavailable;};
struct DynamicPlanChanged : Conflict {using Conflict::Conflict;};
struct DynamicOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
struct DynamicHumanExpired : std::runtime_error {using std::runtime_error::runtime_error;};
// Only a model change rejected BEFORE durable acceptance may be returned as a
// normal tool error. Accepted execution failures retain their typed retirement
// or fail-stop semantics and must never acquire a generic conversation commit.
struct DynamicPlanRejected : std::runtime_error {
    std::string code;
    explicit DynamicPlanRejected(std::string value):std::runtime_error("Dynamic plan was not accepted"),code(std::move(value)){
        if(code!="invalid_plan_arguments"&&code!="plan_changed"&&code!="planning_budget_exhausted")throw std::invalid_argument("Invalid bounded planning rejection code");
    }
};

// Native registered policy. Model arguments never choose these limits.
struct DynamicPlanningPolicy {
    std::int64_t max_nodes=32,max_revisions=16,max_humans=8;
    std::int64_t change_bytes=262144,human_expiry_ms=900000,max_parent_turns=16;
};
struct DynamicPresetCapability {
    std::string id;
    std::int64_t revision=1;
    bool readonly=true;
    std::vector<std::string> tools;
    std::int64_t turn_limit=4;
    std::string backend_identity; // Private identity of the bounded child settings.
};
// Backend-private capability material. Do not serialize this struct into a
// model prompt or public HTTP DTO. The identity binds configured settings,
// instructions and credential references/versions, never credential values.
// Actual schemas/catalogue are finalized once after execution-side discovery;
// root admission and metadata reads do not launch MCP peers.
struct DynamicPlanCapabilities : DynamicPlanningPolicy {
    std::int64_t revision=1;
    std::string backend_identity,workspace_identity,provider_identity_json;
    std::vector<DynamicPresetCapability> presets;
    std::string tool_catalog_json;
    bool catalogue_finalized=false;
};

enum class DynamicNodeKind {agent,human};
enum class DynamicDependencyRequirement {success,observed};
enum class DynamicNodeState {pending,blocked,claimed,waiting_human,settled,skipped,cancelled,uncertain};
struct DynamicDependency {
    std::string task;
    DynamicDependencyRequirement require=DynamicDependencyRequirement::success;
};
struct DynamicNodeDefinition {
    std::string label;
    DynamicNodeKind kind=DynamicNodeKind::agent;
    std::string objective,question,preset_id;
    std::int64_t preset_revision=1;
    std::vector<DynamicDependency> dependencies;
};
struct DynamicNodeRecord {
    DynamicNodeDefinition definition;
    std::string backend_node_id;
    DynamicNodeState state=DynamicNodeState::pending;
    std::int64_t definition_revision=0,claim_revision=0;
    std::string claim_id,child_run_id,human_request_id,child_state,outcome_json;
    std::optional<std::int64_t> settled_event_seq;
    std::string effect_state="none";
    bool protected_definition=false;
};
struct DynamicPlanRecord {
    std::string id,root_run_id;
    std::int64_t revision=0,state_sequence=0;
    std::string state="active";
    DynamicPlanCapabilities capabilities;
    std::vector<DynamicNodeRecord> nodes;
    std::vector<std::string> retired_labels;
    std::int64_t planned_children_reserved=0,planned_humans_reserved=0,humans_published=0;
};
struct DynamicPlanChange {
    std::int64_t expected_revision=0,expected_state_sequence=0;
    std::vector<DynamicNodeDefinition> add,replace;
    std::vector<std::string> skip;
    std::string arguments_json;
};
struct DynamicPlanCandidate {
    std::vector<DynamicNodeRecord> nodes;
    std::vector<std::string> retired_labels;
    std::int64_t planned_children_reserved=0,planned_humans_reserved=0,newhuman_count=0;
    std::string canonical_spec_json;
};

// Backend-authored transaction inputs. The repository reparses and validates
// the exact actual call, then derives candidates/outcomes from owned ledgers.
// These records are not public spawn/state/outcome mutation APIs.
struct DynamicPlanChangeSpec {
    std::string id,plan_id,root_run_id,provider_tool_call_id,origin_attempt_id;
    std::string arguments_json,parent_assistant_json,backend_identity;
    std::int64_t expected_budget_revision=0;
};
struct DynamicPlanCallRecord {
    std::string id,plan_id,root_run_id,provider_tool_call_id,origin_attempt_id;
    std::string arguments_json,parent_assistant_json,state,result_json;
    std::int64_t accepted_revision=0,accepted_event_seq=0;
    std::optional<std::int64_t> result_event_seq,conversation_commit_seq;
    std::string continuation_attempt_id;
    bool created=false;
};
// Accepted topology definitions only: no capability identity, signed provider
// receipts, runtime settings, credentials or caller-authored outcome fields.
struct DynamicPlanRevisionRecord {
    std::string plan_id;
    std::int64_t revision=0,accepted_event_seq=0;
    std::string call_id,canonical_spec_json;
};
struct DynamicTaskClaimSpec {
    std::string label,claim_id,child_run_id,prompt_json,resolved_input_json;
    std::int64_t definition_revision=0;
};
struct DynamicFrontierSpec {
    std::string plan_id,backend_identity,plan_call_id;
    std::int64_t expected_revision=0,expected_state_sequence=0,expected_budget_revision=0;
    std::vector<DynamicTaskClaimSpec> tasks;
};
struct DynamicFrontierClaim {
    DynamicPlanRecord plan;
    RootBudgetRecord budget;
    std::vector<DynamicNodeRecord> nodes;
    bool created=false;
};
struct DynamicHumanRequestSpec {
    std::string id,plan_id,label,backend_identity,plan_call_id;
    std::int64_t expected_revision=0,expected_state_sequence=0,expires_unix_ms=0;
};
struct DynamicHumanRequest {
    std::string id,plan_id,root_run_id,label,backend_node_id,question,state,input_json,actor;
    std::int64_t definition_revision=0,published_event_seq=0,expires_unix_ms=0;
    std::optional<std::int64_t> input_event_seq;
};
struct DynamicHumanInputSpec {
    std::string plan_id,request_id,input_json,authenticated_actor,backend_identity;
    std::int64_t expected_revision=0,expected_state_sequence=0;
};
struct DynamicBudgetSegment {
    std::string root_run_id,id,state;
    std::int64_t ordinal=0,active_elapsed_ms=0,remaining_active_ms=0,opened_event_seq=0;
    std::optional<std::int64_t> closed_event_seq;
};
struct DynamicSegmentSpec {
    std::string root_run_id,id,backend_identity;
    std::int64_t expected_budget_revision=0;
};
struct DynamicPauseSpec {
    std::string plan_id,plan_call_id,segment_id,backend_identity;
    std::int64_t expected_revision=0,expected_state_sequence=0,expected_budget_revision=0;
    std::int64_t measured_active_elapsed_ms=0;
    std::optional<ContextPausePin> context_pin;
};
struct DynamicResumeSpec {
    std::string plan_id,segment_id,backend_identity;
    std::int64_t expected_revision=0,expected_state_sequence=0,expected_budget_revision=0;
    std::optional<ContextPausePin> context_pin;
};
struct DynamicResumeRecord {
    Run run;
    DynamicPlanRecord plan;
    DynamicBudgetSegment segment;
    DynamicPlanCallRecord pending_call;
    std::optional<ContextPausePin> context_pin;
};
struct DynamicPlanStepResult {
    DynamicPlanRecord plan;
    DynamicPlanCallRecord call;
    bool ready=false;
};
}
