#include "agentflow/agent_worker_callable_rpc.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
using Json=nlohmann::json;
AgentWorkerRpcHandler bind_agent_worker_callable_rpc(AgentWorkerCallableHost& host,const AgentWorkerScope& scope){
    const auto& lease=host.lease();
    if(scope!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Process RPC channel differs from captured lease");
    return [&host](const std::string& method,const std::string& payload,AgentWorkerRpcSink,std::stop_token cancel)->std::string{
        if(method!="callable.invoke")throw AgentWorkerRpcRemoteError("unsupported");
        try{const auto call=AgentCallableInvocation::parse(payload);const auto result=host.invoke(call,cancel);return Json{{"success",result.success},{"output_json",result.output_json}}.dump();}
        catch(const AgentWorkerCallableOutcomeUnrecorded&){throw AgentWorkerRpcRemoteError("outcome_unrecorded");}
        catch(const AgentCallableEffectUncertain&){throw AgentWorkerRpcRemoteError("effect_uncertain");}
        catch(const AgentCallableOutcomeUnrecorded&){throw AgentWorkerRpcRemoteError("outcome_unrecorded");}
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
AgentCallableToolResult AgentWorkerCallableClient::invoke(const AgentCallableInvocation& call,std::stop_token cancel){
    try{const auto bytes=peer_.call("callable.invoke",call.serialize(),{},cancel);const auto value=Json::parse(mcp_compact_object(bytes));
        if(value.size()!=2||!value.at("success").is_boolean()||!value.at("output_json").is_string())throw ModelProtocolError("Invalid worker callable result");
        return {value.at("success").get<bool>(),value.at("output_json").get<std::string>()};}
    catch(const AgentWorkerRpcCancelled&){throw ToolCancelled("Worker callable RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw ToolCancelled("Worker callable RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker callable RPC rejected its captured call");if(error.code()=="cancelled"||error.code()=="timeout")throw ToolCancelled("Worker callable RPC cancelled or expired");if(error.code()=="outcome_unrecorded")throw AgentWorkerCallableOutcomeUnrecorded("Worker callable result is unrecorded; no replay is authorized");if(error.code()=="effect_uncertain")throw AgentCallableEffectUncertain("Worker callable effect is uncertain; no replay is authorized");throw ModelProtocolError("Worker callable RPC failed");}
    catch(const McpProtocolError&){throw ModelProtocolError("Invalid worker callable result");}
    catch(const Json::exception&){throw ModelProtocolError("Invalid worker callable result");}
}
}
