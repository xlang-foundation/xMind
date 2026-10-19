#include "agentflow/agent_guidance_tool_host.hpp"
#include "agentflow/agent_run_journal.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <mutex>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
class LocalGuidanceToolHost final:public AgentGuidanceToolHost {
public:
    LocalGuidanceToolHost(AgentRunJournal& journal,AgentGuidanceToolOwnerContext owner)
        :journal_(journal),owner_(std::move(owner)) {
        if(owner_.run_id.empty()||owner_.run_id.size()>256||owner_.run_id.find('\0')!=std::string::npos||!owner_.primary_workspace||!owner_.repository_context||!owner_.offered_tool||owner_.base_instruction_bytes>65536)
            throw std::invalid_argument("Invalid guidance tool owner");
        workspace_id_=owner_.primary_workspace->identity();
        if(owner_.workspace_members){
            if(owner_.workspace_members->workspace_id()!=workspace_id_||owner_.workspace_members->folders().empty()||owner_.workspace_members->folders().front().root!=owner_.primary_workspace->root_path())
                throw ToolAccessDenied("Guidance membership differs from its owner");
            revision_=owner_.workspace_members->revision();
        }
        for(const auto& layer:owner_.inherited_permission_layers)validate_agent_permission_rules(layer);
        validate_agent_permission_rules(owner_.permission_rules);
    }
    AgentGuidanceToolResult invoke(const AgentWorkspaceInvocation& call,std::stop_token cancel)override {
        (void)call.serialize();
        if(cancel.stop_requested()||std::chrono::steady_clock::now()>=owner_.deadline)throw ToolCancelled("Guidance owner cancelled or expired");
        if(call.run_id!=owner_.run_id||call.workspace_id!=workspace_id_||call.workspace_revision!=revision_)
            throw ToolAccessDenied("Guidance differs from its admitted owner");
        if(!is_agent_guidance_tool(call.tool_name)||!owner_.offered_tool(call.tool_name))throw ToolAccessDenied("Guidance is outside the admitted catalogue");
        if(journal_.run(owner_.run_id).get().state!=RunState::running)throw ToolAccessDenied("Guidance owner is no longer running");
        Json args;
        try{args=Json::parse(mcp_compact_object(call.arguments_json));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid guidance arguments");}
        std::size_t index=0;
        if(owner_.workspace_members)index=owner_.workspace_members->folder_index(call.folder_id);
        else if(call.folder_id!=workspace_id_)throw ToolAccessDenied("Guidance folder is not admitted");
        if(args.contains("folder_id")&&(!args["folder_id"].is_string()||args["folder_id"].get<std::string>()!=call.folder_id))throw ToolAccessDenied("Guidance folder differs from its arguments");
        if(index!=0&&(call.tool_name!="read_repository_instructions"||!args.contains("folder_id")))throw ToolAccessDenied("This guidance tool does not admit an attached folder");
        {
            std::lock_guard lock(calls_mutex_);
            if(calls_.size()>=128*64||!calls_.emplace(call.attempt_id,call.call_id).second)throw ToolAccessDenied("Guidance call identity is already used or exhausted");
        }
        // Guidance tools retain the existing whole-tool denial semantics.
        // They neither grant effects nor create reusable read permissions.
        for(const auto& layer:owner_.inherited_permission_layers)
            if(agent_permission_denies(layer,call.tool_name,"*"))throw AgentPermissionDenied("Inherited policy denied guidance");
        if(agent_permission_denies(owner_.permission_rules,call.tool_name,"*"))throw AgentPermissionDenied("Selected policy denied guidance");
        auto* context=owner_.repository_context(index);
        if(!context)throw ToolAccessDenied("Guidance context is not admitted");
        if(call.tool_name=="read_repository_instructions"){
            if(!args.contains("directory")||!args["directory"].is_string())throw std::invalid_argument("Repository guidance requires a directory");
            auto output=owner_.workspace_members?owner_.workspace_members->invoke(call,cancel):owner_.primary_workspace->invoke(call,cancel);
            // Reading guidance is itself discovery. Register its selected scope
            // for delivery in the next model request without requiring that
            // same guidance to have already reached an earlier model request.
            context->request_scope(args["directory"].get<std::string>(),cancel);
            return {true,std::move(output)};
        }
        if(call.tool_name=="list_skills"){
            if(!args.empty())throw std::invalid_argument("Skill listing takes no arguments");
            return {true,context->skills().catalogue_json(cancel)};
        }
        if(!context->ready(context->skills().activation_directory(call.arguments_json,cancel),cancel))
            return {false,R"({"error":{"code":"repository_instructions_required","message":"No requested action or approval proposal occurred. Updated repository or skill guidance will be supplied in the next model request; reconsider this call using it."}})"};
        return {true,context->activate_skill(call.arguments_json,owner_.base_instruction_bytes,cancel)};
    }
private:
    AgentRunJournal& journal_;
    AgentGuidanceToolOwnerContext owner_;
    std::string workspace_id_;
    std::int64_t revision_=1;
    std::mutex calls_mutex_;
    std::set<std::pair<std::string,std::string>> calls_;
};
}
bool is_agent_guidance_tool(const std::string& name){return name=="read_repository_instructions"||name=="list_skills"||name=="load_skill";}
std::unique_ptr<AgentGuidanceToolHost> make_agent_guidance_tool_host(AgentRunJournal& journal,AgentGuidanceToolOwnerContext owner){return std::make_unique<LocalGuidanceToolHost>(journal,std::move(owner));}
}
