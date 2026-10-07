#pragma once
#include "agentflow/mcp_handshake.hpp"
#include "agentflow/mcp_stdio.hpp"

namespace agentflow {
struct McpToolDescription {
    std::string name,description,input_schema_json,annotations_json;
    std::optional<std::string> output_schema_json;
};
struct McpToolReply {std::string request_id,response_json,result_json;bool is_error;};
struct McpDispatchFailure : McpTransportError {
    bool possibly_sent;
    std::string request_id,response_json;
    McpDispatchFailure(bool sent,std::string id,std::string response):McpTransportError("MCP tool dispatch did not complete"),possibly_sent(sent),request_id(std::move(id)),response_json(std::move(response)) {}
};
class McpToolRegistry;
struct McpToolPage {
    std::vector<McpToolDescription> tools;
    std::optional<std::string> next_cursor;
};
// Native stdio access owner. Trusted backend configuration only; one calling
// thread. Discovers descriptions, not executable permissions. Only the journal-
// backed registry can access private tools/call dispatch. No auto-restart
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
    friend class McpToolRegistry; // Only the journal-backed backend owner dispatches.
    McpToolReply call_tool(const std::string& name,std::string_view arguments,
        Deadline deadline,std::stop_token cancel);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
