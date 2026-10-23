#include "agentflow/agent_worker_process_rpc.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/process_executor.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
using Json=nlohmann::json;
AgentProcessToolResult AgentWorkerProcessClient::invoke(const AgentProcessToolInvocation& call,std::stop_token cancel){
    if(auto native=peer_.native_model())return native->process_invoke(call,cancel);
    try{const auto bytes=peer_.call("process.invoke",call.serialize(),{},cancel);const auto value=Json::parse(mcp_compact_object(bytes));
        if(value.size()!=2||!value.at("success").is_boolean()||!value.at("output_json").is_string())throw ModelProtocolError("Invalid worker process result");
        return {value.at("success").get<bool>(),value.at("output_json").get<std::string>()};}
    catch(const AgentWorkerRpcCancelled&){throw ToolCancelled("Worker process RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw ToolCancelled("Worker process RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker process RPC rejected its captured call");if(error.code()=="cancelled"||error.code()=="timeout")throw ToolCancelled("Worker process RPC cancelled or expired");if(error.code()=="outcome_unrecorded")throw ProcessOutcomeUnrecorded("Worker process result is unrecorded; no replay is authorized");if(error.code()=="effect_uncertain")throw ProcessEffectUncertain("Worker command effect is uncertain; no replay is authorized");throw ModelProtocolError("Worker process RPC failed");}
    catch(const McpProtocolError&){throw ModelProtocolError("Invalid worker process result");}
    catch(const Json::exception&){throw ModelProtocolError("Invalid worker process result");}
}
}
