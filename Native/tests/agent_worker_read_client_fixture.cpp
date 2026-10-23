#include "agent_worker_rpc_fixture.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/agent_permissions.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/records.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
using Json=nlohmann::json;
AgentReadToolResult FixtureWorkerRpcPeer::read_invoke(const AgentWorkspaceInvocation& call,std::stop_token cancel){
    try{const auto bytes=this->call("read.invoke",call.serialize(),{},cancel);const auto value=Json::parse(mcp_compact_object(bytes));
        if(value.size()!=2||!value.at("success").is_boolean()||!value.at("output_json").is_string())throw ModelProtocolError("Invalid worker read result");return {value.at("success").get<bool>(),value.at("output_json").get<std::string>()};}
    catch(const AgentWorkerRpcCancelled&){throw ToolCancelled("Worker read RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw ToolCancelled("Worker read RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker read RPC rejected its captured call");if(error.code()=="cancelled"||error.code()=="timeout")throw ToolCancelled("Worker read RPC cancelled or expired");if(error.code()=="outcome_unrecorded")throw AgentWorkerReadOutcomeUnrecorded("Worker read result is unrecorded");throw ModelProtocolError("Worker read RPC failed");}
    catch(const McpProtocolError&){throw ModelProtocolError("Invalid worker read result");}
    catch(const Json::exception&){throw ModelProtocolError("Invalid worker read result");}
}
}
