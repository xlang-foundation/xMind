#pragma once
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
class AgentProcessToolHost;
class WorkspaceTools;
class WorkspaceToolSet;
class RepositoryInstructionContext;

// Owned model-loop values. The owner chooses operation IDs, expiry, executable
// bindings, workspace handles, approvals and instruction preconditions.
struct AgentProcessToolInvocation {
    std::int32_t protocol_version=1;
    std::string run_id,attempt_id,call_id,workspace_id,folder_id,tool_name,arguments_json;
    std::int64_t workspace_revision=0;
    std::string serialize()const;
    static AgentProcessToolInvocation parse(std::string_view);
};
struct AgentProcessToolResult {bool success=false;std::string output_json;};
class AgentProcessOwnerHost {
public:
    virtual ~AgentProcessOwnerHost()=default;
    virtual AgentProcessToolResult invoke(const AgentProcessToolInvocation&,std::stop_token cancel={})=0;
};
// Shared bounded model observation from backend-produced terminal job results.
std::string format_joined_process_observation(const std::vector<std::string>& joined);
bool is_agent_process_tool(const std::string& name);
void validate_agent_process_tool_invocation(const AgentProcessToolInvocation&);

// Trusted backend construction only; none of these capabilities are worker
// request fields. This adapter is not authenticated IPC or durable RPC dedup.
struct AgentProcessToolOwnerContext {
    std::string run_id;
    AgentProcessToolHost* process=nullptr;
    WorkspaceTools* primary_workspace=nullptr;
    WorkspaceToolSet* workspace_members=nullptr;
    std::function<RepositoryInstructionContext*(std::size_t)> repository_context;
    std::function<bool(const std::string&)> offered_tool;
    std::vector<std::vector<AgentPermissionRule>> permission_layers;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::milliseconds approval_timeout{600000};
    bool allow_background=true;
    std::stop_token background_cancel;
    // Backend-only authority for run_process, checked after approval at the
    // actual dispatch boundary. The executor supplies its claimed operation ID.
    std::function<void(const AgentProcessToolInvocation&,const std::string&,std::stop_token)> before_effect;
};
std::unique_ptr<AgentProcessOwnerHost> make_agent_process_tool_host(
    PersistenceService&,AgentProcessToolOwnerContext);
}
