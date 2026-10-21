#include "agentflow/agent_remote_tool_host.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>
namespace agentflow {
using Json=nlohmann::json;
namespace {bool bounded_id(const std::string& value,std::size_t limit){return !value.empty()&&value.size()<=limit&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<0x21||c>0x7e;});}}
bool is_agent_remote_tool(const std::string& name){return name.starts_with("mcp_")||name.starts_with("a2a_delegate_");}
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
}
