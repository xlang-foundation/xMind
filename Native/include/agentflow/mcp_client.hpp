#pragma once
#include "agentflow/mcp_handshake.hpp"
#include "agentflow/mcp_stdio.hpp"

namespace agentflow {
struct McpToolDescription {
    std::string name,description,input_schema_json,annotations_json;
};
struct McpToolPage {
    std::vector<McpToolDescription> tools;
    std::optional<std::string> next_cursor;
};
// Native stdio access owner. Trusted backend configuration only; one calling
// thread. Discovers descriptions, not executable permissions. No tools/call
// API exists until effect admission/journaling is integrated. No auto-restart
// or request replay; any protocol/transport interruption retires this owner.
class McpStdioClient {
public:
    using Deadline=McpStdioProcess::Deadline;
    explicit McpStdioClient(const McpStdioConfiguration& configuration);
    ~McpStdioClient();
    McpStdioClient(const McpStdioClient&)=delete;
    McpStdioClient& operator=(const McpStdioClient&)=delete;
    void connect(Deadline deadline,std::stop_token cancel={},
        std::chrono::milliseconds probe_window=std::chrono::seconds(1));
    McpToolPage list_tools(std::optional<std::string> cursor,Deadline deadline,
        std::stop_token cancel={});
    bool ready() const;
    const McpServerDescription& server() const;
    McpStdioStatus status() const;
    void shutdown() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
