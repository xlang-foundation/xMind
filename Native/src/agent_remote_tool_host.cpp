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
        if(!bounded_id(owner_.run_id,128)||!owner_.primary_workspace||!owner_.repository_context||!owner_.offered_tool||!owner_.mcp_registry||!owner_.a2a_registry||owner_.approval_timeout.count()<=0||owner_.approval_timeout>std::chrono::hours(24))throw std::invalid_argument("Invalid backend remote-tool owner");
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
        auto& context=*owner_.repository_context;
        if(!context.skills().ready(cancel)||!context.ready(".",cancel))return {false,R"({"error":{"code":"repository_instructions_required","message":"No requested action or approval proposal occurred. Updated repository or skill guidance will be supplied in the next model request; reconsider this call using it."}})"};
        auto guidance=context.precondition(".");
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
void validate_agent_remote_tool_invocation(const AgentRemoteToolInvocation& call){
    const auto text=[](const std::string& value,std::size_t limit){return !value.empty()&&value.size()<=limit&&value.find('\0')==std::string::npos;};
    if(call.protocol_version!=1||!bounded_id(call.run_id,128)||!bounded_id(call.attempt_id,128)||!bounded_id(call.tool_name,128)||!text(call.call_id,256)||!text(call.workspace_id,256)||call.workspace_revision<1||call.workspace_revision>9007199254740991LL||call.tool_index>=64||call.arguments_json.size()>4*1024*1024)throw std::invalid_argument("Invalid remote-tool invocation envelope");
}
std::string AgentRemoteToolInvocation::serialize()const{
    validate_agent_remote_tool_invocation(*this);
    return Json{{"protocol_version",protocol_version},{"run_id",run_id},{"attempt_id",attempt_id},{"call_id",call_id},{"workspace_id",workspace_id},{"workspace_revision",workspace_revision},{"tool_name",tool_name},{"arguments_json",arguments_json},{"tool_index",tool_index}}.dump();
}
AgentRemoteToolInvocation AgentRemoteToolInvocation::parse(std::string_view wire){
    if(wire.size()>6*4*1024*1024+16384)throw std::invalid_argument("Remote invocation wire exceeds limits");
    std::set<std::string> keys;
    try{
        const auto value=Json::parse(wire,[&](int depth,Json::parse_event_t event,Json& item){if(depth>1)throw std::invalid_argument("Nested remote invocation envelope");if(event==Json::parse_event_t::key&&!keys.insert(item.get<std::string>()).second)throw std::invalid_argument("Duplicate remote invocation field");return true;});
        static const std::set<std::string> expected={"protocol_version","run_id","attempt_id","call_id","workspace_id","workspace_revision","tool_name","arguments_json","tool_index"};
        if(!value.is_object()||keys!=expected||value.size()!=expected.size())throw std::invalid_argument("Invalid remote invocation fields");
        for(const auto* field:{"run_id","attempt_id","call_id","workspace_id","tool_name","arguments_json"})if(!value.at(field).is_string())throw std::invalid_argument("Invalid remote invocation text");
        if(!value.at("protocol_version").is_number_integer()||value.at("protocol_version")!=1||!value.at("workspace_revision").is_number_integer()||value.at("workspace_revision")<1||value.at("workspace_revision")>9007199254740991LL||!value.at("tool_index").is_number_integer()||value.at("tool_index")<0||value.at("tool_index")>=64)throw std::invalid_argument("Invalid remote invocation version, revision or tool index");
        AgentRemoteToolInvocation call;call.run_id=value.at("run_id");call.attempt_id=value.at("attempt_id");call.call_id=value.at("call_id");call.workspace_id=value.at("workspace_id");call.workspace_revision=value.at("workspace_revision");call.tool_name=value.at("tool_name");call.arguments_json=value.at("arguments_json");call.tool_index=value.at("tool_index");validate_agent_remote_tool_invocation(call);return call;
    }catch(const Json::exception&){throw std::invalid_argument("Invalid remote invocation JSON");}
}
std::unique_ptr<AgentRemoteToolHost> make_agent_remote_tool_host(PersistenceService& store,AgentRunJournal& journal,AgentRemoteToolOwnerContext owner){return std::make_unique<LocalRemoteToolHost>(store,journal,std::move(owner));}
}
