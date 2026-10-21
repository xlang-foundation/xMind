#include "agentflow/agent_process_tool_host.hpp"
#include "agentflow/process_executor.hpp"
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
bool bounded_id(const std::string& value,std::size_t maximum){return !value.empty()&&value.size()<=maximum&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<0x21||c>0x7e;});}
std::string operation_id(){std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)value<<std::setw(8)<<random();return value.str();}
class LocalProcessToolHost final:public AgentProcessOwnerHost {
public:
    LocalProcessToolHost(PersistenceService& store,AgentProcessToolOwnerContext owner):store_(store),owner_(std::move(owner)){
        if(!bounded_id(owner_.run_id,128)||!owner_.process||!owner_.primary_workspace||!owner_.repository_context||!owner_.offered_tool||owner_.approval_timeout.count()<=0||owner_.approval_timeout>std::chrono::hours(24))throw std::invalid_argument("Invalid backend process-tool owner");
        workspace_id_=owner_.primary_workspace->identity();
        if(owner_.workspace_members){
            if(owner_.workspace_members->workspace_id()!=workspace_id_||owner_.workspace_members->folders().empty()||owner_.workspace_members->folders().front().root!=owner_.primary_workspace->root_path())throw ToolAccessDenied("Process-tool membership differs from its owner");
            revision_=owner_.workspace_members->revision();
        }
        for(const auto& layer:owner_.permission_layers)validate_agent_permission_rules(layer);
    }
    AgentProcessToolResult invoke(const AgentProcessToolInvocation& call,std::stop_token cancel)override{
        if(cancel.stop_requested())throw ToolCancelled("Process-tool invocation cancelled before preparation");
        validate_agent_process_tool_invocation(call);
        if(call.run_id!=owner_.run_id||call.workspace_id!=workspace_id_||call.workspace_revision!=revision_)throw ToolAccessDenied("Process-tool invocation differs from its admitted owner");
        if(!is_agent_process_tool(call.tool_name)||!owner_.offered_tool(call.tool_name))throw ToolAccessDenied("Process tool is outside the admitted catalogue");
        if(store_.run(owner_.run_id).get().state!=RunState::running)throw ToolAccessDenied("Process-tool owner is no longer running");
        if(std::chrono::steady_clock::now()>=owner_.deadline)throw ProcessBeforeDispatchError("Process-tool owner deadline elapsed before preparation");
        Json args;try{args=Json::parse(mcp_compact_object(call.arguments_json));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid process-tool JSON arguments");}
        std::size_t index=0;
        if(call.tool_name=="run_process"){
            if(owner_.workspace_members)index=owner_.workspace_members->folder_index(call.folder_id);
            else if(call.folder_id!=workspace_id_)throw ToolAccessDenied("Process-tool folder is outside its admitted membership");
            if(args.contains("folder_id")&&(!args["folder_id"].is_string()||args["folder_id"].get<std::string>()!=call.folder_id))throw ToolAccessDenied("Process-tool folder differs from its arguments");
            if(index!=0&&!args.contains("folder_id"))throw ToolAccessDenied("Attached process-tool folder requires explicit selection");
        }else if(call.folder_id!=workspace_id_)throw ToolAccessDenied("Process observation must bind the owning primary workspace");
        // Guard admission before approval can suspend this call. A lost reply
        // cannot re-enter the same local host; durable worker fencing is separate.
        {std::lock_guard lock(calls_mutex_);if(calls_.size()>=128*64)throw std::invalid_argument("Process-tool owner call limit exceeded");if(!calls_.insert({call.attempt_id,call.call_id}).second)throw ToolAccessDenied("Process-tool call has already entered this owner");}
        for(const auto& layer:owner_.permission_layers)if(agent_permission_denies(layer,call.tool_name,"*"))throw AgentPermissionDenied("Selected agent policy denied this process tool");
        auto* context=owner_.repository_context(index);if(!context)throw ToolAccessDenied("Process-tool folder has no admitted instruction context");
        bool ready=context->skills().ready(cancel);InstructionPrecondition guidance;
        if(call.tool_name=="run_process"){
            std::string scope=".";if(args.contains("workdir")){if(!args["workdir"].is_string())throw std::invalid_argument("Invalid process-tool working directory");scope=args["workdir"].get<std::string>();}
            ready=ready&&context->ready(scope,cancel);if(ready)guidance=context->precondition(scope);
        }
        if(!ready)return {false,R"({"error":{"code":"repository_instructions_required","message":"No requested action or approval proposal occurred. Updated repository or skill guidance will be supplied in the next model request; reconsider this call using it."}})"};
        if(call.tool_name=="run_process"){
            if(owner_.before_effect)guidance.before_effect=[check=owner_.before_effect,call](const std::string& operation,std::stop_token token){check(call,operation,token);};
            AgentProcessInvocation invocation;invocation.operation_id=operation_id();invocation.run_id=owner_.run_id;invocation.workspace_id=workspace_id_;invocation.workspace_revision=revision_;invocation.folder_id=call.folder_id;invocation.arguments_json=call.arguments_json;invocation.allow_background=owner_.allow_background;invocation.background_cancel=owner_.background_cancel;
            invocation.expires_unix_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+owner_.approval_timeout.count();
            return {true,owner_.process->invoke(invocation,cancel,std::move(guidance))};
        }
        if(owner_.before_effect){if(!args.contains("job_id")||!args.at("job_id").is_string())throw std::invalid_argument("Process control requires a captured job ID");owner_.before_effect(call,args.at("job_id").get<std::string>(),cancel);}
        auto result=call.tool_name=="wait_process"?owner_.process->wait(owner_.run_id,call.arguments_json,cancel):owner_.process->stop(owner_.run_id,call.arguments_json,cancel);
        const auto state=Json::parse(result).value("state",std::string{});
        return {state!="uncertain"&&(call.tool_name!="stop_process"||state!="stopping"),std::move(result)};
    }
private:
    PersistenceService& store_;AgentProcessToolOwnerContext owner_;std::string workspace_id_;std::int64_t revision_=1;
    std::mutex calls_mutex_;std::set<std::pair<std::string,std::string>> calls_;
};
}
std::unique_ptr<AgentProcessOwnerHost> make_agent_process_tool_host(PersistenceService& store,AgentProcessToolOwnerContext owner){return std::make_unique<LocalProcessToolHost>(store,std::move(owner));}
}
