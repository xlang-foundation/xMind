#include "agentflow/agent_read_tool_host.hpp"
#include "agentflow/agent_run_journal.hpp"
#include "agentflow/agent_permission_grants.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <iomanip>
#include <mutex>
#include <random>
#include <set>
#include <sstream>

namespace agentflow {
namespace {
using Json=nlohmann::json;
std::string operation_id(){std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)value<<std::setw(8)<<random();return value.str();}
std::string guidance_required(){return Json{{"error",{{"code","repository_instructions_required"},{"message","No workspace read occurred. Updated repository or skill guidance will be supplied in the next model request; reconsider this call using it."}}}}.dump();}
class LocalReadToolHost final:public AgentReadToolHost {
public:
    LocalReadToolHost(AgentRunJournal& journal,PersistenceService& store,AgentReadToolOwnerContext owner)
        :journal_(journal),store_(store),owner_(std::move(owner)) {
        if(owner_.run_id.empty()||owner_.run_id.size()>256||owner_.run_id.find('\0')!=std::string::npos||!owner_.primary_workspace||!owner_.repository_context||!owner_.offered_tool||owner_.approval_timeout.count()<=0||owner_.approval_timeout>std::chrono::hours(24))throw std::invalid_argument("Invalid workspace read owner");
        workspace_id_=owner_.primary_workspace->identity();
        if(owner_.workspace_members){
            if(owner_.workspace_members->workspace_id()!=workspace_id_||owner_.workspace_members->folders().empty()||owner_.workspace_members->folders().front().root!=owner_.primary_workspace->root_path())throw ToolAccessDenied("Workspace read membership differs from its owner");
            revision_=owner_.workspace_members->revision();
        }
        for(const auto& layer:owner_.inherited_permission_layers)validate_agent_permission_rules(layer);
        validate_agent_permission_rules(owner_.permission_rules);
        policy_binding_=agent_permission_policy_binding(owner_.inherited_permission_layers,owner_.permission_rules);
    }
    AgentReadToolResult invoke(const AgentWorkspaceInvocation& call,std::stop_token cancel)override {
        // Reuse the complete versioned envelope validator before any approval,
        // grant lookup or filesystem read. Its wire values contain no authority.
        (void)call.serialize();
        if(cancel.stop_requested()||std::chrono::steady_clock::now()>=owner_.deadline)throw ToolCancelled("Workspace read owner cancelled or expired");
        if(call.run_id!=owner_.run_id||call.workspace_id!=workspace_id_||call.workspace_revision!=revision_)throw ToolAccessDenied("Workspace read differs from its admitted owner");
        if(!is_agent_read_tool(call.tool_name)||!owner_.offered_tool(call.tool_name))throw ToolAccessDenied("Workspace read is outside the admitted catalogue");
        if(call.tool_name=="language_server"&&!owner_.language_server_query)throw ToolAccessDenied("Language-server owner is not admitted for this run");
        const auto run=journal_.run(owner_.run_id).get();
        if(run.state!=RunState::running)throw ToolAccessDenied("Workspace read owner is no longer running");
        Json args;try{args=Json::parse(mcp_compact_object(call.arguments_json));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid workspace read arguments");}
        std::size_t index=0;
        if(owner_.workspace_members)index=owner_.workspace_members->folder_index(call.folder_id);
        else if(call.folder_id!=workspace_id_)throw ToolAccessDenied("Workspace read folder is not admitted");
        if(args.contains("folder_id")&&(!args["folder_id"].is_string()||args["folder_id"].get<std::string>()!=call.folder_id))throw ToolAccessDenied("Workspace read folder differs from its arguments");
        if(index!=0&&!args.contains("folder_id"))throw ToolAccessDenied("Attached workspace read requires explicit folder selection");
        {
            std::lock_guard lock(calls_mutex_);
            if(calls_.size()>=128*64||!calls_.emplace(call.attempt_id,call.call_id).second)throw ToolAccessDenied("Workspace read call identity is already used or exhausted");
        }
        std::string resource="*";
        if(args.contains("path")&&args["path"].is_string()){
            resource=args["path"].get<std::string>();std::replace(resource.begin(),resource.end(),'\\','/');
            while(resource.starts_with("./"))resource.erase(0,2);
            if(resource.empty())resource=".";
        }
        if(resource=="*"&&(call.tool_name=="search_files"||call.tool_name=="find_files"||call.tool_name=="glob_files"))resource=".";
        const bool enumerates=call.tool_name!="read_file"&&call.tool_name!="language_server";
        bool asks=false;
        const auto check_policy=[&](const std::vector<AgentPermissionRule>& rules){
            if(agent_permission_denies(rules,call.tool_name,resource)||(enumerates&&agent_permission_denies_directory_enumeration(rules,call.tool_name,resource)))throw AgentPermissionDenied("Selected agent denied workspace read resources");
            asks=asks||agent_permission_effect(rules,call.tool_name,resource)==AgentPermissionEffect::ask||(enumerates&&agent_permission_asks_directory_enumeration(rules,call.tool_name,resource));
        };
        for(const auto& layer:owner_.inherited_permission_layers)check_policy(layer);
        check_policy(owner_.permission_rules);
        auto* context=owner_.repository_context(index);
        const auto ready=[&](){
            if(!context)return true;
            if(!context->skills().ready(cancel))return false;
            if(call.tool_name!="read_file"&&call.tool_name!="list_files"&&call.tool_name!="language_server")return true;
            if(!args.contains("path")||!args["path"].is_string())throw std::invalid_argument("Scoped workspace read requires a path");
            auto scope=args["path"].get<std::string>();if(call.tool_name!="list_files")scope=RepositoryInstructionContext::file_directory(scope);
            return context->ready(scope,cancel);
        };
        if(!ready())return {false,guidance_required()};
        const auto read=[&](){
            if(call.tool_name=="language_server")return owner_.language_server_query(call,cancel);
            return owner_.workspace_members?owner_.workspace_members->invoke(call,cancel):owner_.primary_workspace->invoke(call,cancel);
        };
        if(!asks)return {true,read()};
        auto* workspace=owner_.workspace_members?&owner_.workspace_members->folder_tools(index):owner_.primary_workspace;
        const auto pending_id=operation_id(),normalized=detail::agent_permission_normalize_resource(resource);
        AgentPermissionGrantStore grants(store_);
        const bool reusable=grants.allows(run.session_id,workspace->identity(),policy_binding_,call.tool_name,normalized);
        if(!reusable)grants.record_request({pending_id,run.session_id,workspace->identity(),policy_binding_,call.tool_name,normalized,agent_permission_grant_identifier(workspace->identity(),policy_binding_,call.tool_name,normalized)});
        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+owner_.approval_timeout.count();
        auto approved=PermissionWaiter(journal_).acquire(pending_id,{owner_.run_id,workspace->identity(),call.tool_name,call.arguments_json,{}},expiry,cancel,reusable?"agent-permission-grant":std::string{});
        try {
            // Guidance may change while a controller is deciding. Claiming a
            // read approval must not reveal bytes under superseded guidance.
            if(!ready()){auto output=guidance_required();journal_.finish_operation(approved.id,OperationState::failed,output).get();return {false,std::move(output)};}
            auto output=read();journal_.finish_operation(approved.id,OperationState::succeeded,output).get();return {true,std::move(output)};
        }catch(...){journal_.finish_operation(approved.id,OperationState::failed,R"({"reason":"permission_asked_read_failed"})").get();throw;}
    }
private:
    AgentRunJournal& journal_;PersistenceService& store_;AgentReadToolOwnerContext owner_;
    std::string workspace_id_,policy_binding_;std::int64_t revision_=1;
    std::mutex calls_mutex_;std::set<std::pair<std::string,std::string>> calls_;
};
}
bool is_agent_read_tool(const std::string& name){return name=="read_file"||name=="list_files"||name=="search_files"||name=="find_files"||name=="glob_files"||name=="language_server";}
std::unique_ptr<AgentReadToolHost> make_agent_read_tool_host(AgentRunJournal& journal,PersistenceService& store,AgentReadToolOwnerContext owner){return std::make_unique<LocalReadToolHost>(journal,store,std::move(owner));}
}
