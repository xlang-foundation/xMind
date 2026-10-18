#pragma once
#include "agentflow/agent_permissions.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace agentflow {
class PersistenceService;
class AgentRunJournal;
class WorkspaceTools;
class WorkspaceToolSet;
class RepositoryInstructionContext;
class McpToolRegistry;
class A2aAgentToolRegistry;

// Owned run-loop values only. Destinations, credentials, grants, registries,
// instruction preconditions and journal handles are never caller-supplied.
struct AgentRemoteToolInvocation {
    std::int32_t protocol_version=1;
    std::string run_id,attempt_id,call_id,workspace_id,tool_name,arguments_json;
    std::int64_t workspace_revision=0;
    std::uint32_t tool_index=0;
};
struct AgentRemoteToolResult {bool success=false;std::string output_json;};
class AgentRemoteToolHost {
public:
    virtual ~AgentRemoteToolHost()=default;
    virtual AgentRemoteToolResult invoke(const AgentRemoteToolInvocation&,std::stop_token cancel={})=0;
};

// Backend-only construction. The owner updates discovery and the offered
// catalogue between model turns; lookups cannot grant aliases absent from
// that currently offered, policy-filtered catalogue. All capabilities outlive
// the host. Requests cannot replace these callbacks or choose a peer endpoint.
struct AgentRemoteToolOwnerContext {
    std::string run_id;
    WorkspaceTools* primary_workspace=nullptr;
    WorkspaceToolSet* workspace_members=nullptr;
    RepositoryInstructionContext* repository_context=nullptr;
    std::vector<std::vector<AgentPermissionRule>> permission_layers;
    std::function<bool(const std::string&)> offered_tool;
    std::function<McpToolRegistry*(const std::string&)> mcp_registry;
    std::function<A2aAgentToolRegistry*(const std::string&)> a2a_registry;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::milliseconds approval_timeout{600000};
};
// In-process owner adapter, not an IPC endpoint or durable RPC deduplicator.
// Agent-worker authentication, supervision and crash reconciliation remain
// separate work; this boundary does not enable process-bound AgentRunners.
std::unique_ptr<AgentRemoteToolHost> make_agent_remote_tool_host(
    PersistenceService&,AgentRunJournal&,AgentRemoteToolOwnerContext);
}
