#include "agent_worker_rpc_fixture.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/a2a_tool_registry.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
using Json=nlohmann::json;
AgentRemoteToolResult FixtureWorkerRpcPeer::remote_invoke(const AgentRemoteToolInvocation& call,std::stop_token cancel){
    try{const auto bytes=this->call("remote.invoke",call.serialize(),{},cancel);const auto value=Json::parse(mcp_compact_object(bytes));
        if(value.size()!=2||!value.at("success").is_boolean()||!value.at("output_json").is_string())throw ModelProtocolError("Invalid worker remote result");
        return {value.at("success").get<bool>(),value.at("output_json").get<std::string>()};}
    catch(const AgentWorkerRpcCancelled&){throw ToolCancelled("Worker remote RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw ToolCancelled("Worker remote RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker remote RPC rejected its captured call");if(error.code()=="cancelled"||error.code()=="timeout")throw ToolCancelled("Worker remote RPC cancelled or expired");if(error.code()=="outcome_unrecorded")throw AgentWorkerRemoteOutcomeUnrecorded("Worker remote result is unrecorded; no replay is authorized");if(error.code()=="effect_uncertain")throw McpEffectUncertain("Worker command effect is uncertain; no replay is authorized");throw ModelProtocolError("Worker remote RPC failed");}
    catch(const McpProtocolError&){throw ModelProtocolError("Invalid worker remote result");}
    catch(const Json::exception&){throw ModelProtocolError("Invalid worker remote result");}
}
}
