#pragma once
#include "agentflow/agent_permissions.hpp"
#include "agentflow/model_provider.hpp"
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <stop_token>

namespace agentflow {
class AgentRunJournal;
class RepositoryInstructionContext;
struct AgentCallableInvocation {
    std::uint32_t protocol_version=1;
    std::string run_id,attempt_id,call_id,workspace_id,tool_name,arguments_json;
    std::int64_t workspace_revision=1;
};
struct AgentCallableToolResult {bool success=false;std::string output_json;};
struct AgentCallableEffectUncertain:std::runtime_error {using std::runtime_error::runtime_error;};
struct AgentCallableOutcomeUnrecorded:std::runtime_error {using std::runtime_error::runtime_error;};
class AgentCallableToolHost {
public:
    virtual ~AgentCallableToolHost()=default;
    virtual AgentCallableToolResult invoke(const AgentCallableInvocation&,std::stop_token={})=0;
};
// Construction is backend-only. These callbacks and code/runtime objects never
// appear in invocation values or become model-selected module/function names.
struct AgentCallableBinding {
    ModelToolDefinition definition;
    std::function<std::string(const std::string&,std::stop_token)> invoke;
};
struct AgentCallableToolOwnerContext {
    std::string run_id,workspace_id;
    std::int64_t workspace_revision=1;
    std::vector<AgentCallableBinding> bindings;
    std::function<bool(const std::string&)> offered_tool;
    RepositoryInstructionContext* repository_context=nullptr;
    std::function<RepositoryInstructionContext*()> repository_context_for_call;
    std::vector<std::vector<AgentPermissionRule>> permission_layers;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::milliseconds approval_timeout{600000};
    // Backend-only live authority, after approval and immediately before the
    // callable may produce an effect. Invocation values cannot supply it.
    std::function<void(const AgentCallableInvocation&,const std::string&,std::stop_token)> before_effect;
};
// Local owned dispatch, not an authenticated worker RPC or an OS sandbox.
std::unique_ptr<AgentCallableToolHost> make_agent_callable_tool_host(
    AgentRunJournal&,AgentCallableToolOwnerContext);
}
