#include "agentflow/agent_remote_tool_host.hpp"
#include "agentflow/agent_run_journal.hpp"
#include "agentflow/a2a_tool_registry.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/repository_instruction_context.hpp"
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
bool bounded_id(const std::string& value,std::size_t limit){return !value.empty()&&value.size()<=limit&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<0x21||c>0x7e;});}
std::string operation_id(){std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)value<<std::setw(8)<<random();return value.str();}
class LocalRemoteToolHost final:public AgentRemoteToolHost {
public:
    LocalRemoteToolHost(PersistenceService& store,AgentRunJournal& journal,AgentRemoteToolOwnerContext owner):store_(store),journal_(journal),owner_(std::move(owner)){
        if(!bounded_id(owner_.run_id,128)||!owner_.primary_workspace||(!owner_.repository_context&&!owner_.repository_context_for_call)||!owner_.offered_tool||!owner_.mcp_registry||!owner_.a2a_registry||owner_.approval_timeout.count()<=0||owner_.approval_timeout>std::chrono::hours(24))throw std::invalid_argument("Invalid backend remote-tool owner");
        workspace_id_=owner_.primary_workspace->identity();
        if(owner_.workspace_members){
            if(owner_.workspace_members->workspace_id()!=workspace_id_||owner_.workspace_members->folders().empty()||owner_.workspace_members->folders().front().root!=owner_.primary_workspace->root_path())throw ToolAccessDenied("Remote-tool membership differs from its owner");
            revision_=owner_.workspace_members->revision();
        }
        for(const auto& layer:owner_.permission_layers)validate_agent_permission_rules(layer);
    }
    AgentRemoteToolResult invoke(const AgentRemoteToolInvocation& call,std::stop_token cancel)override{
        if(cancel.stop_requested())throw ToolCancelled("Remote-tool invocation cancelled before preparation");
        validate_agent_remote_tool_invocation(call);
        if(call.run_id!=owner_.run_id||call.workspace_id!=workspace_id_||call.workspace_revision!=revision_)throw ToolAccessDenied("Remote-tool invocation differs from its admitted owner");
        if(!owner_.offered_tool(call.tool_name))throw ToolAccessDenied("Remote tool is outside the admitted catalogue");
        auto* mcp=owner_.mcp_registry(call.tool_name);auto* a2a=owner_.a2a_registry(call.tool_name);
        if((mcp==nullptr)==(a2a==nullptr))throw ToolAccessDenied("Remote tool does not resolve to one owner registry");
        if(store_.run(owner_.run_id).get().state!=RunState::running)throw ToolAccessDenied("Remote-tool owner is no longer running");
        if(std::chrono::steady_clock::now()>=owner_.deadline){if(mcp)throw McpEffectNotDispatched("MCP owner deadline elapsed before preparation");throw A2aEffectNotDispatched("A2A owner deadline elapsed before preparation");}
        try{(void)mcp_compact_object(call.arguments_json);}catch(const McpProtocolError&){throw std::invalid_argument("Invalid remote-tool JSON arguments");}
        // Lifetime guard only: uncertain/failed calls are not eligible for a
        // second dispatch under the same model-call identity. A future worker
        // protocol additionally needs durable admission and fencing.
        {std::lock_guard lock(calls_mutex_);
            if(calls_.size()>=128*64)throw std::invalid_argument("Remote-tool owner call limit exceeded");
            if(!calls_.insert({call.attempt_id,call.call_id}).second)throw ToolAccessDenied("Remote-tool call has already entered this owner");
        }
        for(const auto& layer:owner_.permission_layers)if(agent_permission_denies(layer,call.tool_name,"*"))throw AgentPermissionDenied("Selected agent policy denied this remote tool");
        auto* current_context=owner_.repository_context_for_call?owner_.repository_context_for_call():owner_.repository_context;
        if(!current_context)throw ToolAccessDenied("Remote tool has no current native repository guidance");
        auto& context=*current_context;
        if(!context.skills().ready(cancel)||!context.ready(".",cancel))return {false,R"({"error":{"code":"repository_instructions_required","message":"No requested action or approval proposal occurred. Updated repository or skill guidance will be supplied in the next model request; reconsider this call using it."}})"};
        auto guidance=context.precondition(".");
        if(owner_.before_effect)guidance.before_effect=[authority=owner_.before_effect,call](const std::string& id,std::stop_token stop){authority(call,id,stop);};
        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+owner_.approval_timeout.count();
        const auto operation=operation_id();
        if(mcp)return {true,mcp->invoke(operation,owner_.run_id,call.tool_name,call.arguments_json,expiry,owner_.deadline,cancel,std::move(guidance))};
        const auto activity=owner_.run_id+":"+call.attempt_id+":"+std::to_string(call.tool_index);
        return {true,a2a->invoke(operation,owner_.run_id,call.tool_name,call.arguments_json,expiry,owner_.deadline,cancel,std::move(guidance),[&](const std::string& event){
            if(cancel.stop_requested())throw ToolCancelled("Remote task observation cancelled");
            const auto observed=Json::parse(event);Json entry={{"operation_id",operation},{"activity_id",activity},{"call_id",call.call_id},{"name",call.tool_name},{"agent_id",observed.at("agent_id")},{"data",observed.at("event")}};
            if(observed.contains("event_id")&&observed["event_id"].is_string())entry["event_id"]=observed["event_id"];
            journal_.append_a2a_task_event(owner_.run_id,operation,entry.dump()).get();
        })};
    }
private:
    PersistenceService& store_;
    AgentRunJournal& journal_;
    AgentRemoteToolOwnerContext owner_;
    std::string workspace_id_;
    std::int64_t revision_=1;
    std::mutex calls_mutex_;
    std::set<std::pair<std::string,std::string>> calls_;
};
}
std::unique_ptr<AgentRemoteToolHost> make_agent_remote_tool_host(PersistenceService& store,AgentRunJournal& journal,AgentRemoteToolOwnerContext owner){return std::make_unique<LocalRemoteToolHost>(store,journal,std::move(owner));}
}
