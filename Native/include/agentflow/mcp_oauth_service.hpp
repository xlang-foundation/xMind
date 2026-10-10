#pragma once
#include "agentflow/mcp_oauth_setup.hpp"
#include "agentflow/mcp_configuration.hpp"

namespace agentflow {
// Native asynchronous public-client login owner. Uses real discovery, callback,
// code exchange and encrypted persistence. Observation never owns its lifetime.
class McpOAuthService final : public McpOAuthSetup {
public:
    McpOAuthService(PersistenceService& store,std::vector<McpServerSetting> settings);
    ~McpOAuthService() override;
    std::vector<McpOAuthServerStatus> servers() override;
    McpOAuthAttemptStatus start(std::string server_id,std::int64_t config_revision,
        std::int64_t credential_revision,std::string request_id) override;
    McpOAuthAttemptStatus renew(std::string server_id,std::int64_t config_revision,
        std::int64_t credential_revision,std::string request_id) override;
    McpOAuthAttemptStatus status(const std::string& id) override;
    McpOAuthAttemptStatus cancel(const std::string& id) override;
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
