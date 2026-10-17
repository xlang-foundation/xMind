#pragma once
#include "agentflow/mcp_tool_client.hpp"
namespace agentflow {
McpToolPage mcp_decode_tool_page(const McpWireMessage& message);
McpToolReply mcp_decode_tool_reply(std::string request_id,const McpWireMessage& message,
    McpWireEra era=McpWireEra::modern);
McpMetadataPage mcp_decode_resource_page(const McpWireMessage& message);
McpMetadataPage mcp_decode_resource_template_page(const McpWireMessage& message);
McpMetadataPage mcp_decode_prompt_page(const McpWireMessage& message);
McpReadOnlyReply mcp_decode_resource_reply(std::string request_id,const McpWireMessage& message);
McpReadOnlyReply mcp_decode_prompt_reply(std::string request_id,const McpWireMessage& message);
}
