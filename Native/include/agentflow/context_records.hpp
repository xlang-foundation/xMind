#pragma once
#include "agentflow/delegation_records.hpp"
#include <cstddef>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace agentflow {
// Backend-private records. None is an HTTP/CLI/renderer observation DTO. All
// original messages remain in the transcript; a checkpoint is a projection.
enum class ContextScopeKind {session,execution};
struct ContextScope {
    ContextScopeKind kind=ContextScopeKind::session;
    std::string id;
    bool operator==(const ContextScope&) const=default;
};
struct ContextBinding {
    std::string provider_identity_json;
    // Native authority digest includes the selected endpoint, workspace,
    // credential metadata/revision and instructions/tool policy, never a key.
    std::string authority_identity;
    std::string policy_id="native.context";
    std::int64_t policy_revision=1;
    std::string strategy_id="responses.compact";
    std::int64_t strategy_revision=1;
    bool operator==(const ContextBinding&) const=default;
};
struct ContextPolicy {
    std::string id="native.context";
    std::int64_t revision=1;
    std::string strategy_id="responses.compact";
    std::int64_t strategy_revision=1;
    bool automatic=true;
    std::size_t max_groups=4096,max_messages=8192;
    std::size_t max_request_bytes=8*1024*1024,max_checkpoint_bytes=4*1024*1024;
    std::int64_t max_maintenance_calls=4,max_count_requests=32;
    std::int64_t maintenance_deadline_ms=120000;
    // Keep the exact latest user/objective independently of the rolling prefix.
    // Recent complete groups form a protected tail; settled earlier groups of
    // the current execution may be compacted without changing that objective.
    std::size_t retain_recent_groups=2;
};
struct ContextReadBound {
    std::size_t max_groups=4096,max_metadata_bytes=1024*1024;
    std::size_t max_payload_bytes=8*1024*1024;
};
enum class ConversationGroupKind {user,assistant,tool_turn};
struct ConversationGroup {
    ContextScope scope;
    // Ordinals are scope-local committed group watermarks, not message seqs.
    std::int64_t ordinal=0,first_message_seq=0,last_message_seq=0;
    std::optional<std::string> execution_owner;
    std::optional<std::int64_t> committed_event_seq;
    ConversationGroupKind kind=ConversationGroupKind::user;
    std::int64_t message_count=0,payload_bytes=0;
    std::string source_binding;
    bool legacy_unattributed=false;
};
struct ContextHead {
    ContextScope scope;
    ContextBinding binding;
    std::int64_t revision=0,covered_through_ordinal=0;
    std::optional<std::string> checkpoint_id;
};
struct ContextPausePin {
    ContextHead head;
    std::int64_t source_watermark=0;
    std::string source_binding,protected_user_binding;
};
struct ContextManualRequest {
    std::string id,actor,state;
    ContextScope scope;
    ContextBinding binding;
    std::int64_t expected_head_revision=0;
    std::optional<std::string> owner_id,compaction_id;
};
// One read-only persistence observation. UI polling reads only indexed source
// metadata and the committed checkpoint's supplied metrics, never its window.
struct ContextCheckpointObservation {
    std::string compaction_id,actual_usage_json;
    std::int64_t measured_elapsed_ms=0;
    std::optional<std::int64_t> measured_preparation_elapsed_ms;
};
struct ContextStatusObservation {
    ContextHead head;
    std::int64_t source_watermark=0;
    std::optional<ContextManualRequest> current_manual;
    std::optional<ContextCheckpointObservation> checkpoint;
};
struct ContextSnapshot {
    ContextHead head;
    std::int64_t source_watermark=0,latest_user_ordinal=0;
    // Metadata only, in original order after the head's covered boundary.
    std::vector<ConversationGroup> groups;
    // Indexed pinned-user metadata can precede the covered head boundary.
    std::vector<ConversationGroup> protected_users;
    std::optional<ContextManualRequest> pending_manual_request;
};
struct ContextGroupPayload {
    ConversationGroup group;
    std::vector<Message> originals;
};
enum class ContextProjectionKind {responses_canonical,structured_summary};
struct ContextProjection {
    ContextHead head;
    std::string compaction_id,source_manifest_binding;
    ContextProjectionKind kind=ContextProjectionKind::responses_canonical;
    // Exact validated canonical array, or exact structured summary object.
    std::string projection_json;
    std::string actual_response_json,actual_usage_json="null";
    std::optional<std::string> provider_response_id,visible_summary;
    std::int64_t measured_elapsed_ms=0;
    // Provider request time above stays immutable when the result is observed.
    // Preparation time additionally includes native/count/receipt work through
    // the final publication request boundary; the root clock covers commit I/O.
    std::optional<std::int64_t> measured_preparation_elapsed_ms;
    std::optional<std::int64_t> provider_created_at;
    std::vector<std::int64_t> protected_user_ordinals;
    std::string protected_user_binding;
};
struct ContextSelection {
    std::int64_t covered_through_ordinal=0,protected_from_ordinal=0;
    // Native digests bind complete prefix groups and the exact original tail.
    std::string source_manifest_binding,protected_tail_binding;
    std::vector<std::int64_t> protected_user_ordinals;
    std::string protected_user_binding;
};
struct ActiveContextAdmission {
    std::string root_run_id,owner_run_id,inference_step_id,inference_attempt_id;
    ModelCallRole role=ModelCallRole::parent;
    std::int64_t expected_budget_revision=0;
};
struct PlanningContextAdmission {
    std::string root_run_id,owner_run_id,inference_step_id,held_attempt_id,plan_call_id;
    std::int64_t expected_budget_revision=0;
};
struct IdleContextAdmission {
    // A real exclusive maintenance owner; no prompt or inference is fabricated.
    std::string owner_id;
    std::int64_t max_model_calls=4,wall_limit_ms=120000;
};
struct IdleContextOwnerSpec {
    std::string id,manual_request_id;
    ContextScope scope;
    ContextBinding binding;
    std::int64_t expected_head_revision=0,max_model_calls=4,wall_limit_ms=120000;
};
struct IdleContextOwnerRecord {
    IdleContextOwnerSpec spec;
    std::string state;
    std::int64_t opened_unix_ms=0,remaining_active_ms=0,model_calls_reserved=0;
};
struct RebuildContextAdmission {
    std::string root_run_id,owner_run_id,inference_step_id,failed_attempt_id,retry_attempt_id;
    ModelCallRole role=ModelCallRole::parent;
    // If present this is the original signed call; its held pointer is fixed.
    std::optional<std::string> planning_call_id;
    std::int64_t expected_budget_revision=0;
};
using ContextAdmission=std::variant<ActiveContextAdmission,PlanningContextAdmission,IdleContextAdmission,RebuildContextAdmission>;
struct ContextCompactionSpec {
    std::string id,maintenance_attempt_id;
    ContextSnapshot snapshot;
    ContextSelection selection;
    ContextPolicy policy;
    ContextAdmission admission;
    // Exact backend-serialized maintenance request, bound before dispatch.
    // Provider-native canonical output must retain its ordered original users.
    std::string input_json,input_binding;
    std::optional<std::string> manual_request_id;
};
struct ContextStepReservation {
    std::string compaction_id,maintenance_attempt_id,state;
    ContextScope scope;
    ContextAdmission admission;
    // Present for funded active/planning continuation; absent for idle work.
    std::optional<ModelCallReservation> inference;
};
struct ContextCompactionCommit {
    std::string compaction_id,maintenance_attempt_id;
    ContextBinding binding;
    std::int64_t expected_head_revision=0,source_watermark=0;
    std::string source_manifest_binding,protected_tail_binding;
    ContextProjectionKind kind=ContextProjectionKind::responses_canonical;
    // Actual adapter result. The repository revalidates envelope/projection/
    // usage matching, and never writes an ordinary assistant receipt for it.
    std::string actual_response_json,projection_json,actual_usage_json="null";
    std::optional<std::string> provider_response_id,visible_summary;
    std::optional<std::int64_t> provider_created_at;
    std::int64_t measured_elapsed_ms=0;
    std::optional<std::int64_t> measured_preparation_elapsed_ms;
    // Filled only for head installation, after the observed result is durably
    // stored. Exact bound provider counts must demonstrate actual reduction.
    std::optional<std::string> before_measure_id,after_measure_id;
    std::string prospective_input_binding;
};
enum class ContextFailureCode {
    cancelled,deadline,provider_failure,invalid_result,capacity_exceeded,
    source_changed,binding_changed,interrupted,native_failure
};
struct ContextCompactionFailure {
    std::string compaction_id,maintenance_attempt_id;
    ContextFailureCode code=ContextFailureCode::provider_failure;
    std::int64_t measured_elapsed_ms=0;
};
struct ContextManualRequestSpec {
    std::string id,actor;
    ContextScope scope;
    ContextBinding binding;
    std::int64_t expected_head_revision=0;
};
enum class ContextMeasureSemantics {provider_exact,provider_estimate,serialized_bytes};
struct ContextInputMeasure {
    std::string id,owner_id,payload_binding,model_id;
    ContextScope scope;
    ContextBinding binding;
    std::int64_t head_revision=0,source_watermark=0;
    ContextMeasureSemantics semantics=ContextMeasureSemantics::serialized_bytes;
    std::int64_t value=0,serialized_bytes=0,measured_elapsed_ms=0;
    std::string actual_response_json;
};
struct ContextMeasureRequestSpec {
    std::string id,owner_id,payload_binding,model_id;
    std::optional<std::string> root_run_id;
    std::optional<std::string> compaction_id;
    ContextScope scope;
    ContextBinding binding;
    std::int64_t head_revision=0,source_watermark=0,serialized_bytes=0;
    std::int64_t max_requests=32,deadline_ms=120000;
    std::string input_json,count_request_binding;
};
struct ContextMeasureReservation {
    ContextMeasureRequestSpec request;
    std::string state;
};
struct InferenceStepSpec {
    std::string id,root_run_id,owner_run_id,attempt_id;
    ModelCallRole role=ModelCallRole::parent;
    ContextScope scope;
    ContextBinding binding;
    std::int64_t head_revision=0,source_watermark=0,expected_budget_revision=0;
    std::string input_binding;
    std::optional<std::string> planning_call_id;
};
struct InferenceStepRecord {
    std::string id,root_run_id,owner_run_id,original_attempt_id,state;
    std::optional<std::string> planning_call_id,successful_attempt_id;
    std::int64_t rebuild_count=0;
    std::vector<ModelCallReservation> physical_attempts;
    ContextScope scope;
    ContextBinding binding;
};
enum class InferenceAttemptOutcome {completed,context_overflow,failed,cancelled};
struct InferenceAttemptResult {
    std::string step_id,attempt_id;
    InferenceAttemptOutcome outcome=InferenceAttemptOutcome::failed;
    std::optional<std::string> actual_assistant_json;
    // A true overflow is mapped by the registered native provider adapter.
    // Persisted attempt-scoped output/effect evidence must also be zero.
    std::string fixed_provider_error_code;
    std::int64_t measured_elapsed_ms=0;
};
struct InferenceRebuildSpec {
    std::string step_id,failed_attempt_id,retry_attempt_id,compaction_id;
    ContextBinding binding;
    std::int64_t head_revision=0,source_watermark=0,expected_budget_revision=0;
    std::string input_binding;
};
struct ContextUnavailable : Conflict {using Conflict::Conflict;};
struct ContextCapacityExceeded : Conflict {using Conflict::Conflict;};
struct ContextSourceChanged : ContextUnavailable {using ContextUnavailable::ContextUnavailable;};
struct ContextBindingChanged : ContextUnavailable {using ContextUnavailable::ContextUnavailable;};
// The repository rolls back before raising this budget-only admission conflict.
// Refreshing this revision cannot replace scope, source, authority or a member.
struct ContextBudgetChanged : ContextUnavailable {using ContextUnavailable::ContextUnavailable;};
struct ContextOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
// Portable native digest of exact bytes. It binds private records; it does not
// normalize JSON, call an SDK or expose the underlying payload.
std::string context_digest(std::string_view bytes);
}
