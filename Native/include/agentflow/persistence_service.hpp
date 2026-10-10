#pragma once
#include "agentflow/repository.hpp"
#include <future>

namespace agentflow {
struct PersistenceClosed : std::runtime_error {using std::runtime_error::runtime_error;};
struct PersistenceBusy : std::runtime_error {using std::runtime_error::runtime_error;};

// Thread-safe request boundary for server/agent workers. The repository, runtime
// and lease exist only on its private worker. Results contain owned C++ values.
// Scope authorization belongs to the calling backend service.
class PersistenceService {
public:
    PersistenceService(std::string database,std::vector<std::string> import_roots,
        std::size_t max_pending=1024,std::optional<BackendOwnerBootstrap> bootstrap={},
        std::optional<LegacyOwnerBootstrap> legacy={});
    ~PersistenceService();
    PersistenceService(const PersistenceService&)=delete;
    PersistenceService& operator=(const PersistenceService&)=delete;
    // Stop accepting, drain accepted requests, join and release ownership.
    // Safe for concurrent callers; the object must outlive all callers.
    void close();
    // A serialized durable admission fence. Reads remain available; pending
    // mutations check the fence on the worker, including requests already queued.
    // Only this live generation can resume its exact quiescence receipt. A
    // stopped quiesced generation cannot be reopened by normal startup.
    std::future<BackendOwnerState> backend_owner();
    std::future<BackendOwnerState> quiesce_backend(BackendOwnerPrecondition);
    std::future<BackendOwnerState> resume_backend(BackendOwnerReceipt);
    // Consumes quiescence; same-generation resume is permanently rejected.
    // Transport shutdown, observed exit and replacement startup are separate.
    std::future<BackendOwnerState> request_backend_retirement(BackendOwnerReceipt,std::optional<BackendOwnerTarget> target={});
    std::future<BackendOwnerState> activate_backend_replacement(BackendOwnerReceipt,BackendOwnerTarget);
    std::future<Session> create_session(std::string id,std::string title);
    std::future<Session> rename_session(std::string id,std::string title,std::string expected_title);
    std::future<Session> session(std::string id);
    std::future<std::vector<Session>> sessions();
    std::future<Run> create_run(std::string id,std::string session_id);
    std::future<std::optional<Run>> incoming_message(std::string message,std::string context,std::string identity,std::string content);
    std::future<Run> start_incoming_message(std::string id,std::string context,std::string message,std::string prompt_json,std::string identity,std::optional<RootBudgetSpec> budget={},std::optional<DynamicPlanCapabilities> dynamic={});
    std::future<std::optional<std::vector<Message>>> task_history(std::string id);
    std::future<std::optional<std::string>> incoming_message_payload(std::string run_id);
    std::future<std::vector<Event>> event_batch(std::string id,std::int64_t after,std::size_t count);
    std::future<RootRunPage> list_root_runs(std::string context,std::string state,std::optional<std::int64_t> since,std::size_t count,
        std::int64_t watermark=0,std::optional<std::int64_t> cursor_ms={},std::int64_t cursor_sequence=0);
    std::future<Run> start_prompt_run(std::string id,std::string session_id,std::string prompt_json,std::optional<RootBudgetSpec> budget={},std::optional<DynamicPlanCapabilities> dynamic={});
    std::future<DynamicPlanCapabilities> dynamic_capabilities(std::string root);
    std::future<DynamicPlanCapabilities> finalize_dynamic_capabilities(std::string root,std::string backend_identity,std::string catalogue_json,std::vector<DynamicPresetCapability> presets);
    std::future<DynamicPlanRecord> dynamic_plan(std::string plan);
    std::future<std::optional<DynamicPlanRecord>> dynamic_plan_for_root(std::string root);
    std::future<DynamicPlanCallRecord> dynamic_plan_call(std::string call);
    std::future<std::vector<DynamicPlanCallRecord>> dynamic_plan_calls(std::string root);
    std::future<std::vector<DynamicPlanRevisionRecord>> dynamic_plan_revisions(std::string plan);
    std::future<DynamicPlanRevisionRecord> dynamic_plan_revision(std::string plan,std::int64_t revision);
    std::future<DynamicPlanCallRecord> accept_dynamic_plan_change(DynamicPlanChangeSpec spec);
    std::future<DynamicFrontierClaim> admit_dynamic_frontier(DynamicFrontierSpec spec);
    std::future<DynamicNodeRecord> settle_dynamic_child(std::string child);
    std::future<DynamicPlanStepResult> settle_dynamic_plan_step(std::string call);
    std::future<DynamicHumanRequest> publish_dynamic_human(DynamicHumanRequestSpec spec);
    std::future<DynamicHumanRequest> dynamic_human_request(std::string plan,std::string request);
    std::future<std::vector<DynamicHumanRequest>> dynamic_human_requests(std::string plan);
    std::future<DynamicHumanRequest> input_dynamic_human(DynamicHumanInputSpec spec);
    std::future<DynamicHumanRequest> expire_dynamic_human(std::string plan,std::string request);
    std::future<DynamicBudgetSegment> open_dynamic_budget_segment(DynamicSegmentSpec spec);
    std::future<DynamicBudgetSegment> dynamic_budget_segment(std::string root);
    std::future<std::optional<ContextPausePin>> dynamic_context_pause(std::string root);
    std::future<Run> suspend_dynamic_owner(DynamicPauseSpec spec);
    std::future<DynamicResumeRecord> resume_dynamic_owner(DynamicResumeSpec spec);
    std::future<void> commit_dynamic_tool_turn(std::string root,std::string call);
    std::future<void> record_rejected_dynamic_tool_turn(std::string root,std::string origin_attempt_id,std::string actual_assistant_json,std::string safe_code);
    std::future<ModelCallReservation> reserve_dynamic_continuation(std::string call,std::string attempt);
    std::future<Run> retire_dynamic_owner(std::string root,RunState terminal_state,std::string reason_json,std::string segment_id={},std::int64_t measured_active_elapsed_ms=0);
    std::future<Run> complete_dynamic_owner(std::string root,std::string assistant_json,std::string segment_id,std::int64_t measured_active_elapsed_ms);
    std::future<RootBudgetRecord> root_budget(std::string id);
    std::future<ModelCallReservation> reserve_model_call(std::string root,std::string owner,std::string attempt,ModelCallRole role);
    std::future<ModelCallReservation> start_model_call(std::string root,std::string owner,std::string attempt);
    std::future<ModelCallReservation> finish_model_call(std::string root,std::string owner,std::string attempt,std::optional<std::string> actual_assistant_json={});
    std::future<ContextSnapshot> context_snapshot(ContextScope scope,ContextBinding binding,ContextReadBound bound={});
    std::future<ContextStatusObservation> context_status_observation(ContextScope scope,ContextBinding binding);
    std::future<std::vector<ContextGroupPayload>> context_group_payloads(ContextSnapshot snapshot,std::vector<std::int64_t> ordinals,ContextReadBound bound={});
    std::future<std::optional<ContextProjection>> context_projection(ContextScope scope,ContextBinding binding,ContextReadBound bound={});
    std::future<ContextStepReservation> begin_context_compaction(ContextCompactionSpec spec);
    std::future<ContextStepReservation> start_context_compaction(std::string id,std::string maintenance_attempt);
    std::future<ContextProjection> record_context_compaction_response(ContextCompactionCommit result);
    std::future<ContextProjection> commit_context_compaction(ContextCompactionCommit result);
    std::future<void> retire_context_compaction(ContextCompactionFailure failure);
    std::future<ContextManualRequest> request_context_compaction(ContextManualRequestSpec spec);
    std::future<ContextManualRequest> context_manual_request(std::string id,ContextBinding binding);
    std::future<std::optional<ContextManualRequest>> context_current_manual_request(ContextScope scope,ContextBinding binding);
    std::future<ContextManualRequest> retire_context_request(std::string id,ContextBinding binding,ContextFailureCode code);
    std::future<IdleContextOwnerRecord> claim_idle_context_owner(IdleContextOwnerSpec spec);
    std::future<IdleContextOwnerRecord> idle_context_owner(std::string id);
    std::future<void> retire_idle_context_owner(std::string id,ContextFailureCode code,std::int64_t measured_elapsed_ms);
    std::future<ContextMeasureReservation> begin_context_measure(ContextMeasureRequestSpec spec);
    std::future<ContextMeasureReservation> start_context_measure(std::string id);
    std::future<void> finish_context_measure(ContextInputMeasure measure);
    std::future<void> retire_context_measure(std::string id,ContextFailureCode code,std::int64_t elapsed_ms);
    std::future<InferenceStepRecord> reserve_inference_step(InferenceStepSpec spec);
    std::future<InferenceStepRecord> inference_step(std::string id);
    std::future<InferenceStepRecord> finish_inference_attempt(InferenceAttemptResult result);
    std::future<InferenceStepRecord> reserve_inference_rebuild(InferenceRebuildSpec spec);
    std::future<DelegationBatchRecord> accept_delegation_batch(DelegationBatchSpec spec);
    std::future<DelegationBatchRecord> delegation_batch(std::string id);
    std::future<std::vector<DelegationBatchRecord>> delegation_batches(std::string parent);
    std::future<DelegationTaskRecord> settle_delegation_child(std::string child);
    std::future<DelegationBatchRecord> settle_delegation_batch(std::string id);
    std::future<ChildAdmissionRecord> child_admission(std::string child);
    std::future<std::vector<OwnedChildRecord>> owned_children(std::string parent);
    std::future<std::vector<Message>> owned_child_history(std::string parent,std::string child);
    std::future<std::vector<Event>> tree_events(std::string parent,std::int64_t after=0,std::size_t count=256);
    std::future<Run> start_graph_run(std::string id,std::string session_id,std::string graph_id,std::int64_t revision,GraphPlan plan,std::string prompt_json,std::optional<RootBudgetSpec> budget={},std::optional<GraphContextSpec> context={});
    std::future<GraphContextOwnerRecord> graph_context_owner(std::string root);
    std::future<GraphContextOpenRecord> open_graph_context_owner(GraphContextOpenSpec spec);
    std::future<GraphContextOpenRecord> resume_graph_context_owner(GraphContextOpenSpec spec);
    std::future<GraphRootRecord> suspend_graph_context_owner(GraphContextBoundarySpec spec);
    std::future<Run> complete_graph_context_owner(GraphContextBoundarySpec spec,std::string actual_join_json);
    std::future<Run> retire_graph_context_owner(std::string root,RunState terminal,std::string reason_json,std::string segment_id={},std::int64_t measured_elapsed_ms=0);
    std::future<GraphRootRecord> graph_run(std::string id);
    std::future<Run> start_graph_child(std::string id,std::string parent_id,std::string node_id,std::string prompt_json,std::int64_t expected_checkpoint_revision=0);
    std::future<GraphRootRecord> settle_graph_child(std::string child_id,std::int64_t expected_checkpoint_revision=0);
    std::future<GraphRootRecord> start_graph_human(std::string id,std::string node_id,std::int64_t expected_checkpoint_revision);
    std::future<GraphRootRecord> input_graph_human(std::string id,std::string node_id,std::string input_json,std::string actor,std::int64_t expected_checkpoint_revision);
    std::future<GraphRootRecord> skip_graph_node(std::string id,std::string node_id,std::int64_t expected_checkpoint_revision);
    std::future<Run> retire_graph_run(std::string id,RunState terminal_state,std::string reason_json);
    std::future<std::vector<Run>> children(std::string parent_id);
    std::future<std::vector<Message>> run_history(std::string id);
    std::future<std::vector<Event>> graph_events(std::string id,std::int64_t after=0);
    std::future<void> append_user_message(std::string session_id,std::string json);
    std::future<SkillSelections> initialize_run_skills(std::string id,std::string workspace_id);
    std::future<SkillSelections> run_skills(std::string id);
    std::future<SessionSkillState> session_skills(std::string session,std::string workspace);
    std::future<SessionSkillState> replace_session_skills(std::string session,SkillSelections selections,std::int64_t expected_revision);
    std::future<SessionAgentState> session_agent(std::string session);
    std::future<SessionAgentState> replace_session_agent(std::string session,std::optional<AgentDefinition> selected,std::int64_t expected_revision);
    std::future<void> record_tool_turn(std::string id,std::string assistant_json,std::vector<std::string> tool_json,std::optional<SkillSelections> skills={});
    std::future<Run> complete_run(std::string id,std::string assistant_json);
    std::future<Run> run(std::string id);
    std::future<std::vector<Run>> runs(std::string session_id);
    std::future<Run> transition(std::string id,RunState expected,RunState next,std::string json="{}");
    std::future<Event> append_event(std::string id,std::string kind,std::string json);
    std::future<std::vector<Event>> events(std::string id,std::int64_t after=0);
    std::future<void> append_message(std::string id,std::string role,std::string json);
    std::future<std::vector<Message>> history(std::string id);
    std::future<void> put_information(std::string category,std::string id,std::string json);
    std::future<void> compare_information(std::string category,std::string id,std::string json,std::optional<std::string> expected);
    std::future<std::string> information(std::string category,std::string id);
    std::future<Operation> request_operation(std::string id,OperationSpec spec,std::int64_t expires_unix_ms);
    std::future<Operation> operation(std::string id);
    std::future<std::vector<Operation>> operations(std::string run_id);
    std::future<Operation> decide_operation(std::string id,OperationDecision decision,std::string actor);
    std::future<Operation> claim_operation(std::string id,OperationSpec actual);
    std::future<Operation> cancel_operation(std::string id,OperationSpec actual);
    std::future<Operation> expire_operation(std::string id);
    std::future<Operation> finish_operation(std::string id,OperationState outcome,std::string result_json);
    std::future<CredentialMetadata> put_credential(std::string scope,std::string id,
        std::string purpose,std::string label,SecretBytes secret,std::int64_t expected_revision);
    std::future<std::vector<CredentialMetadata>> credentials(std::string scope);
    std::future<SecretBytes> resolve_credential(std::string scope,std::string id,std::string purpose);
    std::future<ResolvedCredential> resolve_credential_snapshot(std::string scope,std::string id,std::string purpose);
    std::future<void> delete_credential(std::string scope,std::string id,std::int64_t expected_revision);
    std::future<McpOAuthRefreshClaim> claim_mcp_oauth_refresh(McpOAuthRefreshSpec);
    std::future<McpOAuthRefreshRecord> mcp_oauth_refresh(std::string request_id);
    std::future<McpOAuthRefreshRecord> dispatch_mcp_oauth_refresh(std::string request_id,std::string generation);
    std::future<McpOAuthRefreshRecord> abandon_mcp_oauth_refresh(std::string request_id,std::string generation);
    std::future<McpOAuthRefreshRecord> publish_mcp_oauth_refresh(std::string request_id,std::string generation,SecretBytes complete_grant);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
