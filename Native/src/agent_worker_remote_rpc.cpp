#include "agentflow/agent_worker_remote_rpc.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/a2a_tool_registry.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
using Json=nlohmann::json;
AgentWorkerRpcHandler bind_agent_worker_remote_rpc(AgentWorkerRemoteHost& host,const AgentWorkerScope& scope){
    const auto& lease=host.lease();
    if(scope!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Process RPC channel differs from captured lease");
    return [&host](const std::string& method,const std::string& payload,AgentWorkerRpcSink,std::stop_token cancel)->std::string{
        if(method!="remote.invoke")throw AgentWorkerRpcRemoteError("unsupported");
        try{const auto call=AgentRemoteToolInvocation::parse(payload);const auto result=host.invoke(call,cancel);return Json{{"success",result.success},{"output_json",result.output_json}}.dump();}
        catch(const AgentWorkerRemoteOutcomeUnrecorded&){throw AgentWorkerRpcRemoteError("outcome_unrecorded");}
        catch(const McpEffectUncertain&){throw AgentWorkerRpcRemoteError("effect_uncertain");}
        catch(const A2aEffectUncertain&){throw AgentWorkerRpcRemoteError("effect_uncertain");}
        catch(const McpOutcomeUnrecorded&){throw AgentWorkerRpcRemoteError("outcome_unrecorded");}
        catch(const A2aOutcomeUnrecorded&){throw AgentWorkerRpcRemoteError("outcome_unrecorded");}
        catch(const AgentWorkerAuthorityCancelled&){throw AgentWorkerRpcRemoteError("cancelled");}
        catch(const AgentWorkerAuthorityExpired&){throw AgentWorkerRpcRemoteError("timeout");}
        catch(const ToolCancelled&){throw AgentWorkerRpcRemoteError("cancelled");}
        catch(const Conflict&){throw AgentWorkerRpcRemoteError("rejected");}
        catch(const NotFound&){throw AgentWorkerRpcRemoteError("rejected");}
        catch(const std::invalid_argument&){throw AgentWorkerRpcRemoteError("rejected");}
        catch(const McpProtocolError&){throw AgentWorkerRpcRemoteError("rejected");}
        catch(const Json::exception&){throw AgentWorkerRpcRemoteError("rejected");}
    };
}
AgentRemoteToolResult AgentWorkerRemoteClient::invoke(const AgentRemoteToolInvocation& call,std::stop_token cancel){
    try{const auto bytes=peer_.call("remote.invoke",call.serialize(),{},cancel);const auto value=Json::parse(mcp_compact_object(bytes));
        if(value.size()!=2||!value.at("success").is_boolean()||!value.at("output_json").is_string())throw ModelProtocolError("Invalid worker remote result");
        return {value.at("success").get<bool>(),value.at("output_json").get<std::string>()};}
    catch(const AgentWorkerRpcCancelled&){throw ToolCancelled("Worker remote RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw ToolCancelled("Worker remote RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker remote RPC rejected its captured call");if(error.code()=="cancelled"||error.code()=="timeout")throw ToolCancelled("Worker remote RPC cancelled or expired");if(error.code()=="outcome_unrecorded")throw AgentWorkerRemoteOutcomeUnrecorded("Worker remote result is unrecorded; no replay is authorized");if(error.code()=="effect_uncertain")throw McpEffectUncertain("Worker command effect is uncertain; no replay is authorized");throw ModelProtocolError("Worker remote RPC failed");}
    catch(const McpProtocolError&){throw ModelProtocolError("Invalid worker remote result");}
    catch(const Json::exception&){throw ModelProtocolError("Invalid worker remote result");}
}
}
