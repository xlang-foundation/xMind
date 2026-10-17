#pragma once
#include "agentflow/repository.hpp"
#include <future>

namespace agentflow {

// Durable run-state operations consumed directly by AgentRunner. The backend
// implementation remains PersistenceService; this narrow port is the first
// boundary for a future authenticated worker adapter. It deliberately carries
// owned values and futures, never repository or xlang3 runtime handles.
class AgentRunJournal {
public:
    virtual ~AgentRunJournal()=default;
    virtual std::future<bool> append_a2a_task_event(std::string run_id,std::string operation_id,std::string json)=0;
    virtual std::future<Event> append_event(std::string id,std::string kind,std::string json)=0;
    virtual std::future<ChildAdmissionRecord> child_admission(std::string child)=0;
    virtual std::future<void> commit_dynamic_tool_turn(std::string root,std::string call)=0;
    virtual std::future<Run> complete_dynamic_owner(std::string root,std::string assistant_json,std::string segment_id,std::int64_t measured_active_elapsed_ms)=0;
    virtual std::future<Run> complete_run(std::string id,std::string assistant_json)=0;
    virtual std::future<ContextStatusObservation> context_status_observation(ContextScope scope,ContextBinding binding)=0;
    virtual std::future<DynamicPlanCapabilities> dynamic_capabilities(std::string root)=0;
    virtual std::future<std::optional<ContextPausePin>> dynamic_context_pause(std::string root)=0;
    virtual std::future<DynamicPlanRecord> dynamic_plan(std::string plan)=0;
    virtual std::future<std::vector<DynamicPlanCallRecord>> dynamic_plan_calls(std::string root)=0;
    virtual std::future<std::optional<DynamicPlanRecord>> dynamic_plan_for_root(std::string root)=0;
    virtual std::future<std::optional<DynamicPlanRecord>> dynamic_plan_for_owner(std::string owner)=0;
    virtual std::future<std::vector<Event>> events(std::string id,std::int64_t after=0)=0;
    virtual std::future<DynamicPlanCapabilities> finalize_dynamic_capabilities(std::string root,std::string backend_identity,std::string catalogue_json,std::vector<DynamicPresetCapability> presets)=0;
    virtual std::future<InferenceStepRecord> finish_inference_attempt(InferenceAttemptResult result)=0;
    virtual std::future<Operation> finish_operation(std::string id,OperationState outcome,std::string result_json)=0;
    virtual std::future<GraphContextOwnerRecord> graph_context_owner(std::string root)=0;
    virtual std::future<IdleContextOwnerRecord> idle_context_owner(std::string id)=0;
    virtual std::future<SkillSelections> initialize_run_skills(std::string id,std::string workspace_id)=0;
    virtual std::future<DynamicBudgetSegment> open_dynamic_budget_segment(DynamicSegmentSpec spec)=0;
    virtual std::future<void> record_rejected_dynamic_tool_turn(std::string root,std::string origin_attempt_id,std::string actual_assistant_json,std::string safe_code)=0;
    virtual std::future<void> record_tool_turn(std::string id,std::string assistant_json,std::vector<std::string> tool_json,std::optional<SkillSelections> skills={})=0;
    virtual std::future<Run> retire_dynamic_owner(std::string root,RunState terminal_state,std::string reason_json,std::string segment_id={},std::int64_t measured_active_elapsed_ms=0)=0;
    virtual std::future<DynamicResumeRecord> resume_dynamic_owner(DynamicResumeSpec spec)=0;
    virtual std::future<RootBudgetRecord> root_budget(std::string id)=0;
    virtual std::future<Run> run(std::string id)=0;
    virtual std::future<std::vector<Message>> run_history(std::string id)=0;
    virtual std::future<Session> session(std::string id)=0;
    virtual std::future<ModelCallReservation> reserve_model_call(std::string root,std::string owner,std::string attempt,ModelCallRole role)=0;
    virtual std::future<ModelCallReservation> start_model_call(std::string root,std::string owner,std::string attempt)=0;
    virtual std::future<ModelCallReservation> finish_model_call(std::string root,std::string owner,std::string attempt,std::optional<std::string> assistant_json={})=0;
    virtual std::future<DynamicPlanCallRecord> dynamic_plan_call(std::string call)=0;
    virtual std::future<std::vector<DynamicPlanRecord>> dynamic_plans_for_root(std::string root)=0;
    virtual std::future<ModelCallReservation> reserve_dynamic_continuation(std::string call,std::string attempt)=0;
    virtual std::future<std::vector<OwnedChildRecord>> owned_children(std::string parent)=0;
    virtual std::future<Run> suspend_dynamic_owner(DynamicPauseSpec spec)=0;
    virtual std::future<Operation> request_operation(std::string id,OperationSpec spec,std::int64_t expires_unix_ms)=0;
    virtual std::future<Operation> operation(std::string id)=0;
    virtual std::future<Operation> decide_operation(std::string id,OperationDecision decision,std::string actor)=0;
    virtual std::future<Operation> claim_operation(std::string id,OperationSpec actual)=0;
    virtual std::future<Operation> cancel_operation(std::string id,OperationSpec actual)=0;
    virtual std::future<Operation> expire_operation(std::string id)=0;
    virtual std::future<Run> start_prompt_run(std::string id,std::string session_id,std::string prompt_json,std::optional<RootBudgetSpec> budget={},std::optional<DynamicPlanCapabilities> dynamic={},std::string inbox_id={})=0;
    virtual std::future<Run> transition(std::string id,RunState expected,RunState next,std::string json="{}")=0;
};

}
