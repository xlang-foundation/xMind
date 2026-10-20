#pragma once
#include "agentflow/process_executor.hpp"
#include "agentflow/mcp_configuration.hpp"
#include "agentflow/a2a_agent_registry.hpp"

namespace agentflow {
class McpToolRegistry;
class A2aAgentToolRegistry;
// Trusted owner session. Credentials, native clients, observers and registry
// lifetimes remain inside the backend adapter. This is not a wire contract.
class AgentMcpPeer {
public:
    virtual ~AgentMcpPeer()=default;
    virtual std::string protocol_version()const=0;
    virtual void discover(WorkspaceTools&,std::vector<WorkspaceToolFolder>)=0;
    virtual std::vector<ModelToolDefinition> definitions()const=0;
    virtual std::string approval_bindings_json()const=0;
    virtual McpToolRegistry* owner_registry()noexcept=0;
    // False preserves the existing unavailable-listener/invalidation behavior.
    virtual bool start_observer(const std::string& run)=0;
    virtual void stop_observer()noexcept=0;
};
// Backend-only construction and metadata port. Worker implementations must use
// captured admission and authenticated proxies, never deserialize these native
// capabilities or accept worker-selected endpoints, executables or credentials.
class AgentBackendRuntime {
public:
    virtual ~AgentBackendRuntime()=default;
    virtual std::unique_ptr<AgentProcessToolHost> process(WorkspaceTools&,const std::string&,std::vector<ProcessProfile>)=0;
    virtual std::shared_ptr<AgentMcpPeer> connect_mcp(const McpServerSetting&,std::chrono::steady_clock::time_point,std::stop_token)=0;
    virtual A2aRemoteAgentCatalog a2a_catalog()=0;
    virtual std::unique_ptr<A2aAgentToolRegistry> discover_a2a(WorkspaceTools&,std::chrono::milliseconds,std::stop_token)=0;
};
std::shared_ptr<AgentBackendRuntime> make_persistence_agent_backend_runtime(PersistenceService&);
}
