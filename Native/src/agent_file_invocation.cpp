#include "agentflow/agent_file_tool_host.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>
namespace agentflow {
using Json=nlohmann::json;
namespace {
bool bounded_id(const std::string& value,std::size_t limit){return !value.empty()&&value.size()<=limit&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<0x21||c>0x7e;});}
}
bool is_agent_file_tool(const std::string& name){
    return name=="edit_file"||name=="create_file"||name=="apply_patch"||name=="restore_operation";
}
void validate_agent_file_tool_invocation(const AgentFileToolInvocation& call){
    const auto text=[](const std::string& value,std::size_t limit){return !value.empty()&&value.size()<=limit&&value.find('\0')==std::string::npos;};
    if(call.protocol_version!=1||!bounded_id(call.run_id,128)||!bounded_id(call.attempt_id,128)||
       !text(call.call_id,256)||!text(call.workspace_id,256)||!text(call.folder_id,256)||
       call.workspace_revision<1||call.workspace_revision>9007199254740991LL||
       !is_agent_file_tool(call.tool_name)||call.arguments_json.size()>4*1024*1024)
        throw std::invalid_argument("Invalid file-tool invocation envelope");
}
std::string AgentFileToolInvocation::serialize()const{
    validate_agent_file_tool_invocation(*this);
    return Json{{"protocol_version",protocol_version},{"run_id",run_id},{"attempt_id",attempt_id},
        {"call_id",call_id},{"workspace_id",workspace_id},{"folder_id",folder_id},
        {"workspace_revision",workspace_revision},{"tool_name",tool_name},{"arguments_json",arguments_json}}.dump();
}
AgentFileToolInvocation AgentFileToolInvocation::parse(std::string_view wire){
    // A JSON string can expand one source byte to six escaped bytes. This
    // value bound preserves the native 4 MiB argument limit without claiming
    // that every such call fits a separately bounded authenticated frame.
    if(wire.size()>6*4*1024*1024+16384)throw std::invalid_argument("File invocation wire exceeds limits");
    Json value;std::set<std::string> keys;
    try{
        value=Json::parse(wire,[&](int depth,Json::parse_event_t event,Json& item){
            if(depth>1)throw std::invalid_argument("Nested file invocation envelope");
            if(event==Json::parse_event_t::key&&!keys.insert(item.get<std::string>()).second)
                throw std::invalid_argument("Duplicate file invocation field");
            return true;
        });
        static const std::set<std::string> expected={"protocol_version","run_id","attempt_id","call_id","workspace_id","folder_id","workspace_revision","tool_name","arguments_json"};
        if(!value.is_object()||keys!=expected||value.size()!=expected.size())throw std::invalid_argument("Invalid file invocation fields");
        for(const auto* field:{"run_id","attempt_id","call_id","workspace_id","folder_id","tool_name","arguments_json"})
            if(!value.at(field).is_string())throw std::invalid_argument("Invalid file invocation text");
        if(!value.at("protocol_version").is_number_integer()||value.at("protocol_version")!=1||
           !value.at("workspace_revision").is_number_integer()||value.at("workspace_revision")<1||
           value.at("workspace_revision")>9007199254740991LL)throw std::invalid_argument("Invalid file invocation version or revision");
        AgentFileToolInvocation call;call.run_id=value.at("run_id");call.attempt_id=value.at("attempt_id");
        call.call_id=value.at("call_id");call.workspace_id=value.at("workspace_id");call.folder_id=value.at("folder_id");
        call.workspace_revision=value.at("workspace_revision");call.tool_name=value.at("tool_name");call.arguments_json=value.at("arguments_json");
        validate_agent_file_tool_invocation(call);return call;
    }catch(const Json::exception&){throw std::invalid_argument("Invalid file invocation JSON");}
}
}
