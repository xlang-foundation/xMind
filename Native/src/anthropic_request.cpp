#include "agentflow/model_provider.hpp"
#include "agentflow/anthropic_history.hpp"
#include "nlohmann/json.hpp"
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t wire_limit=8*1024*1024;
Json object(const std::string& source){
    if(source.size()>1024*1024)throw std::invalid_argument("Claude JSON object exceeds limits");
    std::vector<std::set<std::string>> fields;
    try{
        auto value=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& parsed){
            if(depth>16)throw std::invalid_argument("Claude JSON nesting exceeds limits");
            if(event==Json::parse_event_t::object_start)fields.emplace_back();
            else if(event==Json::parse_event_t::object_end)fields.pop_back();
            else if(event==Json::parse_event_t::key&&!fields.back().insert(parsed.get<std::string>()).second)throw std::invalid_argument("Claude JSON has duplicate keys");
            return true;
        });
        if(value.is_object())return value;
    }catch(const Json::exception&){}
    throw std::invalid_argument("Claude request requires a valid JSON object");
}
std::string inner(std::string_view source){
    const auto begin=source.find_first_not_of(" \r\n\t"),end=source.find_last_not_of(" \r\n\t");
    if(begin==std::string_view::npos||source[begin]!='['||source[end]!=']')throw std::invalid_argument("Invalid Claude block array");
    return std::string(source.substr(begin+1,end-begin-1));
}
struct WireMessage {std::string role,content;};
void add(std::vector<WireMessage>& messages,std::size_t& content_bytes,std::string role,std::string content){
    if(content.size()>wire_limit)throw std::invalid_argument("Claude request exceeds limits");
    auto blocks=inner(content);if(blocks.find_first_not_of(" \r\n\t")==std::string::npos)throw IncompatibleProviderHistory("Claude assistant history has no supported content blocks");
    const bool merging=!messages.empty()&&messages.back().role==role;
    const auto added=blocks.size()+(merging?1:2);
    if(added>wire_limit-content_bytes)throw std::invalid_argument("Claude request exceeds limits");content_bytes+=added;
    if(merging){messages.back().content.pop_back();messages.back().content+=',';messages.back().content+=blocks;messages.back().content+=']';}
    else messages.push_back({std::move(role),'['+blocks+']'});
}
}
std::string serialize_anthropic_request(const ChatProviderConfig& config,const ModelRequest& request){
    if(request.canonical_window)throw IncompatibleProviderHistory("Canonical context requires the Responses wire");
    if(config.reasoning_effort)throw std::invalid_argument("Claude reasoning controls are not implemented");
    if(!request.max_output_tokens)throw std::invalid_argument("Claude requires an explicit output token limit");
    if(request.messages.empty()||request.messages.size()>4096||request.tools.size()>64)throw std::invalid_argument("Claude request exceeds configured counts");
    std::size_t receipt_bytes=0;
    for(const auto& message:request.messages)if(message.provider_items_json!="[]"){
        if(message.provider_items_json.size()>wire_limit-receipt_bytes)throw std::invalid_argument("Claude provider history exceeds limits");receipt_bytes+=message.provider_items_json.size();
    }
    // Reuse our native common identity, capability and history-order checks.
    // Its OpenAI-shaped output is discarded; no SDK implementation is involved.
    auto validation=request;std::vector<std::string> receipts(request.messages.size());
    for(std::size_t index=0;index<request.messages.size();++index)if(request.messages[index].provider_items_json!="[]"){
        receipts[index]=anthropic_history_content(request.messages[index]);
        // Remove only successfully validated Claude receipts in this dry copy.
        // Actual wire arrays below always retain their original raw source.
        validation.messages[index].provider_items_json="[]";
    }
    serialize_chat_request(config,validation);
    Json body={{"model",config.model},{"stream",true},{"max_tokens",*request.max_output_tokens}};
    if(!request.tools.empty()){
        body["tools"]=Json::array();
        for(const auto& tool:request.tools)body["tools"].push_back({{"name",tool.name},{"description",tool.description},{"input_schema",object(tool.input_schema_json)}});
    }
    bool conversational=false;std::vector<WireMessage> messages;std::size_t content_bytes=0;
    for(std::size_t index=0;index<request.messages.size();++index){
        const auto& message=request.messages[index];
        if(!message.refusal.empty())throw IncompatibleProviderHistory("Claude refusal history requires an explicit native mapping");
        if(message.role==MessageRole::developer)throw IncompatibleProviderHistory("Claude developer-role authority mapping is not implemented");
        if(message.role==MessageRole::system){
            if(conversational||message.content.empty())throw IncompatibleProviderHistory("Claude system instructions must precede conversation content");
            if(!body.contains("system"))body["system"]=Json::array();
            body["system"].push_back({{"type","text"},{"text",message.content}});continue;
        }
        conversational=true;const auto role=message.role==MessageRole::assistant?"assistant":"user";
        if(!receipts[index].empty()){add(messages,content_bytes,role,receipts[index]);continue;}
        std::string content="[";bool first=true;
        const auto block=[&](const std::string& value){if(!first)content+=',';first=false;content+=value;};
        if(message.role==MessageRole::tool)block(Json{{"type","tool_result"},{"tool_use_id",message.tool_call_id},{"content",message.content}}.dump());
        else{
            if(!message.content.empty())block(Json{{"type","text"},{"text",message.content}}.dump());
            for(const auto& call:message.tool_calls){
                // Legacy DTOs remain supported, with the same strict object
                // validation but without re-dumping numeric/escaped input.
                (void)object(call.arguments_json);
                block("{\"type\":\"tool_use\",\"id\":"+Json(call.id).dump()+",\"name\":"+Json(call.name).dump()+",\"input\":"+call.arguments_json+"}");
            }
        }
        if(first)throw std::invalid_argument("Claude message has no supported content blocks");
        content+=']';add(messages,content_bytes,role,std::move(content));
    }
    if(messages.empty())throw std::invalid_argument("Claude requires conversation content");
    try{
        auto encoded=body.dump();if(encoded.size()>wire_limit-16)throw std::invalid_argument("Claude request exceeds limits");encoded.pop_back();encoded+=",\"messages\":[";bool first=true;
        for(const auto& message:messages){
            const auto prefix="{\"role\":"+Json(message.role).dump()+",\"content\":";
            const auto added=prefix.size()+message.content.size()+1+(first?0:1);
            if(encoded.size()>wire_limit-2||added>wire_limit-2-encoded.size())throw std::invalid_argument("Claude request exceeds limits");
            if(!first)encoded+=',';first=false;encoded+=prefix;encoded+=message.content;encoded+='}';
        }
        encoded+="]}";return encoded;
    }
    catch(const Json::exception&){throw std::invalid_argument("Invalid Claude request encoding");}
}
}
