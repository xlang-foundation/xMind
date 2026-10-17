#pragma once
#include "agentflow/mcp_tool_client.hpp"
namespace agentflow {
McpToolPage mcp_decode_tool_page(const McpWireMessage& message);
McpToolReply mcp_decode_tool_reply(std::string request_id,const McpWireMessage& message,
    McpWireEra era=McpWireEra::modern);
std::string mcp_tool_call_params(const std::string& name,std::string_view arguments,
    const std::map<std::string,std::string>& input_responses={},
    const std::optional<std::string>& request_state={});
McpMetadataPage mcp_decode_resource_page(const McpWireMessage& message);
McpMetadataPage mcp_decode_resource_template_page(const McpWireMessage& message);
McpMetadataPage mcp_decode_prompt_page(const McpWireMessage& message);
McpReadOnlyReply mcp_decode_resource_reply(std::string request_id,const McpWireMessage& message);
McpReadOnlyReply mcp_decode_prompt_reply(std::string request_id,const McpWireMessage& message);
}
