#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agentflow {
struct McpOAuthServerStatus {
    std::string id,state;
    std::int64_t config_revision=0,credential_revision=0;
    bool enabled=false,configured=false;
    std::optional<std::int64_t> expires_unix_ms;
};
struct McpOAuthAttemptStatus {
    std::string id,server_id,state,authorization_url,reason;
    std::int64_t config_revision=0,credential_revision=0,expires_unix_ms=0;
    bool cancellation_requested=false;
};
// Authenticated local-owner commands. Destinations, registration and credentials
// are backend policy; clients supply only identities and observed revisions.
class McpOAuthSetup {
public:
    virtual ~McpOAuthSetup()=default;
    virtual std::vector<McpOAuthServerStatus> servers()=0;
    virtual McpOAuthAttemptStatus start(std::string server_id,std::int64_t config_revision,
        std::int64_t credential_revision,std::string request_id)=0;
    virtual McpOAuthAttemptStatus status(const std::string& id)=0;
    virtual McpOAuthAttemptStatus cancel(const std::string& id)=0;
};
}
