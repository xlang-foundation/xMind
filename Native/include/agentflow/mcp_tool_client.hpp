#pragma once
#include "agentflow/mcp_handshake.hpp"
#include "agentflow/mcp_transport.hpp"
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
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
struct McpToolPage {
    std::vector<McpToolDescription> tools;
    std::optional<std::string> next_cursor;
};
class McpToolRegistry;
// One native access owner; discovery is metadata, not execution permission.
// All transport bindings dispatch only through the same journal-backed registry.
// A lost reply must preserve possibly_sent; changing transport cannot replay it.
class McpToolClient {
public:
    using Deadline=McpDeadline;
    virtual ~McpToolClient()=default;
    virtual McpToolPage list_tools(std::optional<std::string> cursor,Deadline deadline,
        std::stop_token cancel={})=0;
    virtual bool ready() const=0;
    virtual const McpServerDescription& server() const=0;
    virtual void shutdown() noexcept=0;
private:
    friend class McpToolRegistry;
    virtual McpToolReply call_tool(const std::string& name,std::string_view arguments,
        Deadline deadline,std::stop_token cancel)=0;
};
}
