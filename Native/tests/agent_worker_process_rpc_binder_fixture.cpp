#include "agent_worker_rpc_fixture.hpp"
#include "agentflow/agent_worker_process_rpc.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/process_executor.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
using Json=nlohmann::json;
FixtureWorkerRpcHandler bind_agent_worker_process_rpc(AgentWorkerProcessHost& host,const AgentWorkerScope& scope){
    return bind_agent_worker_process_rpc(host,host.lease(),scope);
}
FixtureWorkerRpcHandler bind_agent_worker_process_rpc(AgentProcessOwnerHost& host,const AgentWorkerLeaseSpec& lease,const AgentWorkerScope& scope){
    if(scope!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Process RPC channel differs from captured lease");
    return [&host](const std::string& method,const std::string& payload,AgentWorkerRpcSink,std::stop_token cancel)->std::string{
        if(method!="process.invoke")throw AgentWorkerRpcRemoteError("unsupported");
        try{const auto call=AgentProcessToolInvocation::parse(payload);const auto result=host.invoke(call,cancel);return Json{{"success",result.success},{"output_json",result.output_json}}.dump();}
        catch(const ProcessOutcomeUnrecorded&){throw AgentWorkerRpcRemoteError("outcome_unrecorded");}
        catch(const ProcessEffectUncertain&){throw AgentWorkerRpcRemoteError("effect_uncertain");}
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
