#pragma once
#include "agentflow/mcp_tool_client.hpp"
#include "agentflow/secret_protection.hpp"
#include <functional>
#include <memory>
namespace agentflow {
struct McpBearerValidity {std::int64_t acquired_unix_ms;std::optional<std::int64_t> expires_unix_ms;};
// Native HTTP access owner. Trusted endpoint/credential only; one calling
// thread. Tool dispatch remains private to the durable approval registry.
// An interrupted owner is retired, never reconnected or used to replay effects.
class McpHttpClient final : public McpToolClient {
public:
    explicit McpHttpClient(std::string endpoint,std::optional<SecretBytes> bearer={},std::optional<McpBearerValidity> validity={});
    ~McpHttpClient() override;
    McpHttpClient(const McpHttpClient&)=delete;
    McpHttpClient& operator=(const McpHttpClient&)=delete;
    void connect(Deadline deadline,std::stop_token cancel={});
    McpToolPage list_tools(std::optional<std::string> cursor,Deadline deadline,std::stop_token cancel={}) override;
    McpMetadataPage list_resources(std::optional<std::string> cursor,Deadline deadline,std::stop_token cancel={}) override;
    McpReadOnlyReply read_resource(const std::string& uri,Deadline deadline,std::stop_token cancel={}) override;
    McpMetadataPage list_prompts(std::optional<std::string> cursor,Deadline deadline,std::stop_token cancel={}) override;
    McpReadOnlyReply get_prompt(const std::string& name,std::string_view arguments_json,Deadline deadline,std::stop_token cancel={}) override;
    // Blocks until the server gracefully closes the request-scoped SSE stream
    // or the caller cancels it. Cancellation closes that HTTP response stream.
    McpSubscriptionAcknowledgement listen_subscriptions(const McpSubscriptionFilter& requested,
        const std::function<void(const McpWireMessage&)>& on_notification,Deadline deadline,std::stop_token cancel={},
        const std::function<void(const McpSubscriptionAcknowledgement&)>& on_acknowledgement={}) override;
    bool ready() const override;
    const McpServerDescription& server() const override;
    std::size_t rejected_tools() const;
    void shutdown() noexcept override;
private:
    McpToolReply call_tool(const std::string& name,std::string_view arguments,Deadline deadline,std::stop_token cancel) override;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
