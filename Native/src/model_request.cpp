#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <map>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
void require_capability(Capability capability,const char* feature) {
    if(capability!=Capability::supported) throw std::invalid_argument(std::string("Model capability is unknown or unsupported: ")+feature);
}
void function_name(const std::string& name) {
    if(name.empty() || name.size()>64) throw std::invalid_argument("Invalid function name");
    for(unsigned char c:name) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-')) throw std::invalid_argument("Invalid function name");
}
std::string role_name(MessageRole role) {
    switch(role) {
    case MessageRole::system:return "system";case MessageRole::developer:return "developer";
    case MessageRole::user:return "user";case MessageRole::assistant:return "assistant";case MessageRole::tool:return "tool";
    }
    throw std::invalid_argument("Invalid message role");
}
Json object_json(const std::string& source,const char* error) {
    if(source.size()>1024*1024) throw std::invalid_argument(error);
    try {auto parsed=Json::parse(source);if(parsed.is_object()) return parsed;}
    catch(const Json::exception&) {}
    throw std::invalid_argument(error);
}
}
std::string serialize_chat_request(const ChatProviderConfig& config,const ModelRequest& request) {
    if(config.model.empty() || config.model.size()>512 || config.model.find('\0')!=std::string::npos || config.endpoint.empty()) throw std::invalid_argument("Missing or invalid model configuration");
    if(request.messages.empty() || request.messages.size()>4096 || request.tools.size()>64) throw std::invalid_argument("Model request exceeds configured limits");
    std::size_t input_bytes=config.model.size();
    auto account=[&](std::size_t size) {
        if(size>8*1024*1024-input_bytes) throw std::invalid_argument("Model request exceeds configured limits");
        input_bytes+=size;
    };
    Json body={{"model",config.model},{"stream",true},{"n",1},{"messages",Json::array()}};
    std::set<std::string> names,seen_calls,pending;
    if(!request.tools.empty()) {
        require_capability(config.tools,"function calls");body["tools"]=Json::array();
        for(const auto& tool:request.tools) {
            account(tool.name.size());account(tool.description.size());account(tool.input_schema_json.size());
            function_name(tool.name);
            if(!names.insert(tool.name).second || tool.description.size()>65536) throw std::invalid_argument("Invalid or duplicate tool definition");
            const auto schema=object_json(tool.input_schema_json,"Invalid tool schema JSON");
            body["tools"].push_back({{"type","function"},{"function",{{"name",tool.name},{"description",tool.description},{"parameters",schema}}}});
        }
    }
    for(const auto& message:request.messages) {
        account(message.content.size());account(message.tool_call_id.size());
        if(message.content.size()>4*1024*1024 || message.tool_calls.size()>64) throw std::invalid_argument("Model message exceeds configured limits");
        Json item={{"role",role_name(message.role)},{"content",message.content}};
        if(message.role==MessageRole::tool) {
            require_capability(config.tools,"function calls");
            if(!message.tool_calls.empty() || message.tool_call_id.empty() || pending.erase(message.tool_call_id)!=1) throw std::invalid_argument("Tool response does not match a pending call");
            item["tool_call_id"]=message.tool_call_id;
        } else {
            if(!pending.empty()) throw std::invalid_argument("Tool results must precede the next message");
            if(!message.tool_call_id.empty()) throw std::invalid_argument("Only tool responses carry tool_call_id");
            if(!message.tool_calls.empty()) {
                require_capability(config.tools,"function calls");
                if(message.role!=MessageRole::assistant) throw std::invalid_argument("Only assistant messages carry tool calls");
                item["tool_calls"]=Json::array();
                for(const auto& call:message.tool_calls) {
                    account(call.id.size());account(call.name.size());account(call.arguments_json.size());
                    function_name(call.name);
                    if(call.id.empty() || call.id.size()>256 || call.id.find('\0')!=std::string::npos || !seen_calls.insert(call.id).second) throw std::invalid_argument("Invalid or duplicate tool call identity");
                    object_json(call.arguments_json,"Invalid tool argument JSON");pending.insert(call.id);
                    item["tool_calls"].push_back({{"id",call.id},{"type","function"},{"function",{{"name",call.name},{"arguments",call.arguments_json}}}});
                }
            }
        }
        body["messages"].push_back(std::move(item));
    }
    if(!pending.empty()) throw std::invalid_argument("Model continuation requires all pending tool results");
    if(request.include_usage) {require_capability(config.stream_usage,"streaming usage");body["stream_options"]={{"include_usage",true}};}
    if(request.max_output_tokens) {
        require_capability(config.output_limit,"output token limit");
        if(*request.max_output_tokens<=0) throw std::invalid_argument("Invalid output token limit");
        body["max_completion_tokens"]=*request.max_output_tokens;
    }
    try {
        auto encoded=body.dump();if(encoded.size()>8*1024*1024) throw std::invalid_argument("Model request exceeds configured limits");return encoded;
    } catch(const Json::exception&) {throw std::invalid_argument("Invalid UTF-8 model request");}
}
}
