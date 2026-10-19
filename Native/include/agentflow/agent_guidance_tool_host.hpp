#pragma once
#include "agentflow/agent_permissions.hpp"
#include "agentflow/workspace_tools.hpp"
#include <chrono>
#include <functional>
#include <memory>

namespace agentflow {
class AgentRunJournal;
class RepositoryInstructionContext;
struct AgentGuidanceToolResult { bool success=false;std::string output_json; };
class AgentGuidanceToolHost {
public:
    virtual ~AgentGuidanceToolHost()=default;
    virtual AgentGuidanceToolResult invoke(const AgentWorkspaceInvocation&,std::stop_token={})=0;
};
bool is_agent_guidance_tool(const std::string&);
// Backend construction only. Invocation values cannot choose roots, supply
// guidance contents, restore skill state or bypass the offered catalogue.
struct AgentGuidanceToolOwnerContext {
    std::string run_id;
    WorkspaceTools* primary_workspace=nullptr;
    WorkspaceToolSet* workspace_members=nullptr;
    std::function<RepositoryInstructionContext*(std::size_t)> repository_context;
    std::function<bool(const std::string&)> offered_tool;
    std::vector<std::vector<AgentPermissionRule>> inherited_permission_layers;
    std::vector<AgentPermissionRule> permission_rules;
    std::chrono::steady_clock::time_point deadline;
    std::size_t base_instruction_bytes=0;
};
// Local owner and lifetime guard. Does not establish authenticated worker RPC.
std::unique_ptr<AgentGuidanceToolHost> make_agent_guidance_tool_host(
    AgentRunJournal&,AgentGuidanceToolOwnerContext);
}
