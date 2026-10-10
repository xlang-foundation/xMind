#pragma once
#include "agentflow/persistence_service.hpp"
#include "agentflow/run_executor.hpp"
#include <string_view>

namespace agentflow {
class EditRecoveryReader;
class ProviderSetup;
class ProviderProfileSetup;
class GraphExecution;
class BackendOwnerControl;
struct McpServerMetadata {std::string id;std::int64_t revision;bool enabled;std::string transport="stdio";};
struct ProcessProfileMetadata {std::string id;std::int64_t revision,max_timeout_ms;};
struct AgentInstructionMetadata {std::int64_t revision=0;std::size_t byte_count=0;};
void validate_local_auth_token(std::string_view token);
// Loopback transport adapter. No HTTP/Electron/WebRTC dependencies in core.
class HttpServer {
public:
    HttpServer(PersistenceService& persistence,std::string auth_token,RunExecutor* executor=nullptr,EditRecoveryReader* recovery=nullptr,std::vector<McpServerMetadata> mcp_servers={},std::vector<ProcessProfileMetadata> process_profiles={},AgentInstructionMetadata instructions={},ProviderSetup* provider_setup=nullptr,GraphExecution* graphs=nullptr,ProviderProfileSetup* provider_profiles=nullptr,BackendOwnerControl* owner_control=nullptr);
    ~HttpServer();
    HttpServer(const HttpServer&)=delete;
    HttpServer& operator=(const HttpServer&)=delete;
    int bind(int port); // 0 selects an available loopback port.
    bool listen();
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
