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
void validate_content(const Json& block) {
    if(!block.is_object() || !block.contains("type") || !block["type"].is_string())throw McpProtocolError("Invalid MCP content block");
    const auto type=block["type"].get<std::string>();
    if(type=="text") {if(!block.contains("text") || !block["text"].is_string() || block["text"].get_ref<const std::string&>().size()>512*1024)throw McpProtocolError("Invalid MCP text block");}
    else if(type=="image" || type=="audio") {if(!block.contains("data") || !block["data"].is_string() || block["data"].get_ref<const std::string&>().size()>768*1024 || !block.contains("mimeType") || !block["mimeType"].is_string() || block["mimeType"].get_ref<const std::string&>().size()>256)throw McpProtocolError("Invalid MCP media block");}
    else if(type=="resource") {
        if(!block.contains("resource") || !block["resource"].is_object() || !block["resource"].contains("uri") || !block["resource"]["uri"].is_string())throw McpProtocolError("Invalid MCP embedded resource block");
        const auto& resource=block["resource"];const bool text=resource.contains("text"),blob=resource.contains("blob");
        if(text==blob || (text&&(!resource["text"].is_string() || resource["text"].get_ref<const std::string&>().size()>512*1024)) || (blob&&(!resource["blob"].is_string() || resource["blob"].get_ref<const std::string&>().size()>768*1024)))throw McpProtocolError("Invalid MCP embedded resource contents");
    }
    else if(type=="resource_link") {if(!block.contains("uri") || !block["uri"].is_string() || !block.contains("name") || !block["name"].is_string())throw McpProtocolError("Invalid MCP resource link");}
    else throw McpProtocolError("Unsupported MCP content block");
}
McpMetadataPage metadata_page(const McpWireMessage& message,const char* collection,const char* identity) {
    if(message.kind!=McpMessageKind::result)throw McpProtocolError("MCP peer rejected metadata discovery");
    const auto value=Json::parse(message.payload_json);
    if(value.contains("resultType") && value["resultType"]!="complete")throw McpProtocolError("MCP metadata discovery requires a complete result");
    if(!value.contains(collection) || !value[collection].is_array() || value[collection].size()>128)throw McpProtocolError("Invalid or excessive MCP metadata page");
    McpMetadataPage page;std::set<std::string> ids;
    for(const auto& raw:mcp_array_values(*mcp_object_member(message.payload_json,collection))) {
        const auto item=Json::parse(raw);if(!item.is_object())throw McpProtocolError("Invalid MCP metadata entry");
        const auto id=bounded_text(item,identity,8192);if(!ids.insert(id).second)throw McpProtocolError("Duplicate MCP metadata identity");
        if(collection==std::string("resources")) {
            (void)bounded_text(item,"name",128);
            for(const auto* field:{"title","description","mimeType"})if(item.contains(field))(void)bounded_text(item,field,field==std::string("description")?65536:4096,false);
            if(item.contains("size")&&(!item["size"].is_number_integer()||item["size"]<0||item["size"]>9007199254740991LL))throw McpProtocolError("Invalid MCP resource size");
            if(item.contains("annotations")&&!item["annotations"].is_object())throw McpProtocolError("Invalid MCP resource annotations");
        } else if(collection==std::string("resourceTemplates")) {
            (void)bounded_text(item,"name",128);
            (void)bounded_text(item,"uriTemplate",8192);
            for(const auto* field:{"title","description","mimeType"})if(item.contains(field))(void)bounded_text(item,field,field==std::string("description")?65536:4096,false);
            if(item.contains("annotations")&&!item["annotations"].is_object())throw McpProtocolError("Invalid MCP resource-template annotations");
            if(item.contains("_meta")&&!item["_meta"].is_object())throw McpProtocolError("Invalid MCP resource-template metadata");
        } else {
            (void)bounded_text(item,"name",128);
            for(const auto* field:{"title","description"})if(item.contains(field))(void)bounded_text(item,field,field==std::string("description")?65536:4096,false);
            if(item.contains("arguments")) {
                if(!item["arguments"].is_array()||item["arguments"].size()>64)throw McpProtocolError("Invalid MCP prompt arguments");std::set<std::string> names;
                for(const auto& argument:item["arguments"]) {if(!argument.is_object())throw McpProtocolError("Invalid MCP prompt argument");const auto name=bounded_text(argument,"name",128);if(!names.insert(name).second)throw McpProtocolError("Duplicate MCP prompt argument");if(argument.contains("description"))(void)bounded_text(argument,"description",65536,false);if(argument.contains("required")&&!argument["required"].is_boolean())throw McpProtocolError("Invalid MCP prompt argument requirement");}
            }
        }
        if(raw.size()>256*1024)throw McpProtocolError("MCP metadata entry exceeds limits");page.entries_json.push_back(raw);
    }
    if(value.contains("nextCursor"))page.next_cursor=bounded_text(value,"nextCursor",4096);return page;
}
McpReadOnlyReply read_reply(std::string request_id,const McpWireMessage& message,bool prompt) {
    if(message.kind!=McpMessageKind::result)throw McpProtocolError(prompt?"MCP peer rejected prompt retrieval":"MCP peer rejected resource read");
    const auto value=Json::parse(message.payload_json);if(value.contains("resultType")&&value["resultType"]!="complete")throw McpProtocolError("MCP read-only operation requires a complete result");
    if(prompt) {
        if(value.contains("description"))(void)bounded_text(value,"description",65536,false);
        if(!value.contains("messages")||!value["messages"].is_array()||value["messages"].empty()||value["messages"].size()>128)throw McpProtocolError("Invalid MCP prompt messages");
        for(const auto& item:value["messages"]) {if(!item.is_object()||!item.contains("role")||!item["role"].is_string()||(item["role"]!="user"&&item["role"]!="assistant")||!item.contains("content"))throw McpProtocolError("Invalid MCP prompt message");validate_content(item["content"]);}
    } else {
        if(!value.contains("contents")||!value["contents"].is_array()||value["contents"].empty()||value["contents"].size()>128)throw McpProtocolError("Invalid MCP resource contents");
        for(const auto& item:value["contents"]) {if(!item.is_object()||!item.contains("uri")||!item["uri"].is_string())throw McpProtocolError("Invalid MCP resource content");const bool text=item.contains("text"),blob=item.contains("blob");if(text==blob||(text&&(!item["text"].is_string()||item["text"].get_ref<const std::string&>().size()>512*1024))||(blob&&(!item["blob"].is_string()||item["blob"].get_ref<const std::string&>().size()>768*1024)))throw McpProtocolError("Invalid MCP resource data");if(item.contains("mimeType")&&(!item["mimeType"].is_string()||item["mimeType"].get_ref<const std::string&>().size()>256))throw McpProtocolError("Invalid MCP resource MIME type");}
    }
    return {std::move(request_id),message.raw_json,message.payload_json};
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
McpToolReply mcp_decode_tool_reply(std::string request_id,const McpWireMessage& message,McpWireEra era) {
    if(message.kind!=McpMessageKind::result)throw McpProtocolError("MCP tool returned an RPC error");
    const auto value=Json::parse(message.payload_json);
    if(era==McpWireEra::modern) {
        if(!value.contains("resultType")||!value["resultType"].is_string())throw McpProtocolError("Modern MCP tool result is missing its result type");
        if(value["resultType"]=="input_required") {
            McpInputRequired input;
            if(value.contains("inputRequests")) {
                const auto& requests=value["inputRequests"];
                if(!requests.is_object()||requests.size()>32)throw McpProtocolError("Invalid MCP input-required requests");
                for(auto it=requests.begin();it!=requests.end();++it) {
                    if(it.key().empty()||it.key().size()>128||it.key().find('\\0')!=std::string::npos||!it.value().is_object())throw McpProtocolError("Invalid MCP input request identity or shape");
                    const auto raw=mcp_object_member(message.payload_json,"inputRequests");
                    if(!raw)throw McpProtocolError("MCP input-required requests are missing");
                    const auto encoded=mcp_object_member(*raw,it.key());
                    if(!encoded||encoded->size()>65536)throw McpProtocolError("MCP input request exceeds limits");
                    if(!it.value().contains("method")||!it.value()["method"].is_string())throw McpProtocolError("MCP input request has no method");
                    const auto method=it.value()["method"].get<std::string>();
                    if(method!="elicitation/create"&&method!="sampling/createMessage"&&method!="roots/list")throw McpProtocolError("Unsupported MCP input request method");
                    if(it.value().contains("params")&&!it.value()["params"].is_object())throw McpProtocolError("Invalid MCP input request parameters");
                    input.input_requests_json.emplace(it.key(),*encoded);
                }
            }
            if(value.contains("requestState")) {
                if(!value["requestState"].is_string()||value["requestState"].get_ref<const std::string&>().size()>65536)throw McpProtocolError("Invalid MCP opaque request state");
                input.request_state=value["requestState"].get<std::string>();
            }
            if(input.input_requests_json.empty()&&!input.request_state)throw McpProtocolError("MCP input-required result contains no continuation state");
            return {std::move(request_id),message.raw_json,message.payload_json,false,std::move(input)};
        }
        if(value["resultType"]!="complete")throw McpProtocolError("Unsupported modern MCP tool result type");
    } else if(value.contains("resultType")) {
        // The discriminator is not part of the legacy result contract. Ignore
        // it as an extension field; a legacy result still needs normal content.
    }
    if(!value.contains("content") || !value["content"].is_array() || value["content"].size()>128 || (value.contains("isError") && !value["isError"].is_boolean()))throw McpProtocolError("Invalid MCP tool result");
    for(const auto& block:value["content"])validate_content(block);
    return {std::move(request_id),message.raw_json,message.payload_json,value.value("isError",false)};
}
std::string mcp_tool_call_params(const std::string& name,std::string_view arguments,
    const std::map<std::string,std::string>& input_responses,const std::optional<std::string>& request_state) {
    if(name.empty()||name.size()>128||name.find('\0')!=std::string::npos||input_responses.size()>32)throw McpProtocolError("Invalid MCP tool continuation parameters");
    const auto compact=mcp_compact_object(arguments);std::string result="{\"name\":"+Json(name).dump()+",\"arguments\":"+compact;
    if(!input_responses.empty()) {
        result+=",\"inputResponses\":{";bool first=true;
        for(const auto& [identity,response]:input_responses) {
            if(identity.empty()||identity.size()>128||identity.find('\0')!=std::string::npos)throw McpProtocolError("Invalid MCP input response identity");
            if(!first)result.push_back(',');first=false;result+=Json(identity).dump();result.push_back(':');result+=mcp_compact_object(response);
        }
        result.push_back('}');
    }
    if(request_state) {
        if(request_state->size()>65536||request_state->find('\0')!=std::string::npos)throw McpProtocolError("Invalid MCP opaque request state");
        result+=",\"requestState\":"+Json(*request_state).dump();
    }
    result.push_back('}');(void)mcp_compact_object(result);return result;
}
McpMetadataPage mcp_decode_resource_page(const McpWireMessage& message){return metadata_page(message,"resources","uri");}
McpMetadataPage mcp_decode_resource_template_page(const McpWireMessage& message){return metadata_page(message,"resourceTemplates","uriTemplate");}
McpMetadataPage mcp_decode_prompt_page(const McpWireMessage& message){return metadata_page(message,"prompts","name");}
McpReadOnlyReply mcp_decode_resource_reply(std::string request_id,const McpWireMessage& message){return read_reply(std::move(request_id),message,false);}
McpReadOnlyReply mcp_decode_prompt_reply(std::string request_id,const McpWireMessage& message){return read_reply(std::move(request_id),message,true);}
}
