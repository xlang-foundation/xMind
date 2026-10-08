#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
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
}
std::string serialize_anthropic_request(const ChatProviderConfig& config,const ModelRequest& request){
    if(config.reasoning_effort)throw std::invalid_argument("Claude reasoning controls are not implemented");
    if(!request.max_output_tokens)throw std::invalid_argument("Claude requires an explicit output token limit");
    // Reuse our native common identity, capability and history-order checks.
    // Its OpenAI-shaped output is discarded; no SDK implementation is involved.
    serialize_chat_request(config,request);
    Json body={{"model",config.model},{"stream",true},{"max_tokens",*request.max_output_tokens},{"messages",Json::array()}};
    if(!request.tools.empty()){
        body["tools"]=Json::array();
        for(const auto& tool:request.tools)body["tools"].push_back({{"name",tool.name},{"description",tool.description},{"input_schema",object(tool.input_schema_json)}});
    }
    bool conversational=false;
    for(const auto& message:request.messages){
        if(!message.refusal.empty())throw IncompatibleProviderHistory("Claude refusal history requires an explicit native mapping");
        if(message.role==MessageRole::developer)throw IncompatibleProviderHistory("Claude developer-role authority mapping is not implemented");
        if(message.role==MessageRole::system){
            if(conversational||message.content.empty())throw IncompatibleProviderHistory("Claude system instructions must precede conversation content");
            if(!body.contains("system"))body["system"]=Json::array();
            body["system"].push_back({{"type","text"},{"text",message.content}});continue;
        }
        conversational=true;auto content=Json::array();const auto role=message.role==MessageRole::assistant?"assistant":"user";
        if(message.role==MessageRole::tool)content.push_back({{"type","tool_result"},{"tool_use_id",message.tool_call_id},{"content",message.content}});
        else{
            if(!message.content.empty())content.push_back({{"type","text"},{"text",message.content}});
            for(const auto& call:message.tool_calls)content.push_back({{"type","tool_use"},{"id",call.id},{"name",call.name},{"input",object(call.arguments_json)}});
        }
        if(content.empty())throw std::invalid_argument("Claude message has no supported content blocks");
        auto& messages=body["messages"];
        if(!messages.empty()&&messages.back()["role"]==role){for(auto& block:content)messages.back()["content"].push_back(std::move(block));}
        else messages.push_back({{"role",role},{"content",std::move(content)}});
    }
    if(body["messages"].empty())throw std::invalid_argument("Claude requires conversation content");
    try{auto encoded=body.dump();if(encoded.size()>8*1024*1024)throw std::invalid_argument("Claude request exceeds limits");return encoded;}
    catch(const Json::exception&){throw std::invalid_argument("Invalid Claude request encoding");}
}
}
