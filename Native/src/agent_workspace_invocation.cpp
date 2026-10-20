#include "agentflow/workspace_tools.hpp"
#include "nlohmann/json.hpp"
#include <set>
#include <limits>
namespace agentflow {
namespace {
using Json=nlohmann::json;
void validate_workspace_invocation(const AgentWorkspaceInvocation& request){
    const auto bounded=[](const std::string& value,std::size_t maximum){return !value.empty()&&value.size()<=maximum;};
    if(request.protocol_version!=1||!bounded(request.run_id,256)||!bounded(request.attempt_id,256)||!bounded(request.call_id,256)||
       !bounded(request.workspace_id,256)||!bounded(request.folder_id,256)||!bounded(request.tool_name,128)||
       request.workspace_revision<=0||request.arguments_json.size()>65536)
        throw std::invalid_argument("Invalid run-scoped workspace invocation metadata");
}
}
std::string AgentWorkspaceInvocation::serialize()const{
    validate_workspace_invocation(*this);
    const auto wire=Json{{"protocol_version",protocol_version},{"run_id",run_id},{"attempt_id",attempt_id},
        {"call_id",call_id},{"workspace_id",workspace_id},{"workspace_revision",workspace_revision},
        {"folder_id",folder_id},{"tool_name",tool_name},{"arguments_json",arguments_json}}.dump();
    if(wire.size()>70000)throw std::invalid_argument("Workspace invocation wire message exceeds its limit");
    return wire;
}
AgentWorkspaceInvocation AgentWorkspaceInvocation::parse(std::string_view wire_json){
    if(wire_json.size()>70000)throw std::invalid_argument("Workspace invocation wire message exceeds its limit");
    std::vector<std::set<std::string>> fields;Json value;
    try{value=Json::parse(wire_json,[&](int depth,Json::parse_event_t event,Json& item){
        if(depth>4)throw std::invalid_argument("Workspace invocation wire nesting exceeds its limit");
        if(event==Json::parse_event_t::object_start)fields.emplace_back();
        else if(event==Json::parse_event_t::object_end)fields.pop_back();
        else if(event==Json::parse_event_t::key&&!fields.back().insert(item.get<std::string>()).second)
            throw std::invalid_argument("Duplicate workspace invocation wire field");
        return true;
    });}catch(const Json::exception&){throw std::invalid_argument("Invalid workspace invocation wire JSON");}
    static const std::set<std::string> expected={"protocol_version","run_id","attempt_id","call_id","workspace_id","workspace_revision","folder_id","tool_name","arguments_json"};
    if(!value.is_object()||value.size()!=expected.size())throw std::invalid_argument("Invalid workspace invocation wire object");
    for(const auto& [key,field]:value.items()){(void)field;if(!expected.contains(key))throw std::invalid_argument("Unknown workspace invocation wire field");}
    for(const auto& field:expected)if(!value.contains(field))throw std::invalid_argument("Missing workspace invocation wire field");
    for(const auto* field:{"run_id","attempt_id","call_id","workspace_id","folder_id","tool_name","arguments_json"})
        if(!value[field].is_string())throw std::invalid_argument("Workspace invocation wire text field has the wrong type");
    if(!value["protocol_version"].is_number_integer()||!value["workspace_revision"].is_number_integer())
        throw std::invalid_argument("Workspace invocation wire version or revision has the wrong type");
    const auto& version=value["protocol_version"];
    if((version.is_number_unsigned()&&version.get<std::uint64_t>()!=1)||
       (!version.is_number_unsigned()&&version.get<std::int64_t>()!=1))
        throw std::invalid_argument("Unsupported workspace invocation wire version");
    AgentWorkspaceInvocation request;
    request.protocol_version=1;
    request.run_id=value["run_id"].get<std::string>();request.attempt_id=value["attempt_id"].get<std::string>();
    request.call_id=value["call_id"].get<std::string>();request.workspace_id=value["workspace_id"].get<std::string>();
    const auto& revision=value["workspace_revision"];
    if(revision.is_number_unsigned()){
        const auto unsigned_revision=revision.get<std::uint64_t>();
        if(unsigned_revision>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            throw std::invalid_argument("Workspace invocation revision exceeds its limit");
        request.workspace_revision=static_cast<std::int64_t>(unsigned_revision);
    }else request.workspace_revision=revision.get<std::int64_t>();
    request.folder_id=value["folder_id"].get<std::string>();
    request.tool_name=value["tool_name"].get<std::string>();request.arguments_json=value["arguments_json"].get<std::string>();
    validate_workspace_invocation(request);return request;
}
}
