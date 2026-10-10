#pragma once
#include "agentflow/mcp_tool_client.hpp"
namespace agentflow {
McpToolPage mcp_decode_tool_page(const McpWireMessage& message);
McpToolReply mcp_decode_tool_reply(std::string request_id,const McpWireMessage& message);
}
