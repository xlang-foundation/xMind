#include "agentflow/edit_executor.hpp"
#include "nlohmann/json.hpp"

namespace agentflow {
WorkspaceSnapshot EditExecutor::execute(const std::string& id,const std::string& run,
    WorkspaceEditPlan plan,std::int64_t expiry,std::stop_token cancel) {
    using Json=nlohmann::json;
    const auto workspace=workspace_.identity();
    if(plan.before.workspace_id!=workspace) throw ToolAccessDenied("Edit proposal belongs to another workspace");
    // Owned plan bytes cannot change while the controller reviews them. The
    // repository bounds this JSON before any grant or filesystem effect.
    OperationSpec spec{run,workspace,"replace_file",Json{
        {"path",plan.before.path},{"file_id",plan.before.file_id},
        {"before_content",plan.before.content},{"before_sha256",plan.before.content_sha256},
        {"after_content",plan.after_content},{"after_sha256",plan.after_sha256},
        {"replaced_occurrences",plan.replaced_occurrences}}.dump()};
    PermissionWaiter(store_).acquire(id,spec,expiry,cancel);
    auto finish=[&](OperationState outcome,const std::string& result) {
        try {store_.finish_operation(id,outcome,result).get();}
        catch(...) {throw EditOutcomeUnrecorded("Edit outcome could not be recorded; stop execution and recover the claimed operation");}
    };
    WorkspaceSnapshot actual;
    try {actual=workspace_.apply_plan(plan,cancel);}
    catch(const ToolMutationUncertain&) {finish(OperationState::uncertain,R"({"reason":"file_effect_uncertain"})");throw;}
    catch(const ToolContentConflict&) {finish(OperationState::failed,R"({"reason":"stale_edit_precondition"})");throw;}
    catch(const ToolAccessDenied&) {finish(OperationState::failed,R"({"reason":"workspace_access_denied_before_effect"})");throw;}
    catch(const ToolFileError&) {finish(OperationState::failed,R"({"reason":"file_unavailable_before_effect"})");throw;}
    catch(const ToolCancelled&) {finish(OperationState::failed,R"({"reason":"cancelled_before_effect"})");throw;}
    catch(const std::invalid_argument&) {finish(OperationState::failed,R"({"reason":"invalid_edit_before_effect"})");throw;}
    catch(...) {finish(OperationState::uncertain,R"({"reason":"unexpected_edit_failure"})");throw;}
    // No cancellation check after actual application: record what happened.
    // Result construction/journaling failure leaves an executing claim for
    // startup recovery; it must never be converted into a no-effect failure.
    try {
        finish(OperationState::succeeded,Json{{"path",actual.path},{"workspace_id",actual.workspace_id},
            {"file_id",actual.file_id},{"before_sha256",plan.before.content_sha256},
            {"after_sha256",actual.content_sha256},{"size",actual.content.size()}}.dump());
    } catch(const EditOutcomeUnrecorded&) {throw;}
    catch(...) {throw EditOutcomeUnrecorded("Applied edit outcome could not be encoded; recovery is required");}
    return actual;
}
}
