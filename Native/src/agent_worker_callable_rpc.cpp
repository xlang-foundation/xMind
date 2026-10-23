#include "agentflow/agent_worker_callable_rpc.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
using Json=nlohmann::json;
AgentCallableToolResult AgentWorkerCallableClient::invoke(const AgentCallableInvocation& call,std::stop_token cancel){
    if(auto native=peer_.native_model())return native->callable_invoke(call,cancel);
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
