#include "agentflow/agent_worker_file_rpc.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
using Json=nlohmann::json;
AgentFileToolResult AgentWorkerFileClient::invoke(const AgentFileToolInvocation& call,std::stop_token cancel){
    if(auto native=peer_.native_model())return native->file_invoke(call,cancel);
    try{const auto bytes=peer_.call("file.invoke",call.serialize(),{},cancel);const auto value=Json::parse(mcp_compact_object(bytes));
        if(value.size()!=2||!value.at("success").is_boolean()||!value.at("output_json").is_string())throw ModelProtocolError("Invalid worker file result");
        return {value.at("success").get<bool>(),value.at("output_json").get<std::string>()};}
    catch(const AgentWorkerRpcCancelled&){throw ToolCancelled("Worker file RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw ToolCancelled("Worker file RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker file RPC rejected its captured call");if(error.code()=="cancelled"||error.code()=="timeout")throw ToolCancelled("Worker file RPC cancelled or expired");if(error.code()=="outcome_unrecorded")throw AgentFileToolOutcomeUnrecorded("Worker file result is unrecorded; no replay is authorized");throw ModelProtocolError("Worker file RPC failed");}
    catch(const McpProtocolError&){throw ModelProtocolError("Invalid worker file result");}
    catch(const Json::exception&){throw ModelProtocolError("Invalid worker file result");}
}
}
