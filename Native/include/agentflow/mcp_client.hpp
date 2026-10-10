#pragma once
#include "agentflow/mcp_tool_client.hpp"
#include "agentflow/mcp_stdio.hpp"

namespace agentflow {
// Native stdio access owner. Trusted backend configuration only; one calling
// thread. Discovers descriptions, not executable permissions. Only the journal-
// backed registry can access private tools/call dispatch. No auto-restart
// or request replay; any protocol/transport interruption retires this owner.
class McpStdioClient final : public McpToolClient {
public:
    using Deadline=McpStdioProcess::Deadline;
    explicit McpStdioClient(const McpStdioConfiguration& configuration);
    ~McpStdioClient() override;
    McpStdioClient(const McpStdioClient&)=delete;
    McpStdioClient& operator=(const McpStdioClient&)=delete;
    void connect(Deadline deadline,std::stop_token cancel={},
        std::chrono::milliseconds probe_window=std::chrono::seconds(1));
    McpToolPage list_tools(std::optional<std::string> cursor,Deadline deadline,
        std::stop_token cancel={}) override;
    bool ready() const override;
    const McpServerDescription& server() const override;
    McpStdioStatus status() const;
    void shutdown() noexcept override;
private:
    McpToolReply call_tool(const std::string& name,std::string_view arguments,
        Deadline deadline,std::stop_token cancel) override;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
