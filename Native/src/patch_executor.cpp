#include "agentflow/patch_executor.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <type_traits>
namespace agentflow {
using Json=nlohmann::json;
std::string PatchExecutor::execute(std::string patch_id,std::string run,WorkspacePatchPlan plan,
    std::int64_t expiry,std::stop_token cancel,std::vector<InstructionPrecondition> guidance){
    if(plan.workspace_id!=workspace_.identity())throw ToolAccessDenied("Patch batch belongs to another workspace");
    if(plan.files.empty()||plan.files.size()>128)throw std::invalid_argument("Invalid patch batch size");
    if(guidance.empty())guidance.resize(plan.files.size());if(guidance.size()!=plan.files.size())throw std::invalid_argument("Patch guidance count differs");
    PatchFileExecutor file_executor(store_,workspace_);Json manifest=Json::array();
    for(std::size_t index=0;index<plan.files.size();++index){
        const auto id=patch_id+"-"+std::to_string(index);
        auto spec=file_executor.proposal(id,run,plan.files[index],{patch_id,index,plan.files.size()},guidance[index]);const auto payload=Json::parse(spec.arguments_json);
        Json entry={{"index",index},{"operation_id",id},{"action",payload.at("action")},{"path",payload.at("path")}};
        for(const auto* key:{"destination_path","create_directories"})if(payload.contains(key))entry[key]=payload[key];manifest.push_back(entry);
        try{(void)store_.operation(id).get();throw Conflict("Patch operation identity was already used");}catch(const NotFound&){}
    }
    const auto manifest_source=manifest.dump();if(manifest_source.size()>65536)throw std::invalid_argument("Patch batch manifest exceeds bounds");
    std::size_t encoded_bytes=0;
    for(std::size_t index=0;index<plan.files.size();++index){
        auto spec=file_executor.proposal(patch_id+"-"+std::to_string(index),run,plan.files[index],{patch_id,index,plan.files.size(),manifest_source},guidance[index]);
        if(spec.arguments_json.size()>8*1024*1024-encoded_bytes)throw std::invalid_argument("Encoded patch batch review exceeds bounds");encoded_bytes+=spec.arguments_json.size();
    }
    std::vector<WorkspaceDirectoryBinding> owned_directories;
    auto owned=[&](const std::string& path)->const WorkspaceDirectoryBinding*{for(const auto& binding:owned_directories)if(WorkspaceTools::same_relative_path(binding.path,path))return &binding;return nullptr;};
    auto refresh_creation=[&](WorkspaceCreatePlan& creation){
        if(creation.create_directories.empty())return;
        const auto anchor=std::filesystem::u8path(creation.create_directories.front()).parent_path().generic_u8string();
        const std::string anchor_path=anchor.empty()?".":std::string(reinterpret_cast<const char*>(anchor.data()),anchor.size());
        if(workspace_.directory_identity(anchor_path,cancel)!=creation.parent_id)throw ToolContentConflict("Patch creation anchor changed");
        std::vector<std::string> remaining;bool missing=false;
        for(const auto& path:creation.create_directories){
            if(const auto* binding=owned(path)){
                if(missing||workspace_.directory_identity(path,cancel)!=binding->file_id)throw ToolContentConflict("Patch-owned directory changed");
            }else{missing=true;remaining.push_back(path);}
        }
        if(remaining.size()==creation.create_directories.size())return;
        auto refreshed=workspace_.plan_creation(creation.path,creation.content,cancel,true);
        if(refreshed.create_directories!=remaining)throw ToolContentConflict("Unowned directory changed patch creation preconditions");
        creation=std::move(refreshed);
    };
    std::string stop_reason;Json outcomes=Json::array(),not_requested=Json::array();std::size_t succeeded=0;bool uncertain=false;
    for(std::size_t index=0;index<plan.files.size();++index){
        if(cancel.stop_requested()){stop_reason="cancelled";break;}
        const auto id=patch_id+"-"+std::to_string(index);
        try{
            std::visit([&](auto& file){using T=std::decay_t<decltype(file)>;if constexpr(std::is_same_v<T,WorkspaceCreatePlan>)refresh_creation(file);else if constexpr(std::is_same_v<T,WorkspaceMovePlan>)refresh_creation(file.destination);},plan.files[index]);
            // Refreshed parents are disclosed in a new immutable per-file
            // proposal. Original requested paths/content remain in the manifest.
            (void)file_executor.execute(id,run,plan.files[index],{patch_id,index,plan.files.size(),manifest_source},expiry,cancel,guidance[index]);
        }catch(const EditOutcomeUnrecorded&){throw;}
        catch(const PermissionDenied&){stop_reason="permission_denied";}
        catch(const PermissionExpired&){stop_reason="permission_expired";}
        catch(const PermissionCancelled&){stop_reason="cancelled";}
        catch(const ToolGuidanceChanged&){stop_reason="guidance_changed";}
        catch(const ToolContentConflict&){stop_reason="content_conflict";}
        catch(const ToolAccessDenied&){stop_reason="access_denied";}
        catch(const ToolFileError&){stop_reason="file_unavailable";}
        catch(const ToolCancelled&){stop_reason="cancelled";}
        catch(const ToolMutationUncertain&){stop_reason="effect_uncertain";uncertain=true;}
        catch(const WorkspaceEffectUncertain&){stop_reason="workspace_effect_uncertain";}
        catch(const std::invalid_argument&){stop_reason="invalid_arguments";}
        catch(...){throw EditOutcomeUnrecorded("Patch coordination failed; inspect recorded per-file outcomes before further effects");}
        try{
            const auto operation=store_.operation(id).get();
            if(operation.spec.run_id!=run||operation.spec.workspace!=plan.workspace_id||operation.spec.tool!="patch_file")throw EditOutcomeUnrecorded("Patch operation ownership differs");
            if(operation.state==OperationState::executing||operation.state==OperationState::ready||operation.state==OperationState::awaiting_approval)throw EditOutcomeUnrecorded("Patch file remains unresolved");
            auto outcome=manifest[index];outcome["state"]=to_string(operation.state);outcome["result"]=Json::parse(operation.result_json);outcomes.push_back(outcome);
            if(operation.state==OperationState::succeeded){
                ++succeeded;for(const auto& directory:outcome["result"].value("created_directory_bindings",Json::array()))owned_directories.push_back({directory.at("path").get<std::string>(),directory.at("file_id").get<std::string>()});
            }else{uncertain|=operation.state==OperationState::uncertain;if(stop_reason.empty())stop_reason="recorded_file_failure";}
        }catch(const NotFound&){if(stop_reason.empty())throw EditOutcomeUnrecorded("Patch file produced no durable operation");}
        if(!stop_reason.empty())break;
    }
    for(const auto& entry:manifest){bool present=false;for(const auto& outcome:outcomes)if(outcome.at("operation_id")==entry.at("operation_id"))present=true;if(!present){
        try{(void)store_.operation(entry.at("operation_id").get<std::string>()).get();throw EditOutcomeUnrecorded("Unreported patch operation exists");}catch(const NotFound&){}
        not_requested.push_back(entry);
    }}
    Json report={{"patch_id",patch_id},{"atomic",false},{"requested_files",plan.files.size()},{"succeeded_files",succeeded},{"outcomes",outcomes},{"not_requested",not_requested},{"stop_reason",stop_reason},{"state",uncertain?"uncertain":succeeded==plan.files.size()?"succeeded":succeeded?"partial":"not_applied"}};
    const auto source=report.dump();if(source.size()>262144)throw EditOutcomeUnrecorded("Patch outcome exceeds result bounds; recorded file outcomes remain authoritative");return source;
}
}
