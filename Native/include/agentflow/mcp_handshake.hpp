#pragma once
#include "agentflow/mcp_requests.hpp"

namespace agentflow {
struct McpServerDescription {
    McpWireEra era;
    std::string protocol_version,capabilities_json,server_info_json,instructions;
};
struct McpHandshakeAction {
    std::optional<McpPreparedRequest> request;
    std::string notification;
};
// Transport-neutral negotiation state. Send returned frames through the real
// binding; legacy readiness requires confirmation after initialized is written.
// Server identity/instructions are untrusted descriptive data, not authority.
class McpHandshake {
public:
    explicit McpHandshake(McpRequestTracker& requests,McpClientIdentity identity={});
    ~McpHandshake();
    McpHandshake(const McpHandshake&)=delete;
    McpHandshake& operator=(const McpHandshake&)=delete;
    McpPreparedRequest begin();
    McpHandshakeAction accept(const McpCorrelatedReply& reply);
    McpPreparedRequest probe_timeout(); // same stdio process; late probe replies retired
    void initialized_sent();
    bool ready() const;
    const McpServerDescription& server() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
