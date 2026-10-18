#pragma once
#include "agentflow/agent_permissions.hpp"
#include "agentflow/workspace_tools.hpp"
#include <chrono>
#include <functional>
#include <memory>

namespace agentflow {
class AgentRunJournal;
class PersistenceService;
class RepositoryInstructionContext;

struct AgentReadToolResult {bool success=false;std::string output_json;};
class AgentReadToolHost {
public:
    virtual ~AgentReadToolHost()=default;
    virtual AgentReadToolResult invoke(const AgentWorkspaceInvocation&,std::stop_token cancel={})=0;
};
bool is_agent_read_tool(const std::string& name);

// Trusted backend construction only. Requests use the existing owned,
// versioned workspace envelope; roots, grants and approval IDs stay here.
struct AgentReadToolOwnerContext {
    std::string run_id;
    WorkspaceTools* primary_workspace=nullptr;
    WorkspaceToolSet* workspace_members=nullptr;
    std::function<RepositoryInstructionContext*(std::size_t)> repository_context;
    std::function<bool(const std::string&)> offered_tool;
    std::vector<std::vector<AgentPermissionRule>> inherited_permission_layers;
    std::vector<AgentPermissionRule> permission_rules;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::milliseconds approval_timeout{600000};
};
// Local owner and lifetime call guard, not authenticated or durable worker RPC.
std::unique_ptr<AgentReadToolHost> make_agent_read_tool_host(
    AgentRunJournal&,PersistenceService&,AgentReadToolOwnerContext);
}
