#include "agentflow/edit_executor.hpp"
#include "nlohmann/json.hpp"
#include <set>
#include <chrono>

namespace agentflow {
EditRecoveryInspection EditExecutor::inspect_uncertain(const std::string& id,std::stop_token cancel) {
    using Json=nlohmann::json;
    const auto operation=store_.operation(id).get();
    if(operation.state!=OperationState::uncertain || operation.spec.tool!="replace_file") throw Conflict("Operation is not an uncertain file edit");
    if(operation.spec.workspace!=workspace_.identity()) throw ToolAccessDenied("Recovery inspection belongs to another workspace");
    const auto owner=store_.run(operation.spec.run_id).get();
    if(owner.state!=RunState::failed && owner.state!=RunState::cancelled) throw Conflict("Recovery inspection requires a retired owner");
    const auto plan=Json::parse(operation.spec.arguments_json,[](int depth,Json::parse_event_t,Json&) {
        if(depth>64) throw std::invalid_argument("Recovery proposal nesting exceeds limits");return true;
    });
    for(const auto* key:{"path","file_id","before_sha256","after_sha256"})
        if(!plan.contains(key) || !plan[key].is_string()) throw std::invalid_argument("Recovery proposal has no verified file preconditions");
    const auto observed=workspace_.fingerprint_file(plan["path"].get<std::string>(),cancel);
    const bool same_file=observed.file_id==plan["file_id"].get<std::string>();
    auto match=EditSnapshotMatch::different;
    if(same_file && observed.content_sha256==plan["before_sha256"].get<std::string>()) match=EditSnapshotMatch::before;
    else if(same_file && observed.content_sha256==plan["after_sha256"].get<std::string>()) match=EditSnapshotMatch::after;
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return {operation,observed,match,same_file,now};
}
ModelToolDefinition EditExecutor::definition() {
    return {"edit_file","Propose an exact literal replacement in a UTF-8 workspace file. Always waits for controller approval; stale files are rejected. Report only the returned actual outcome.",R"({"type":"object","properties":{"path":{"type":"string"},"old_text":{"type":"string","minLength":1},"new_text":{"type":"string"},"expected_occurrences":{"type":"integer","minimum":1,"maximum":1024}},"required":["path","old_text","new_text"],"additionalProperties":false})"};
}
std::string EditExecutor::invoke(const std::string& id,const std::string& run,const std::string& source,std::int64_t expiry,std::stop_token cancel) {
    using Json=nlohmann::json;
    if(source.size()>65536) throw std::invalid_argument("Edit arguments exceed limits");
    Json args;std::vector<std::set<std::string>> fields;
    try {args=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& parsed) {
        if(depth>64) throw std::invalid_argument("Edit argument nesting exceeds limits");
        if(event==Json::parse_event_t::object_start) fields.emplace_back();
        else if(event==Json::parse_event_t::object_end) fields.pop_back();
        else if(event==Json::parse_event_t::key && !fields.back().insert(parsed.get<std::string>()).second) throw std::invalid_argument("Duplicate edit argument");return true;
    });} catch(const Json::exception&) {throw std::invalid_argument("Invalid edit argument JSON");}
    if(!args.is_object() || args.size()<3 || args.size()>4) throw std::invalid_argument("Invalid edit argument fields");
    for(auto it=args.begin();it!=args.end();++it) if(it.key()!="path" && it.key()!="old_text" && it.key()!="new_text" && it.key()!="expected_occurrences") throw std::invalid_argument("Unknown edit argument");
    for(const auto* field:{"path","old_text","new_text"}) if(!args.contains(field) || !args[field].is_string()) throw std::invalid_argument("Missing edit text argument");
    std::size_t count=1;
    if(args.contains("expected_occurrences")) {
        const auto& number=args["expected_occurrences"];
        if(!number.is_number_integer() || number<1 || number>1024) throw std::invalid_argument("Invalid edit occurrence count");count=number.get<std::size_t>();
    }
    auto plan=workspace_.plan_replacement(args["path"].get<std::string>(),args["old_text"].get<std::string>(),args["new_text"].get<std::string>(),count,cancel);
    const auto actual=execute(id,run,std::move(plan),expiry,cancel);
    return Json{{"operation_id",id},{"path",actual.path},{"file_id",actual.file_id},{"content_sha256",actual.content_sha256},{"size",actual.content.size()}}.dump();
}
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
