#pragma once
#include "agentflow/records.hpp"
#include "agentflow/secret_protection.hpp"
#include "agentflow/operation.hpp"
#include "agentflow/delegation_records.hpp"
#include "agentflow/dynamic_plan_records.hpp"
#include <memory>
#include <vector>
#include <optional>

namespace agentflow {
class BackendLease;
class GraphPlan;
struct GraphRootRecord {Run run;std::string graph_id;std::int64_t graph_revision;std::string specification_json;std::int64_t checkpoint_revision;std::string checkpoint_json;std::string input_json;};
struct RootRunRecord {Run run;std::optional<std::int64_t> status_ms;std::int64_t status_sequence;};
struct RootRunPage {std::vector<RootRunRecord> entries;std::int64_t total,watermark;bool more;};
struct StatusTimeUnavailable : std::runtime_error {using std::runtime_error::runtime_error;};
struct CredentialMetadata {
    std::string scope,id,purpose,label;
    std::int64_t revision;
};
// Target repository: C++ contracts, all database operations through xlang3.
// Construct/use/destroy on the persistence thread. No direct SQLite linkage.
class Repository {
public:
    Repository(const std::string& database,const std::vector<std::string>& import_roots);
    ~Repository();
    Repository(const Repository&)=delete;
    Repository& operator=(const Repository&)=delete;
    Session create_session(const std::string& id,const std::string& title);
    Session rename_session(const std::string& id,const std::string& title,const std::string& expected_title);
    Session session(const std::string& id);
    std::vector<Session> sessions();
    Run create_run(const std::string& id,const std::string& session_id);
    std::optional<Run> incoming_message(const std::string& message,const std::string& context,const std::string& identity,const std::string& content);
    Run start_incoming_message(const std::string& id,const std::string& context,const std::string& message,const std::string& prompt_json,const std::string& identity,std::optional<RootBudgetSpec> budget={},std::optional<DynamicPlanCapabilities> dynamic={});
    std::optional<std::vector<Message>> task_history(const std::string& id);
    std::optional<std::string> incoming_message_payload(const std::string& run_id);
    Run start_prompt_run(const std::string& id,const std::string& session_id,const std::string& prompt_json,std::optional<RootBudgetSpec> budget={},std::optional<DynamicPlanCapabilities> dynamic={});
    DynamicPlanCapabilities dynamic_capabilities(const std::string& root);
    DynamicPlanCapabilities finalize_dynamic_capabilities(const std::string& root,const std::string& backend_identity,const std::string& catalogue_json,const std::vector<DynamicPresetCapability>& presets);
    DynamicPlanRecord dynamic_plan(const std::string& plan);
    std::optional<DynamicPlanRecord> dynamic_plan_for_root(const std::string& root);
    DynamicPlanCallRecord dynamic_plan_call(const std::string& call);
    // Backend-only call records contain exact private provider receipts.
    // HTTP/CLI observation must project a fixed public field whitelist.
    std::vector<DynamicPlanCallRecord> dynamic_plan_calls(const std::string& root);
    std::vector<DynamicPlanRevisionRecord> dynamic_plan_revisions(const std::string& plan);
    DynamicPlanRevisionRecord dynamic_plan_revision(const std::string& plan,std::int64_t revision);
    DynamicPlanCallRecord accept_dynamic_plan_change(const DynamicPlanChangeSpec& spec);
    DynamicFrontierClaim admit_dynamic_frontier(const DynamicFrontierSpec& spec);
    DynamicNodeRecord settle_dynamic_child(const std::string& child);
    DynamicPlanStepResult settle_dynamic_plan_step(const std::string& call);
    DynamicHumanRequest publish_dynamic_human(const DynamicHumanRequestSpec& spec);
    DynamicHumanRequest dynamic_human_request(const std::string& plan,const std::string& request);
    std::vector<DynamicHumanRequest> dynamic_human_requests(const std::string& plan);
    DynamicHumanRequest input_dynamic_human(const DynamicHumanInputSpec& spec);
    DynamicHumanRequest expire_dynamic_human(const std::string& plan,const std::string& request);
    DynamicBudgetSegment open_dynamic_budget_segment(const DynamicSegmentSpec& spec);
    DynamicBudgetSegment dynamic_budget_segment(const std::string& root);
    Run suspend_dynamic_owner(const DynamicPauseSpec& spec);
    DynamicResumeRecord resume_dynamic_owner(const DynamicResumeSpec& spec);
    void commit_dynamic_tool_turn(const std::string& root,const std::string& call);
    void record_rejected_dynamic_tool_turn(const std::string& root,const std::string& origin_attempt_id,const std::string& actual_assistant_json,const std::string& safe_code);
    ModelCallReservation reserve_dynamic_continuation(const std::string& call,const std::string& attempt);
    Run retire_dynamic_owner(const std::string& root,RunState terminal_state,const std::string& reason_json,const std::string& segment_id={},std::int64_t measured_active_elapsed_ms=0);
    Run complete_dynamic_owner(const std::string& root,const std::string& assistant_json,const std::string& segment_id,std::int64_t measured_active_elapsed_ms);
    RootBudgetRecord root_budget(const std::string& id);
    ModelCallReservation reserve_model_call(const std::string& root,const std::string& owner,const std::string& attempt,ModelCallRole role);
    ModelCallReservation start_model_call(const std::string& root,const std::string& owner,const std::string& attempt);
    ModelCallReservation finish_model_call(const std::string& root,const std::string& owner,const std::string& attempt,std::optional<std::string> actual_assistant_json={});
    DelegationBatchRecord accept_delegation_batch(const DelegationBatchSpec& spec);
    DelegationBatchRecord delegation_batch(const std::string& id);
    std::vector<DelegationBatchRecord> delegation_batches(const std::string& parent);
    DelegationTaskRecord settle_delegation_child(const std::string& child);
    DelegationBatchRecord settle_delegation_batch(const std::string& id);
    ChildAdmissionRecord child_admission(const std::string& child);
    std::vector<OwnedChildRecord> owned_children(const std::string& parent);
    std::vector<Message> owned_child_history(const std::string& parent,const std::string& child);
    std::vector<Event> tree_events(const std::string& parent,std::int64_t after=0,std::size_t count=256);
    Run start_graph_run(const std::string& id,const std::string& session_id,const std::string& graph_id,std::int64_t revision,const GraphPlan& plan,const std::string& prompt_json);
    GraphRootRecord graph_run(const std::string& id);
    Run start_graph_child(const std::string& id,const std::string& parent_id,const std::string& node_id,const std::string& prompt_json,std::int64_t expected_checkpoint_revision=0);
    GraphRootRecord settle_graph_child(const std::string& child_id,std::int64_t expected_checkpoint_revision=0);
    GraphRootRecord start_graph_human(const std::string& id,const std::string& node_id,std::int64_t expected_checkpoint_revision);
    GraphRootRecord input_graph_human(const std::string& id,const std::string& node_id,const std::string& input_json,const std::string& actor,std::int64_t expected_checkpoint_revision);
    GraphRootRecord skip_graph_node(const std::string& id,const std::string& node_id,std::int64_t expected_checkpoint_revision);
    Run retire_graph_run(const std::string& id,RunState terminal_state,const std::string& reason_json);
    std::vector<Run> children(const std::string& parent_id);
    std::vector<Message> run_history(const std::string& id);
    std::vector<Event> graph_events(const std::string& id,std::int64_t after=0);
    void append_user_message(const std::string& session_id,const std::string& json);
    void record_tool_turn(const std::string& run_id,const std::string& assistant_json,const std::vector<std::string>& tool_json);
    Run complete_run(const std::string& run_id,const std::string& assistant_json);
    Run run(const std::string& id);
    std::vector<Run> runs(const std::string& session_id);
    RootRunPage list_root_runs(const std::string& context,const std::string& state,std::optional<std::int64_t> since,std::size_t count,
        std::int64_t watermark=0,std::optional<std::int64_t> cursor_ms={},std::int64_t cursor_sequence=0);
    Run transition(const std::string& id,RunState expected,RunState next,const std::string& json="{}");
    Event append_event(const std::string& id,const std::string& kind,const std::string& json);
    std::vector<Event> events(const std::string& id,std::int64_t after=0);
    std::vector<Event> event_batch(const std::string& id,std::int64_t after,std::size_t count);
    void append_message(const std::string& id,const std::string& role,const std::string& json);
    std::vector<Message> history(const std::string& id);
    void put_information(const std::string& category,const std::string& id,const std::string& json);
    // Exact durable compare-and-swap; null expected means create only if absent.
    void compare_information(const std::string& category,const std::string& id,const std::string& json,const std::optional<std::string>& expected);
    std::string information(const std::string& category,const std::string& id);
    // Effect journal and one-operation permissions. Backend callers authorize
    // controller access before deciding; only an owning executor claims/finishes.
    Operation request_operation(const std::string& id,const OperationSpec& spec,std::int64_t expires_unix_ms);
    Operation operation(const std::string& id);
    std::vector<Operation> operations(const std::string& run_id);
    // Actor is derived from the backend's authenticated controller context.
    Operation decide_operation(const std::string& id,OperationDecision decision,const std::string& actor);
    Operation claim_operation(const std::string& id,const OperationSpec& actual);
    Operation cancel_operation(const std::string& id,const OperationSpec& actual);
    Operation expire_operation(const std::string& id);
    Operation finish_operation(const std::string& id,OperationState outcome,const std::string& result_json);
    // Backend-internal operations. Service must authorize scope before calling.
    // expected_revision=0 creates; positive revisions rotate with stale-write rejection.
    // Deleted identities are retired permanently to prevent stale-reference reuse.
    CredentialMetadata put_credential(const std::string& scope,const std::string& id,
        const std::string& purpose,const std::string& label,const SecretBytes& secret,
        std::int64_t expected_revision);
    std::vector<CredentialMetadata> credentials(const std::string& scope);
    SecretBytes resolve_credential(const std::string& scope,const std::string& id,
        const std::string& purpose);
    void delete_credential(const std::string& scope,const std::string& id,std::int64_t expected_revision);
    std::size_t recover_interrupted(const BackendLease& owner);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
