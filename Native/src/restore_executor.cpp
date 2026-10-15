#include "agentflow/restore_executor.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>

namespace agentflow {
using Json=nlohmann::json;
namespace {
std::string parent_directory(const std::string& path){const auto value=std::filesystem::u8path(path).parent_path().generic_u8string();return value.empty()?".":std::string(reinterpret_cast<const char*>(value.data()),value.size());}
std::string required_string(const Json& value,const char* key){if(!value.contains(key)||!value[key].is_string())throw std::invalid_argument(std::string("Saved change has no ")+key);return value[key].get<std::string>();}
void require_hash(const std::string& content,const std::string& expected,const char* description){if(WorkspaceTools::hash_content(content)!=expected)throw Conflict(std::string("Saved ")+description+" bytes do not match their recorded hash");}
}
ModelToolDefinition RestoreExecutor::definition(){return {"restore_operation","Restore one successfully applied xMind file change by its saved operation_id. The native backend verifies the original operation belongs to this conversation and workspace, confirms current file identities and hashes still match its recorded after-state, then proposes the exact inverse through normal patch approval. This tool never writes without approval; use the operation_id returned by the earlier edit or patch tool.",R"({"type":"object","properties":{"operation_id":{"type":"string","minLength":1,"maxLength":128},"folder_id":{"type":"string","maxLength":128,"description":"Optional registered workspace folder ID; omit it to use the primary folder."}},"required":["operation_id"],"additionalProperties":false})"};}
std::string RestoreExecutor::invoke(const std::string& patch_id,const std::string& run_id,const std::string& source,
    std::int64_t expiry,std::stop_token cancel,PathAuthorizer authorize,GuidanceFactory guidance){
    if(source.size()>8192||patch_id.empty()||patch_id.size()>64||patch_id.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)throw std::invalid_argument("Invalid restore request identity");
    Json args;try{args=Json::parse(mcp_compact_object(source));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid restore request JSON");}
    if(!args.is_object()||args.empty()||args.size()>2||!args.contains("operation_id")||!args["operation_id"].is_string())throw std::invalid_argument("Invalid restore request fields");
    for(const auto& [key,value]:args.items())if(key!="operation_id"&&key!="folder_id")throw std::invalid_argument("Unsupported restore request field");
    const auto original_id=args["operation_id"].get<std::string>();if(original_id.empty()||original_id.size()>128||original_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos)throw std::invalid_argument("Invalid original operation ID");
    if(args.contains("folder_id")&&(!args["folder_id"].is_string()||args["folder_id"].get<std::string>()!=folder_id_))throw ToolAccessDenied("Restore folder does not match its admitted workspace folder");
    const auto current_run=store_.run(run_id).get();if(current_run.state!=RunState::running||current_run.graph_root)throw Conflict("Restore requires an active conversation run");
    const auto original=store_.operation(original_id).get();if(original.state!=OperationState::succeeded||original.spec.workspace!=owner_workspace_)throw Conflict("Only a successful file operation in this workspace can be restored");
    const auto owner=store_.run(original.spec.run_id).get();if(owner.session_id!=current_run.session_id)throw ToolAccessDenied("Restore operation belongs to another conversation");
    if(original.spec.tool!="replace_file"&&original.spec.tool!="create_file"&&original.spec.tool!="patch_file")throw std::invalid_argument("The selected operation is not a restorable file change");
    const auto saved=Json::parse(original.spec.arguments_json);const auto receipt=Json::parse(original.result_json);
    for(const auto* key:{"folder_id","workspace_id","workspace_revision","target_workspace_id"})if(!saved.contains(key))throw ToolAccessDenied("Saved file change has no admitted workspace binding");
    if(!saved["folder_id"].is_string()||saved["folder_id"]!=folder_id_||!saved["workspace_id"].is_string()||saved["workspace_id"]!=owner_workspace_||
       !saved["workspace_revision"].is_number_integer()||saved["workspace_revision"]!=workspace_revision_||!saved["target_workspace_id"].is_string()||saved["target_workspace_id"]!=workspace_.identity())
        throw ToolAccessDenied("Saved file change belongs to a different workspace membership or folder revision");
    if(!receipt.is_object())throw Conflict("Saved file operation has no actual outcome receipt");
    const auto action=original.spec.tool=="replace_file"?std::string("update"):original.spec.tool=="create_file"?std::string("add"):required_string(saved,"action");
    if((receipt.contains("operation_id")&&receipt.value("operation_id",std::string{})!=original_id)||
       (receipt.contains("state")&&receipt.value("state",std::string{})!="succeeded")||
       (receipt.contains("action")&&receipt.value("action",std::string{})!=action))
        throw Conflict("Saved file operation receipt does not match its successful journal entry");
    const auto path=required_string(saved,"path");
    WorkspacePatchFilePlan inverse;
    std::vector<std::string> authorized_paths{path};
    if(action=="update"){
        const auto file_id=required_string(saved,"file_id"),before_hash=required_string(saved,"before_sha256"),after_hash=required_string(saved,"after_sha256");
        const auto after_content=required_string(saved,"after_content"),before_content=required_string(saved,"before_content");
        require_hash(before_content,before_hash,"before-state");require_hash(after_content,after_hash,"after-state");
        if(receipt.value("path",std::string{})!=path||receipt.value("file_id",std::string{})!=file_id||receipt.value("after_sha256",std::string{})!=after_hash||
           (receipt.contains("before_sha256")&&receipt.value("before_sha256",std::string{})!=before_hash))throw Conflict("Saved edit receipt differs from its approved before/after state");
        const auto current=workspace_.snapshot_file(path,cancel);if(current.file_id!=file_id||current.content_sha256!=after_hash||current.content!=after_content)throw ToolContentConflict("Current file differs from the saved edit after-state; no restore proposal was created");
        inverse=WorkspaceEditPlan{current,before_content,WorkspaceTools::hash_content(before_content),1,workspace_.directory_identity(parent_directory(path),cancel)};
    }else if(action=="add"){
        const auto after_hash=required_string(saved,"after_sha256");const auto after_content=required_string(saved,"after_content");const auto file_id=required_string(receipt,"file_id");
        require_hash(after_content,after_hash,"created after-state");
        if(receipt.value("path",std::string{})!=path||receipt.value("after_sha256",receipt.value("content_sha256",std::string{}))!=after_hash)throw Conflict("Saved creation receipt differs from its approved after-state");
        const auto current=workspace_.snapshot_file(path,cancel);if(current.file_id!=file_id||current.content_sha256!=after_hash||current.content!=after_content)throw ToolContentConflict("Created file differs from its saved after-state; no restore proposal was created");
        inverse=WorkspaceRemovalPlan{current,workspace_.directory_identity(parent_directory(path),cancel)};
    }else if(action=="delete"){
        const auto before_hash=required_string(saved,"before_sha256"),before_content=required_string(saved,"before_content"),file_id=required_string(saved,"file_id");
        require_hash(before_content,before_hash,"deleted before-state");
        if(required_string(saved,"action")!="delete"||receipt.value("action",std::string{})!="delete")throw Conflict("Saved file operation is not a completed deletion");
        if(receipt.value("path",std::string{})!=path||receipt.value("removed_sha256",std::string{})!=before_hash||receipt.value("file_id",std::string{})!=file_id)throw Conflict("Saved removal receipt differs from its approved before-state");
        if(workspace_.fingerprint_optional_file(path,cancel).file)throw ToolContentConflict("Deleted path has been recreated; no restore proposal was created");
        inverse=workspace_.plan_creation(path,before_content,cancel,false);
    }else if(action=="move"){
        const auto destination=required_string(saved,"destination_path"),file_id=required_string(saved,"file_id"),before_hash=required_string(saved,"before_sha256"),after_hash=required_string(saved,"after_sha256");
        const auto before_content=required_string(saved,"before_content"),after_content=required_string(saved,"after_content");authorized_paths.push_back(destination);
        require_hash(before_content,before_hash,"move before-state");require_hash(after_content,after_hash,"move after-state");
        if(required_string(saved,"action")!="move"||receipt.value("action",std::string{})!="move")throw Conflict("Saved file operation is not a completed move");
        if(receipt.value("path",std::string{})!=destination||receipt.value("source_path",std::string{})!=path||receipt.value("file_id",std::string{})!=file_id||
           receipt.value("after_sha256",std::string{})!=after_hash||receipt.value("before_sha256",std::string{})!=before_hash)throw Conflict("Saved move receipt differs from its approved before/after state");
        const auto current=workspace_.snapshot_file(destination,cancel);if(current.file_id!=file_id||current.content_sha256!=after_hash||current.content!=after_content)throw ToolContentConflict("Moved file differs from its saved after-state; no restore proposal was created");
        if(workspace_.fingerprint_optional_file(path,cancel).file)throw ToolContentConflict("Original move path has been recreated; no restore proposal was created");
        const auto destination_plan=workspace_.plan_creation(path,before_content,cancel,false);
        inverse=WorkspaceMovePlan{current,workspace_.directory_identity(parent_directory(destination),cancel),destination_plan};
    }else throw std::invalid_argument("Saved file action cannot be restored");
    for(const auto& affected:authorized_paths)if(authorize)authorize(affected);
    WorkspacePatchPlan plan{workspace_.identity(),{std::move(inverse)}};std::vector<InstructionPrecondition> checks;
    if(guidance)checks.push_back(guidance(authorized_paths,cancel));
    auto report=Json::parse(PatchExecutor(store_,workspace_,owner_workspace_,folder_id_,workspace_revision_).execute(patch_id,run_id,std::move(plan),expiry,cancel,std::move(checks)));
    report["restored_operation_id"]=original_id;return report.dump();
}
}
