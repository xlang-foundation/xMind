#include "agentflow/mcp_tool_codec.hpp"
#include "nlohmann/json.hpp"
#include <set>
#include <utility>
namespace agentflow {
namespace {
using Json=nlohmann::json;
std::string bounded_text(const Json& value,const char* key,std::size_t limit,bool required=true) {
    if(!value.contains(key)) {if(!required)return {};throw McpProtocolError("Missing MCP tool description field");}
    if(!value[key].is_string())throw McpProtocolError("Invalid MCP tool description field");
    auto text=value[key].get<std::string>();
    if(text.size()>limit || text.find('\0')!=std::string::npos || (required && text.empty()))throw McpProtocolError("MCP tool description exceeds limits");
    return text;
}
}
McpToolPage mcp_decode_tool_page(const McpWireMessage& message) {
    if(message.kind!=McpMessageKind::result)throw McpProtocolError("MCP peer rejected tool discovery");
    const auto value=Json::parse(message.payload_json);
    if(value.contains("resultType") && value["resultType"]!="complete")throw McpProtocolError("MCP tool discovery requires a complete result");
    if(!value.contains("tools") || !value["tools"].is_array() || value["tools"].size()>128)throw McpProtocolError("Invalid or excessive MCP tool page");
    McpToolPage page;std::set<std::string> names;
    for(const auto& raw:mcp_array_values(*mcp_object_member(message.payload_json,"tools"))) {
        const auto item=Json::parse(raw);
        if(!item.is_object())throw McpProtocolError("Invalid MCP tool description");
        auto name=bounded_text(item,"name",128);
        if(!names.insert(name).second)throw McpProtocolError("Duplicate MCP tool name in page");
        if(!item.contains("inputSchema") || !item["inputSchema"].is_object())throw McpProtocolError("Missing MCP input schema");
        if(item.contains("annotations") && !item["annotations"].is_object())throw McpProtocolError("Invalid MCP tool annotations");
        if(item.contains("outputSchema") && !item["outputSchema"].is_object())throw McpProtocolError("Invalid MCP output schema");
        page.tools.push_back({std::move(name),bounded_text(item,"description",65536,false),*mcp_object_member(raw,"inputSchema"),mcp_object_member(raw,"annotations").value_or("{}"),mcp_object_member(raw,"outputSchema")});
    }
    if(value.contains("nextCursor"))page.next_cursor=bounded_text(value,"nextCursor",4096);
    return page;
}
McpToolReply mcp_decode_tool_reply(std::string request_id,const McpWireMessage& message) {
    if(message.kind!=McpMessageKind::result)throw McpProtocolError("MCP tool returned an RPC error");
    const auto value=Json::parse(message.payload_json);
    if(value.contains("resultType") && value["resultType"]!="complete")throw McpProtocolError("MCP interactive result requires an unsupported continuation owner");
    if(!value.contains("content") || !value["content"].is_array() || value["content"].size()>128 || (value.contains("isError") && !value["isError"].is_boolean()))throw McpProtocolError("Invalid MCP tool result");
    for(const auto& block:value["content"]) {
        if(!block.is_object() || !block.contains("type") || !block["type"].is_string())throw McpProtocolError("Invalid MCP content block");
        const auto type=block["type"].get<std::string>();
        if(type=="text") {if(!block.contains("text") || !block["text"].is_string())throw McpProtocolError("Invalid MCP text block");}
        else if(type=="image" || type=="audio") {if(!block.contains("data") || !block["data"].is_string() || !block.contains("mimeType") || !block["mimeType"].is_string())throw McpProtocolError("Invalid MCP media block");}
        else if(type=="resource") {if(!block.contains("resource") || !block["resource"].is_object() || !block["resource"].contains("uri") || !block["resource"]["uri"].is_string())throw McpProtocolError("Invalid MCP resource block");}
        else if(type=="resource_link") {if(!block.contains("uri") || !block["uri"].is_string() || !block.contains("name") || !block["name"].is_string())throw McpProtocolError("Invalid MCP resource link");}
        else throw McpProtocolError("Unsupported MCP content block");
    }
    return {std::move(request_id),message.raw_json,message.payload_json,value.value("isError",false)};
}
}
