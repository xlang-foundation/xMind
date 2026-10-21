#pragma once
#include "agentflow/model_provider.hpp"
#include "agentflow/agent_permissions.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace agentflow {
class PersistenceService;
class WorkspaceTools;
class WorkspaceToolSet;
class RepositoryInstructionContext;

// Owned run-loop/owner values. No workspace root, prepared plan, approval,
// instruction verifier, persistence object or credential crosses this boundary.
struct AgentFileToolInvocation {
    std::int32_t protocol_version=1;
    std::string run_id,attempt_id,call_id,workspace_id,folder_id,tool_name,arguments_json;
    std::int64_t workspace_revision=0;
    // Exact values only: no approval, operation ID, path/root capability or
    // owner policy may be added to this envelope. Transport frame limits are
    // enforced separately after RPC/channel encoding.
    std::string serialize()const;
    static AgentFileToolInvocation parse(std::string_view);
};
struct AgentFileToolResult {bool success=false;std::string output_json;};
class AgentFileToolHost {
public:
    virtual ~AgentFileToolHost()=default;
    virtual AgentFileToolResult invoke(const AgentFileToolInvocation&,std::stop_token cancel={})=0;
};
bool is_agent_file_tool(const std::string& name);

// Backend-only construction. These capabilities must outlive the host; they
// never form part of a worker request. Permissions and catalogue are frozen
// server-created run policy, not values selected by a worker or model.
struct AgentFileToolOwnerContext {
    std::string run_id;
    WorkspaceTools* primary_workspace=nullptr;
    WorkspaceToolSet* workspace_members=nullptr;
    std::function<RepositoryInstructionContext*(std::size_t)> repository_context;
    std::vector<std::vector<AgentPermissionRule>> permission_layers;
    std::vector<std::string> allowed_tools;
    std::chrono::milliseconds approval_timeout{600000};
    // Revalidate the retained owner immediately before each approved effect.
    // A multi-file invocation may call this more than once; it cannot replay
    // effects or replace the caller's admitted identity.
    // The operation ID is supplied by the native executor, never by the model.
    std::function<void(const AgentFileToolInvocation&,const std::string&,std::stop_token)> before_effect;
};
// Local owner adapter only. This is not an authenticated IPC endpoint, durable
// RPC deduplicator, process supervisor or an out-of-process AgentRunner.
std::unique_ptr<AgentFileToolHost> make_agent_file_tool_host(
    PersistenceService&,AgentFileToolOwnerContext);
}
