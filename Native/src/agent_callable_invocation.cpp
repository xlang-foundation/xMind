#include "agentflow/agent_callable_tool_host.hpp"
#include "agentflow/agent_remote_tool_host.hpp"
namespace agentflow {
std::string AgentCallableInvocation::serialize()const{
    if(protocol_version!=1||arguments_json.size()>256*1024)throw std::invalid_argument("Invalid callable invocation version or argument size");
    AgentRemoteToolInvocation value;value.run_id=run_id;value.attempt_id=attempt_id;value.call_id=call_id;value.workspace_id=workspace_id;value.workspace_revision=workspace_revision;value.tool_name=tool_name;value.arguments_json=arguments_json;value.tool_index=tool_index;return value.serialize();
}
AgentCallableInvocation AgentCallableInvocation::parse(std::string_view wire){
    const auto value=AgentRemoteToolInvocation::parse(wire);AgentCallableInvocation call;call.run_id=value.run_id;call.attempt_id=value.attempt_id;call.call_id=value.call_id;call.workspace_id=value.workspace_id;call.workspace_revision=value.workspace_revision;call.tool_name=value.tool_name;call.arguments_json=value.arguments_json;call.tool_index=value.tool_index;(void)call.serialize();return call;
}
}
