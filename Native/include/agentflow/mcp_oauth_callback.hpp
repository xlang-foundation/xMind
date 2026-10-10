#pragma once
#include "agentflow/mcp_oauth.hpp"
#include <cstdint>

namespace agentflow {
// Backend-owned, one-attempt loopback redirect receiver. Bind before creating
// the authorization attempt and register/use its exact redirect URI. Receiving
// a code grants no tool authority and performs no token exchange or persistence.
class McpOAuthLoopbackCallback {
public:
    explicit McpOAuthLoopbackCallback(std::string path="/oauth/callback",std::uint16_t port=0);
    ~McpOAuthLoopbackCallback();
    McpOAuthLoopbackCallback(const McpOAuthLoopbackCallback&)=delete;
    McpOAuthLoopbackCallback& operator=(const McpOAuthLoopbackCallback&)=delete;
    std::string redirect_uri() const;
    void receive(McpOAuthAuthorizationAttempt& attempt,McpDeadline deadline,std::stop_token cancel={});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
