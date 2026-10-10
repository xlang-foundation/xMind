#pragma once
#include "agentflow/mcp_wire.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace agentflow {
struct McpHttpHeader {std::string name,value;};
// Bounded protocol metadata only. Credentials/endpoints/session ownership are
// supplied by the native HTTP owner, never by tool arguments or these headers.
std::vector<McpHttpHeader> mcp_http_request_headers(std::string_view request,
    McpWireEra era,std::string_view legacy_protocol="2025-11-25");
class McpHttpToolHeaders {
public:
    explicit McpHttpToolHeaders(std::string_view input_schema);
    std::vector<McpHttpHeader> project(std::string_view arguments) const;
private:
    struct Parameter {std::vector<std::string> path;std::string name,type;};
    std::vector<Parameter> parameters_;
};
}
