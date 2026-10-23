#include "agent_worker_rpc_fixture.hpp"
#include "agentflow/agent_worker_callable_rpc.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
using Json=nlohmann::json;
FixtureWorkerRpcHandler bind_agent_worker_callable_rpc(AgentWorkerCallableHost& host,const AgentWorkerScope& scope){
    return bind_agent_worker_callable_rpc(host,host.lease(),scope);
}
FixtureWorkerRpcHandler bind_agent_worker_callable_rpc(AgentWorkerCallableService& host,const AgentWorkerLeaseSpec& lease,const AgentWorkerScope& scope){
    if(scope!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Callable RPC channel differs from captured lease");
    return [&host](const std::string& method,const std::string& payload,AgentWorkerRpcSink,std::stop_token cancel)->std::string{
        if(method!="callable.catalogue"&&method!="callable.invoke")throw AgentWorkerRpcRemoteError("unsupported");
        try{if(method=="callable.catalogue"){if(payload!="{}")throw AgentWorkerRpcRemoteError("rejected");return Json(host.tool_names(cancel)).dump();}const auto call=AgentCallableInvocation::parse(payload);const auto result=host.invoke(call,cancel);return Json{{"success",result.success},{"output_json",result.output_json}}.dump();}
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
}
