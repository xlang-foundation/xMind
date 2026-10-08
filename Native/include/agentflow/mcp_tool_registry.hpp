#pragma once
#include "agentflow/mcp_client.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/workspace_tools.hpp"

namespace agentflow {
struct McpEffectUncertain : std::runtime_error {using std::runtime_error::runtime_error;};
struct McpOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
struct McpEffectNotDispatched : std::runtime_error {using std::runtime_error::runtime_error;};
// Owning backend adapter; config identity/revision comes from trusted backend
// configuration, workspace identity from the opened directory. One caller.
// Discovery completes before any alias becomes usable. All remote calls require
// exact durable controller approval, regardless of untrusted tool annotations.
// Client/store/workspace outlive this registry. Views cannot supply configuration
// commands, raw peer names, approval actors, claims or operation outcomes.
class McpToolRegistry {
public:
    McpToolRegistry(McpStdioClient& client,PersistenceService& store,WorkspaceTools& workspace,
        std::string config_id,std::int64_t config_revision,McpStdioClient::Deadline deadline,
        std::stop_token cancel={});
    ~McpToolRegistry();
    McpToolRegistry(const McpToolRegistry&)=delete;
    McpToolRegistry& operator=(const McpToolRegistry&)=delete;
    std::vector<ModelToolDefinition> definitions() const;
    // Backend-private immutable approval metadata for each actual discovered
    // alias, as a JSON array. No arguments, credentials or peer access. This
    // snapshot belongs in sealed native authority, never model/public DTOs.
    std::string approval_bindings_json() const;
    std::string invoke(const std::string& operation_id,const std::string& run_id,
        const std::string& alias,const std::string& arguments,std::int64_t approval_expiry,
        McpStdioClient::Deadline deadline,std::stop_token cancel={});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
