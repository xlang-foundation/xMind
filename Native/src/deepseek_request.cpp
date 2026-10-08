#include "agentflow/deepseek_provider.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t request_limit=8*1024*1024,receipt_limit=4*1024*1024;
[[noreturn]] void incompatible(){throw IncompatibleProviderHistory("DeepSeek receipt differs from conversation");}
Json parse_receipt(const std::string& source){
    if(source.size()>receipt_limit)incompatible();
    std::vector<std::set<std::string>> fields;
    try{return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>64)incompatible();
        if(event==Json::parse_event_t::object_start)fields.emplace_back();
        else if(event==Json::parse_event_t::object_end)fields.pop_back();
        else if(event==Json::parse_event_t::key&&!fields.back().insert(value.get<std::string>()).second)incompatible();
        return true;
    });}catch(const Json::exception&){incompatible();}
}
Json receipt(const ModelMessage& message,const std::string& model){
    if(message.role!=MessageRole::assistant||!message.refusal.empty())incompatible();
    const auto saved=parse_receipt(message.provider_items_json);
    if(!saved.is_array()||saved.size()!=1)incompatible();
    const auto& entry=saved[0];
    if(!entry.is_object()||entry.size()!=3||entry.value("type",std::string{})!="deepseek_assistant"||entry.value("model",std::string{})!=model||!entry.contains("message"))incompatible();
    const auto& original=entry["message"];
    if(!original.is_object()||original.value("role",std::string{})!="assistant"||
        !original.contains("content")||!original["content"].is_string()||original["content"]!=message.content)incompatible();
    for(const auto& [key,value]:original.items()){
        (void)value;if(key!="role"&&key!="content"&&key!="reasoning_content"&&key!="tool_calls")incompatible();
    }
    if(original.contains("reasoning_content")&&!original["reasoning_content"].is_string()&&!original["reasoning_content"].is_null())incompatible();
    const auto calls=original.value("tool_calls",Json::array());
    if(!calls.is_array()||calls.size()!=message.tool_calls.size())incompatible();
    for(std::size_t i=0;i<calls.size();++i){
        const auto& call=calls[i];const auto& actual=message.tool_calls[i];
        if(!call.is_object()||call.size()!=3||call.value("id",std::string{})!=actual.id||call.value("type",std::string{})!="function"||!call.contains("function"))incompatible();
        const auto& function=call["function"];
        if(!function.is_object()||function.size()!=2||function.value("name",std::string{})!=actual.name||function.value("arguments",std::string{})!=actual.arguments_json)incompatible();
    }
    return original;
}
}
std::string serialize_deepseek_request(const ChatProviderConfig& config,const ModelRequest& request){
    if(config.wire!=ProviderWire::chat_completions||config.chat_dialect!=ChatDialect::deepseek)
        throw std::invalid_argument("DeepSeek requires its explicit native chat policy");
    if(request.canonical_window)throw IncompatibleProviderHistory("Canonical context requires the Responses wire");
    if(request.max_output_tokens&&(*request.max_output_tokens<=0||*request.max_output_tokens>393216))
        throw std::invalid_argument("Invalid DeepSeek output limit");
    // Reuse native complete tool-pair/schema/UTF8/size validation. The provider
    // receipt is checked independently below rather than discarded as history.
    auto common=request;
    for(auto& message:common.messages){
        message.provider_items_json="[]";
        if(message.role==MessageRole::developer)message.role=MessageRole::system;
        if(!message.refusal.empty())throw IncompatibleProviderHistory("DeepSeek does not accept refusal receipts from another wire");
    }
    auto body=Json::parse(serialize_chat_request(config,common));
    body.erase("n");
    if(request.max_output_tokens){body.erase("max_completion_tokens");body["max_tokens"]=*request.max_output_tokens;}
    std::size_t receipts=0;
    const bool thinking=!config.reasoning_effort||*config.reasoning_effort!=ReasoningEffort::none;
    for(std::size_t i=0;i<request.messages.size();++i){
        const auto& message=request.messages[i];
        if(message.provider_items_json=="[]"){
            // Thinking + tools requires prior assistant reasoning, even for an
            // earlier assistant turn which ended without a function call.
            if(thinking&&!request.tools.empty()&&message.role==MessageRole::assistant)incompatible();
            continue;
        }
        if(message.provider_items_json.size()>request_limit-receipts)throw ModelRequestCapacityExceeded("DeepSeek history exceeds limits");
        receipts+=message.provider_items_json.size();
        try{
            auto original=receipt(message,config.model);
            if(thinking&&!request.tools.empty()&&!original.contains("reasoning_content"))incompatible();
            body["messages"][i]=std::move(original);
        }catch(const Json::exception&){incompatible();}
    }
    // Omitted effort keeps the provider's documented enabled/high default.
    // Existing enum compatibility values are accepted by DeepSeek itself.
    try{auto encoded=body.dump();if(encoded.size()>request_limit)throw ModelRequestCapacityExceeded("DeepSeek request exceeds limits");return encoded;}
    catch(const Json::exception&){throw std::invalid_argument("Invalid UTF-8 DeepSeek request");}
}
}
