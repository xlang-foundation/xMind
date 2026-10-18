#include "agentflow/agent_file_tool_host.hpp"
#include "agentflow/create_executor.hpp"
#include "agentflow/patch_tool.hpp"
#include "agentflow/restore_executor.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <iomanip>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <type_traits>

namespace agentflow {
using Json=nlohmann::json;
bool is_agent_file_tool(const std::string& name){
    return name=="edit_file"||name=="create_file"||name=="apply_patch"||name=="restore_operation";
}
namespace {
std::string operation_id(){std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)value<<std::setw(8)<<random();return value.str();}
bool bounded_id(const std::string& value,std::size_t limit){return !value.empty()&&value.size()<=limit&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<0x21||c>0x7e;});}
AgentFileToolResult guidance_required(){return {false,R"({"error":{"code":"repository_instructions_required","message":"No requested action or approval proposal occurred. Updated repository or skill guidance will be supplied in the next model request; reconsider this call using it."}})"};}
class LocalFileToolHost final:public AgentFileToolHost {
public:
    LocalFileToolHost(PersistenceService& store,AgentFileToolOwnerContext context):store_(store),owner_(std::move(context)){
        if(!bounded_id(owner_.run_id,128)||!owner_.primary_workspace||!owner_.repository_context||owner_.approval_timeout.count()<=0||owner_.approval_timeout>std::chrono::hours(24))throw std::invalid_argument("Invalid backend file-tool owner");
        workspace_id_=owner_.primary_workspace->identity();
        if(owner_.workspace_members){
            if(owner_.workspace_members->workspace_id()!=workspace_id_||owner_.workspace_members->folders().empty()||owner_.workspace_members->folders().front().root!=owner_.primary_workspace->root_path())throw ToolAccessDenied("File-tool membership differs from its owner");
            revision_=owner_.workspace_members->revision();
        }
        for(const auto& name:owner_.allowed_tools)if(!is_agent_file_tool(name))throw std::invalid_argument("File-tool owner binds an unsupported tool");
        for(const auto& layer:owner_.permission_layers)validate_agent_permission_rules(layer);
    }
    AgentFileToolResult invoke(const AgentFileToolInvocation& call,std::stop_token cancel)override{
        if(cancel.stop_requested())throw ToolCancelled("File-tool invocation cancelled before preparation");
        // Match the existing model-stream value bound. Individual executors
        // retain their tighter file/patch limits, including each folder group.
        if(call.protocol_version!=1||!bounded_id(call.attempt_id,128)||call.call_id.empty()||call.call_id.size()>256||call.call_id.find('\0')!=std::string::npos||call.arguments_json.size()>4*1024*1024)throw std::invalid_argument("Invalid file-tool invocation envelope");
        if(call.run_id!=owner_.run_id||call.workspace_id!=workspace_id_||call.workspace_revision!=revision_)throw ToolAccessDenied("File-tool invocation differs from its admitted owner");
        if(!is_agent_file_tool(call.tool_name)||std::find(owner_.allowed_tools.begin(),owner_.allowed_tools.end(),call.tool_name)==owner_.allowed_tools.end())throw ToolAccessDenied("File tool is outside the admitted catalogue");
        if(store_.run(owner_.run_id).get().state!=RunState::running)throw ToolAccessDenied("File-tool owner is no longer running");
        std::size_t index=0;WorkspaceTools* workspace=owner_.primary_workspace;
        if(owner_.workspace_members){index=owner_.workspace_members->folder_index(call.folder_id);workspace=&owner_.workspace_members->folder_tools(index);}
        else if(call.folder_id!=workspace_id_)throw ToolAccessDenied("File-tool folder is outside its admitted membership");
        Json args;try{args=Json::parse(mcp_compact_object(call.arguments_json));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid scoped file-tool JSON");}
        if(args.contains("folder_id")&&(!args["folder_id"].is_string()||args["folder_id"].get<std::string>()!=call.folder_id))throw ToolAccessDenied("File-tool folder differs from its arguments");
        if(index!=0&&!args.contains("folder_id"))throw ToolAccessDenied("Attached file-tool folder requires explicit selection");
        // The owner rejects a second dispatch of one model call. This local
        // lifetime guard does not replace future durable worker RPC admission.
        {std::lock_guard lock(calls_mutex_);
            // AgentRunner permits at most 128 turns and 64 calls per turn.
            if(calls_.size()>=128*64)throw std::invalid_argument("File-tool owner call limit exceeded");
            if(!calls_.insert({call.attempt_id,call.call_id}).second)throw ToolAccessDenied("File-tool call has already entered this owner");
        }
        std::string resource="*";const auto* field=call.tool_name=="restore_operation"?"operation_id":"path";
        if(args.contains(field)&&args[field].is_string()){resource=args[field].get<std::string>();std::replace(resource.begin(),resource.end(),'\\','/');while(resource.starts_with("./"))resource.erase(0,2);if(resource.empty())resource=".";}
        authorize(call.tool_name,resource);
        auto* context=owner_.repository_context(index);if(!context)throw ToolAccessDenied("File-tool folder has no admitted instruction context");
        bool ready=context->skills().ready(cancel);InstructionPrecondition guidance;
        if(call.tool_name=="edit_file"||call.tool_name=="create_file"){
            if(!args.contains("path")||!args["path"].is_string())throw std::invalid_argument("Scoped file tool requires a path");
            auto scope=args["path"].get<std::string>();
            if(call.tool_name=="create_file"&&args.contains("create_parents")&&!args["create_parents"].is_boolean())throw std::invalid_argument("Invalid creation parent option");
            scope=call.tool_name=="create_file"&&args.value("create_parents",false)?workspace->creation_directory(scope,cancel):RepositoryInstructionContext::file_directory(scope);
            ready=ready&&context->ready(scope,cancel);if(ready)guidance=context->precondition(scope);
        }
        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+owner_.approval_timeout.count();
        if(call.tool_name=="apply_patch")return patch(call,args,index,*workspace,*context,ready,expiry,cancel);
        if(!ready)return guidance_required();
        if(call.tool_name=="edit_file")return {true,EditExecutor(store_,*workspace,workspace_id_,call.folder_id,revision_).invoke(operation_id(),owner_.run_id,call.arguments_json,expiry,cancel,std::move(guidance))};
        if(call.tool_name=="create_file")return {true,CreateExecutor(store_,*workspace,workspace_id_,call.folder_id,revision_).invoke(operation_id(),owner_.run_id,call.arguments_json,expiry,cancel,std::move(guidance))};
        const auto path_authorizer=[&](const std::string& path){authorize("apply_patch",path);};
        const auto make_guidance=[&](const std::vector<std::string>& paths,std::stop_token token){
            std::vector<InstructionPrecondition> conditions;
            for(const auto& path:paths){const auto directory=workspace->creation_directory(path,token);if(!context->ready(directory,token))throw ToolGuidanceChanged("Updated repository guidance must reach the model before restoring this change");conditions.push_back(context->precondition(directory));}
            if(conditions.size()==1)return conditions.front();Json metadata=Json::array();for(const auto& condition:conditions)metadata.push_back(Json::parse(condition.metadata_json));
            InstructionPrecondition combined{Json{{"version",1},{"conditions",metadata}}.dump(),[conditions=std::move(conditions)](std::stop_token token){for(const auto& condition:conditions)if(condition.verify)condition.verify(token);}};combined.validate();return combined;
        };
        return {true,RestoreExecutor(store_,*workspace,workspace_id_,call.folder_id,revision_).invoke(operation_id(),owner_.run_id,call.arguments_json,expiry,cancel,path_authorizer,make_guidance)};
    }
private:
    void authorize(const std::string& tool,const std::string& resource)const{
        for(const auto& layer:owner_.permission_layers)if(agent_permission_denies(layer,tool,resource))throw AgentPermissionDenied("Selected agent policy denied this file-tool resource");
    }
    void authorize(const PreparedPatch& patch)const{
        for(const auto& file:patch.plan.files)std::visit([&](const auto& plan){using T=std::decay_t<decltype(plan)>;
            if constexpr(std::is_same_v<T,WorkspaceCreatePlan>)authorize("apply_patch",plan.path);
            else if constexpr(std::is_same_v<T,WorkspaceMovePlan>){authorize("apply_patch",plan.before.path);authorize("apply_patch",plan.destination.path);}
            else authorize("apply_patch",plan.before.path);
        },file);
    }
    AgentFileToolResult patch(const AgentFileToolInvocation& call,const Json& args,std::size_t index,WorkspaceTools& workspace,RepositoryInstructionContext& context,bool ready,std::int64_t expiry,std::stop_token cancel){
        struct FolderPatch{std::size_t index;std::string id;PreparedPatch patch;};
        std::vector<FolderPatch> groups;std::optional<PreparedPatch> single;
        if(args.contains("patches")){
            if(!owner_.workspace_members||args.size()!=1||!args["patches"].is_array()||args["patches"].empty()||args["patches"].size()>16)throw std::invalid_argument("Invalid multi-folder patch request");
            std::set<std::size_t> seen;
            for(const auto& item:args["patches"]){
                if(!item.is_object()||item.size()<2||item.size()>3||!item.contains("folder_id")||!item["folder_id"].is_string()||!item.contains("patch_text")||!item["patch_text"].is_string()||(item.contains("create_parents")&&!item["create_parents"].is_boolean()))throw std::invalid_argument("Invalid multi-folder patch entry");
                for(auto field=item.begin();field!=item.end();++field)if(field.key()!="folder_id"&&field.key()!="patch_text"&&field.key()!="create_parents")throw std::invalid_argument("Unsupported multi-folder patch field");
                const auto folder=owner_.workspace_members->folder_index(item["folder_id"].get<std::string>());if(!seen.insert(folder).second)throw std::invalid_argument("Multi-folder patches must combine each folder into one entry");
                const auto folder_id=owner_.workspace_members->folders()[folder].folder_id;auto* folder_context=owner_.repository_context(folder);if(!folder_context)throw ToolAccessDenied("Patch folder has no admitted instruction context");
                Json input={{"folder_id",folder_id},{"patch_text",item["patch_text"]}};if(item.contains("create_parents"))input["create_parents"]=item["create_parents"];
                auto prepared=PatchTool(store_,owner_.workspace_members->folder_tools(folder),*folder_context,workspace_id_,folder_id,revision_,folder==0).prepare(input.dump(),cancel);
                if(!prepared){ready=false;break;}groups.push_back({folder,folder_id,std::move(*prepared)});
            }
            std::size_t files=0;for(const auto& group:groups){if(group.patch.plan.files.size()>128-files)throw std::invalid_argument("Cross-folder patch exceeds the 128-file batch limit");files+=group.patch.plan.files.size();authorize(group.patch);}
            ready=ready&&!groups.empty()&&groups.size()==args["patches"].size();
        }else{
            single=PatchTool(store_,workspace,context,workspace_id_,call.folder_id,revision_,index==0).prepare(call.arguments_json,cancel);ready=single.has_value()&&ready;if(single)authorize(*single);
        }
        if(!ready)return guidance_required();
        if(single){auto result=PatchTool(store_,workspace,context,workspace_id_,call.folder_id,revision_,index==0).execute(operation_id(),owner_.run_id,std::move(*single),expiry,cancel);const bool success=Json::parse(result).value("state",std::string{})=="succeeded";return {success,std::move(result)};}
        const auto patch_id=operation_id();Json results=Json::array(),not_requested=Json::array();std::size_t requested=0,succeeded=0,processed=0;std::string stop_reason;
        for(const auto& group:groups)requested+=group.patch.plan.files.size();
        for(std::size_t i=0;i<groups.size();++i){auto& group=groups[i];auto* folder_context=owner_.repository_context(group.index);if(!folder_context)throw ToolAccessDenied("Patch folder has no admitted instruction context");
            const auto report=Json::parse(PatchTool(store_,owner_.workspace_members->folder_tools(group.index),*folder_context,workspace_id_,group.id,revision_,group.index==0).execute(patch_id+"-"+std::to_string(i),owner_.run_id,std::move(group.patch),expiry,cancel));++processed;succeeded+=report.value("succeeded_files",std::size_t{});results.push_back(Json{{"folder_id",group.id},{"report",report}});
            if(report.value("state",std::string{})!="succeeded"){stop_reason=report.value("stop_reason",std::string{"folder_patch_incomplete"});for(std::size_t pending=i+1;pending<groups.size();++pending)not_requested.push_back(Json{{"folder_id",groups[pending].id},{"group_index",pending}});break;}
        }
        const auto state=succeeded==requested&&processed==groups.size()?std::string("succeeded"):succeeded?std::string("partial"):std::string("not_applied");
        return {state=="succeeded",Json{{"patch_id",patch_id},{"atomic",false},{"requested_folders",groups.size()},{"processed_folders",processed},{"requested_files",requested},{"succeeded_files",succeeded},{"folder_results",results},{"not_requested_folders",not_requested},{"stop_reason",stop_reason},{"state",state}}.dump()};
    }
    PersistenceService& store_;AgentFileToolOwnerContext owner_;std::string workspace_id_;std::int64_t revision_=1;
    std::mutex calls_mutex_;
    std::set<std::pair<std::string,std::string>> calls_;
};
}
std::unique_ptr<AgentFileToolHost> make_agent_file_tool_host(PersistenceService& store,AgentFileToolOwnerContext context){return std::make_unique<LocalFileToolHost>(store,std::move(context));}
}
