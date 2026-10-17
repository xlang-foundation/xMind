#include "agentflow/edit_executor.hpp"
#include "nlohmann/json.hpp"
#include <set>
#include <chrono>

namespace agentflow {
EditRecoveryInspection EditExecutor::inspect_uncertain(const std::string& id,std::stop_token cancel) {
    using Json=nlohmann::json;
    const auto operation=store_.operation(id).get();
    const bool replacement=operation.spec.tool=="replace_file";
    if(operation.state!=OperationState::uncertain || (!replacement&&operation.spec.tool!="patch_file")) throw Conflict("Operation is not an uncertain file edit");
    if(operation.spec.workspace!=owner_workspace_) throw ToolAccessDenied("Recovery inspection belongs to another workspace");
    const auto owner=store_.run(operation.spec.run_id).get();
    if(owner.state!=RunState::failed && owner.state!=RunState::cancelled) throw Conflict("Recovery inspection requires a retired owner");
    const auto plan=Json::parse(operation.spec.arguments_json,[](int depth,Json::parse_event_t,Json&) {
        if(depth>64) throw std::invalid_argument("Recovery proposal nesting exceeds limits");return true;
    });
    const auto action=replacement?std::string("update"):(plan.contains("action")&&plan["action"].is_string()?plan["action"].get<std::string>():std::string{});
    if(action!="update"&&action!="add"&&action!="delete"&&action!="move")throw Conflict("Uncertain patch action cannot be inspected");
    if(!plan.contains("path")||!plan["path"].is_string())throw std::invalid_argument("Recovery proposal has no verified target path");
    if(action!="add")for(const auto* key:{"file_id","before_sha256"})
        if(!plan.contains(key)||!plan[key].is_string())throw std::invalid_argument("Recovery proposal has no verified before-image preconditions");
    if(action!="delete"&&(!plan.contains("after_sha256")||!plan["after_sha256"].is_string()))throw std::invalid_argument("Recovery proposal has no verified after-image hash");
    if(plan.contains("folder_id")&&plan.value("folder_id",std::string{})!=folder_id_)throw ToolAccessDenied("Recovery inspection belongs to another workspace folder");
    if(plan.contains("workspace_id")&&plan.value("workspace_id",std::string{})!=owner_workspace_)throw ToolAccessDenied("Recovery workspace group changed");
    if(plan.contains("workspace_revision")&&(!plan["workspace_revision"].is_number_integer()||plan["workspace_revision"].get<std::int64_t>()!=workspace_revision_))throw ToolAccessDenied("Recovery membership revision changed");
    if(plan.contains("target_workspace_id")&&plan.value("target_workspace_id",std::string{})!=workspace_.identity())throw ToolAccessDenied("Recovery folder identity changed");
    std::vector<WorkspacePathFingerprint> paths;
    const auto inspect=[&](const std::string& path){auto value=workspace_.fingerprint_optional_file(path,cancel);if(value.workspace_id!=workspace_.identity())throw ToolAccessDenied("Recovery folder identity changed during inspection");paths.push_back(std::move(value));return paths.size()-1;};
    const auto primary=inspect(plan["path"].get<std::string>());
    std::optional<std::size_t> secondary;
    if(action=="move"){
        if(!plan.contains("destination_path")||!plan["destination_path"].is_string())throw std::invalid_argument("Move recovery has no destination path");
        secondary=inspect(plan["destination_path"].get<std::string>());
    }
    auto matches_created_directories=[&](const char* field,bool expect_absent){
        if(!plan.contains(field))return true;const auto& values=plan[field];if(!values.is_array()||values.size()>32)return false;
        for(const auto& item:values){if(!item.is_string())return false;const auto exists=workspace_.instruction_directory_identity(item.get<std::string>(),cancel).has_value();if(exists==expect_absent)return false;}
        return true;
    };
    const auto& target=paths[primary];const auto source_id=plan.value("file_id",std::string{});
    bool same_file=target.file&&(!source_id.empty()&&target.file->file_id==source_id);
    auto match=EditSnapshotMatch::different;
    if(action=="update"){
        if(target.file&&same_file&&target.file->content_sha256==plan["before_sha256"].get<std::string>())match=EditSnapshotMatch::before;
        else if(target.file&&same_file&&target.file->content_sha256==plan["after_sha256"].get<std::string>())match=EditSnapshotMatch::after;
    }else if(action=="add"){
        if(!target.file&&matches_created_directories("create_directories",true))match=EditSnapshotMatch::before;
        else if(target.file&&target.file->content_sha256==plan["after_sha256"].get<std::string>())match=EditSnapshotMatch::after;
    }else if(action=="delete"){
        if(target.file&&same_file&&target.file->content_sha256==plan["before_sha256"].get<std::string>())match=EditSnapshotMatch::before;
        else if(!target.file)match=EditSnapshotMatch::after;
    }else{
        const auto& destination=paths[*secondary];
        const bool source_before=target.file&&same_file&&target.file->content_sha256==plan["before_sha256"].get<std::string>();
        const bool destination_before=!destination.file&&matches_created_directories("create_directories",true);
        if(source_before&&destination_before)match=EditSnapshotMatch::before;
        else if(!target.file&&destination.file&&destination.file->file_id==source_id&&destination.file->content_sha256==plan["after_sha256"].get<std::string>()){
            same_file=true;match=EditSnapshotMatch::after;
        }
    }
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    WorkspaceFingerprint observed{};for(const auto& path:paths)if(path.file){observed=*path.file;break;}if(observed.path.empty())observed={target.path,target.workspace_id,{},{},0};
    return {operation,std::move(observed),match,same_file,now,action,std::move(paths)};
}
ModelToolDefinition EditExecutor::definition() {
    return {"edit_file","Propose one focused exact literal replacement in a UTF-8 workspace file. Do not send an entire file as old_text when the file exceeds 4 KiB; the backend rejects large whole-file replacements. Use apply_patch with focused unified-diff hunks for those changes. For related changes or multiple hunks, prefer one apply_patch call so the model can review the file once; each affected file still requires its own controller approval. Always waits for controller approval; stale files are rejected without writing. If content_conflict is returned, read the current file with read_file and make a fresh focused edit_file call from its exact current contents. When the user's requested result remains clear, do not ask them to restate it solely because the old precondition changed; preserve unrelated intervening content. The fresh proposal still waits for normal approval. Stop for clarification only if the new state makes the requested outcome ambiguous. Report only the actual tool outcome.",R"({"type":"object","properties":{"path":{"type":"string"},"old_text":{"type":"string","minLength":1},"new_text":{"type":"string"},"expected_occurrences":{"type":"integer","minimum":1,"maximum":1024},"folder_id":{"type":"string","description":"Optional registered workspace folder ID; omit it to use the primary folder."}},"required":["path","old_text","new_text"],"additionalProperties":false})"};
}
std::string EditExecutor::invoke(const std::string& id,const std::string& run,const std::string& source,std::int64_t expiry,std::stop_token cancel,InstructionPrecondition guidance) {
    using Json=nlohmann::json;
    if(source.size()>65536) throw std::invalid_argument("Edit arguments exceed limits");
    Json args;std::vector<std::set<std::string>> fields;
    try {args=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& parsed) {
        if(depth>64) throw std::invalid_argument("Edit argument nesting exceeds limits");
        if(event==Json::parse_event_t::object_start) fields.emplace_back();
        else if(event==Json::parse_event_t::object_end) fields.pop_back();
        else if(event==Json::parse_event_t::key && !fields.back().insert(parsed.get<std::string>()).second) throw std::invalid_argument("Duplicate edit argument");return true;
    });} catch(const Json::exception&) {throw std::invalid_argument("Invalid edit argument JSON");}
    if(!args.is_object() || args.size()<3 || args.size()>5) throw std::invalid_argument("Invalid edit argument fields");
    for(auto it=args.begin();it!=args.end();++it) if(it.key()!="path" && it.key()!="old_text" && it.key()!="new_text" && it.key()!="expected_occurrences" && it.key()!="folder_id") throw std::invalid_argument("Unknown edit argument");
    for(const auto* field:{"path","old_text","new_text"}) if(!args.contains(field) || !args[field].is_string()) throw std::invalid_argument("Missing edit text argument");
    std::size_t count=1;
    if(args.contains("expected_occurrences")) {
        const auto& number=args["expected_occurrences"];
        if(!number.is_number_integer() || number<1 || number>1024) throw std::invalid_argument("Invalid edit occurrence count");count=number.get<std::size_t>();
    }
    if(args.contains("folder_id")&&(!args["folder_id"].is_string()||args["folder_id"].get<std::string>()!=folder_id_))throw ToolAccessDenied("Edit folder does not match its admitted workspace folder");
    auto plan=workspace_.plan_replacement(args["path"].get<std::string>(),args["old_text"].get<std::string>(),args["new_text"].get<std::string>(),count,cancel);
    if(plan.before.content.size()>edit_file_full_replacement_limit && args["old_text"].get<std::string>()==plan.before.content)
        throw ToolEditNeedsPatch("Large whole-file edit_file calls are not accepted; use apply_patch with focused unified-diff hunks, then review the resulting bounded proposal");
    const auto actual=execute(id,run,std::move(plan),expiry,cancel,std::move(guidance));
    return Json{{"operation_id",id},{"path",actual.path},{"file_id",actual.file_id},{"content_sha256",actual.content_sha256},{"size",actual.content.size()}}.dump();
}
WorkspaceSnapshot EditExecutor::execute(const std::string& id,const std::string& run,
    WorkspaceEditPlan plan,std::int64_t expiry,std::stop_token cancel,InstructionPrecondition guidance) {
    using Json=nlohmann::json;
    const auto workspace=workspace_.identity();
    if(plan.before.workspace_id!=workspace) throw ToolAccessDenied("Edit proposal belongs to another workspace folder");
    // Owned plan bytes cannot change while the controller reviews them. The
    // repository bounds this JSON before any grant or filesystem effect.
    OperationSpec spec{run,owner_workspace_,"replace_file",Json{
        {"workspace_id",owner_workspace_},{"folder_id",folder_id_},{"workspace_revision",workspace_revision_},{"target_workspace_id",workspace},
        {"path",plan.before.path},{"file_id",plan.before.file_id},
        {"before_content",plan.before.content},{"before_sha256",plan.before.content_sha256},
        {"after_content",plan.after_content},{"after_sha256",plan.after_sha256},
        {"replaced_occurrences",plan.replaced_occurrences}}.dump()};
    if(!plan.parent_id.empty()){auto payload=Json::parse(spec.arguments_json);payload["parent_id"]=plan.parent_id;spec.arguments_json=payload.dump();}
    guidance.validate();if(guidance.verify){auto payload=Json::parse(spec.arguments_json);payload["repository_guidance"]=Json::parse(guidance.metadata_json);spec.arguments_json=payload.dump();}
    PermissionWaiter(store_).acquire(id,spec,expiry,cancel);
    auto finish=[&](OperationState outcome,const std::string& result) {
        try {store_.finish_operation(id,outcome,result).get();}
        catch(...) {throw EditOutcomeUnrecorded("Edit outcome could not be recorded; stop execution and recover the claimed operation");}
    };
    WorkspaceSnapshot actual;
    if(guidance.verify){try{guidance.verify(cancel);}catch(...){finish(OperationState::failed,R"({"reason":"repository_guidance_changed_or_unavailable_before_effect"})");throw;}}
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
