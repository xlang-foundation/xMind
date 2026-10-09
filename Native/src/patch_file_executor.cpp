#include "agentflow/patch_file_executor.hpp"
#include "nlohmann/json.hpp"
#include <type_traits>

namespace agentflow {
using Json=nlohmann::json;
OperationSpec PatchFileExecutor::proposal(const std::string& id,const std::string& run,
    const WorkspacePatchFilePlan& plan,const PatchFileIdentity& identity,const InstructionPrecondition& guidance) const {
    if(identity.patch_id.empty()||identity.patch_id.size()>192||identity.patch_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos||
       identity.file_count<1||identity.file_count>128||identity.index>=identity.file_count||id!=identity.patch_id+"-"+std::to_string(identity.index))throw std::invalid_argument("Invalid patch operation identity");
    guidance.validate();const auto workspace=workspace_.identity();
    Json payload={{"patch_id",identity.patch_id},{"patch_index",identity.index},{"patch_file_count",identity.file_count}};
    std::visit([&](const auto& file){
        using T=std::decay_t<decltype(file)>;
        if constexpr(std::is_same_v<T,WorkspaceCreatePlan>){
            if(file.parent_id.empty())throw std::invalid_argument("Patch creation requires a bound parent");
            if(file.workspace_id!=workspace)throw ToolAccessDenied("Patch creation belongs to another workspace");
            payload.update(Json{{"action","add"},{"path",file.path},{"parent_id",file.parent_id},{"before_exists",false},{"after_exists",true},{"before_content",""},{"after_content",file.content},{"after_sha256",file.content_sha256},{"create_directories",file.create_directories}});
        }else{
            if(file.parent_id.empty()||file.before.file_id.empty())throw std::invalid_argument("Patch source requires bound parent and file identities");
            if(file.before.workspace_id!=workspace)throw ToolAccessDenied("Patch source belongs to another workspace");
            payload.update(Json{{"path",file.before.path},{"parent_id",file.parent_id},{"file_id",file.before.file_id},{"before_exists",true},{"before_content",file.before.content},{"before_sha256",file.before.content_sha256}});
            if constexpr(std::is_same_v<T,WorkspaceEditPlan>)payload.update(Json{{"action","update"},{"after_exists",true},{"after_content",file.after_content},{"after_sha256",file.after_sha256}});
            else if constexpr(std::is_same_v<T,WorkspaceRemovalPlan>)payload.update(Json{{"action","delete"},{"after_exists",false},{"after_content",""}});
            else{
                if(file.destination.workspace_id!=workspace)throw ToolAccessDenied("Patch move destination belongs to another workspace");
                payload.update(Json{{"action","move"},{"after_exists",true},{"after_content",file.destination.content},{"after_sha256",file.destination.content_sha256},{"destination_path",file.destination.path},{"destination_parent_id",file.destination.parent_id},{"create_directories",file.destination.create_directories}});
            }
        }
    },plan);
    if(guidance.verify)payload["repository_guidance"]=Json::parse(guidance.metadata_json);
    auto source=payload.dump();if(source.size()>2*1024*1024)throw std::invalid_argument("Encoded patch proposal exceeds the durable operation limit");
    return {run,workspace,"patch_file",std::move(source)};
}
std::string PatchFileExecutor::execute(const std::string& id,const std::string& run,WorkspacePatchFilePlan plan,
    PatchFileIdentity identity,std::int64_t expiry,std::stop_token cancel,InstructionPrecondition guidance){
    const auto spec=proposal(id,run,plan,identity,guidance);
    PermissionWaiter(store_).acquire(id,spec,expiry,cancel);
    auto finish=[&](OperationState state,const std::string& result){
        try{store_.finish_operation(id,state,result).get();}
        catch(...){throw EditOutcomeUnrecorded("Patch file outcome was not recorded; claimed-effect recovery is required");}
    };
    if(guidance.verify){try{guidance.verify(cancel);}catch(...){finish(OperationState::failed,R"({"reason":"patch_guidance_changed_or_unavailable_before_effect"})");throw;}}
    std::string receipt;
    try{
        const auto actual=std::visit([&](const auto& file)->Json {
            using T=std::decay_t<decltype(file)>;
            if constexpr(std::is_same_v<T,WorkspaceRemovalPlan>){
                const auto removed=workspace_.apply_removal(file,cancel);
                return Json{{"action","delete"},{"path",removed.path},{"workspace_id",removed.workspace_id},{"file_id",removed.file_id},{"after_exists",false},{"removed_sha256",removed.removed_sha256},{"removed_size",removed.removed_size}};
            }else{
                WorkspaceSnapshot observed;std::string action;
                if constexpr(std::is_same_v<T,WorkspaceCreatePlan>){observed=workspace_.apply_creation(file,cancel);action="add";}
                else if constexpr(std::is_same_v<T,WorkspaceEditPlan>){observed=workspace_.apply_plan(file,cancel);action="update";}
                else {observed=workspace_.apply_move(file,cancel);action="move";}
                Json result={{"action",action},{"path",observed.path},{"workspace_id",observed.workspace_id},{"file_id",observed.file_id},{"after_exists",true},{"after_sha256",observed.content_sha256},{"size",observed.content.size()}};
                if constexpr(std::is_same_v<T,WorkspaceMovePlan>){result["source_path"]=file.before.path;result["before_sha256"]=file.before.content_sha256;result["created_directories"]=file.destination.create_directories;}
                else if constexpr(std::is_same_v<T,WorkspaceCreatePlan>)result["created_directories"]=file.create_directories;
                else result["before_sha256"]=file.before.content_sha256;
                return result;
            }
        },plan);
        Json result=actual;result["operation_id"]=id;result["patch_id"]=identity.patch_id;result["patch_index"]=identity.index;result["state"]="succeeded";receipt=result.dump();
    }catch(const ToolMutationUncertain&){finish(OperationState::uncertain,R"({"reason":"patch_file_effect_uncertain"})");throw;}
    catch(const ToolContentConflict&){finish(OperationState::failed,R"({"reason":"patch_file_precondition_conflict"})");throw;}
    catch(const ToolAccessDenied&){finish(OperationState::failed,R"({"reason":"patch_file_access_denied_before_effect"})");throw;}
    catch(const ToolFileError&){finish(OperationState::failed,R"({"reason":"patch_file_unavailable_before_effect"})");throw;}
    catch(const ToolCancelled&){finish(OperationState::failed,R"({"reason":"patch_file_cancelled_before_effect"})");throw;}
    catch(const std::invalid_argument&){finish(OperationState::failed,R"({"reason":"invalid_patch_file_before_effect"})");throw;}
    catch(...){finish(OperationState::uncertain,R"({"reason":"unexpected_patch_file_failure"})");throw ToolMutationUncertain("Patch file outcome requires reconciliation");}
    finish(OperationState::succeeded,receipt);return receipt;
}
}
