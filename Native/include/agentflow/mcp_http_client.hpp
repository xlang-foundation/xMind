#pragma once
#include "agentflow/mcp_tool_client.hpp"
#include "agentflow/secret_protection.hpp"
#include <memory>
namespace agentflow {
// Native HTTP access owner. Trusted endpoint/credential only; one calling
// thread. Tool dispatch remains private to the durable approval registry.
// An interrupted owner is retired, never reconnected or used to replay effects.
class McpHttpClient final : public McpToolClient {
public:
    explicit McpHttpClient(std::string endpoint,std::optional<SecretBytes> bearer={});
    ~McpHttpClient() override;
    McpHttpClient(const McpHttpClient&)=delete;
    McpHttpClient& operator=(const McpHttpClient&)=delete;
    void connect(Deadline deadline,std::stop_token cancel={});
    McpToolPage list_tools(std::optional<std::string> cursor,Deadline deadline,std::stop_token cancel={}) override;
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
