#pragma once
#include "agentflow/mcp_tool_client.hpp"

namespace agentflow {
// Shared protocol validation for subscriptions/listen, independent of whether
// notifications arrive in HTTP SSE frames or stdio JSON-RPC messages.
std::string mcp_subscription_request_params(const McpSubscriptionFilter& requested);

class McpSubscriptionTracker {
public:
    McpSubscriptionTracker(McpSubscriptionFilter requested,const McpServerDescription& server,
        std::string request_id);
    void notification(const McpWireMessage& message);
    void complete(const McpWireMessage& response);
    const std::optional<McpSubscriptionAcknowledgement>& acknowledgement() const noexcept;
private:
    McpSubscriptionFilter requested_;
    const McpServerDescription& server_;
    std::string request_id_;
    std::optional<McpSubscriptionAcknowledgement> acknowledgement_;
};
}
