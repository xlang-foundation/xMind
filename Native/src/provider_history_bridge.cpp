#include "agentflow/model_provider.hpp"
#include "agentflow/anthropic_history.hpp"
#include "agentflow/gemini_history.hpp"
#include "agentflow/deepseek_provider.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
enum class Source {anthropic,gemini,responses,deepseek};
struct Receipt {Source source;std::string model;bool portable{true};};
[[noreturn]] void incompatible(){throw IncompatibleProviderHistory("Saved provider history cannot be safely converted for this model");}
Json parse(const std::string& source){
    if(source.size()>4*1024*1024)incompatible();std::vector<std::set<std::string>> keys;
    try{return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>64)incompatible();
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::object_end)keys.pop_back();
        else if(event==Json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)incompatible();
        return true;
    });}catch(const Json::exception&){incompatible();}
}
std::optional<Receipt> receipt(const ModelMessage& message){
    if(message.provider_items_json=="[]")return {};
    try{
        const auto items=parse(message.provider_items_json);if(!items.is_array()||items.empty())incompatible();
        const auto type=items[0].value("type",std::string{});
        if(type=="anthropic_content"){
            if(items.size()!=1)incompatible();return Receipt{Source::anthropic,{}};
        }
        if(type=="gemini_content"){
            if(items.size()!=1)incompatible();return Receipt{Source::gemini,{}};
        }
        if(type=="deepseek_assistant"){
            if(items.size()!=1||!items[0].contains("model")||!items[0]["model"].is_string())incompatible();
            const auto model=items[0]["model"].get<std::string>();if(model.empty()||model.size()>256)incompatible();return Receipt{Source::deepseek,model};
        }
        bool responses=true;
        for(const auto& item:items){if(!item.is_object())incompatible();const auto item_type=item.value("type",std::string{});
            if(item_type!="message"&&item_type!="function_call"&&item_type!="reasoning")responses=false;
        }
        // Responses receipts can carry continuation and reasoning semantics that
        // are not represented by canonical chat messages; preserve them only on
        // the Responses wire and fail closed when selecting another protocol.
        if(responses)return Receipt{Source::responses,{},false};
    }catch(const Json::exception&){incompatible();}
    incompatible();
}
bool native_for(const ChatProviderConfig& target,const Receipt& value){
    switch(value.source){
    case Source::anthropic:return target.wire==ProviderWire::anthropic_messages;
    case Source::gemini:return target.wire==ProviderWire::gemini_generate_content;
    case Source::responses:return target.wire==ProviderWire::responses;
    case Source::deepseek:return target.wire==ProviderWire::chat_completions&&target.chat_dialect==ChatDialect::deepseek&&target.model==value.model;
    }
    return false;
}
void validate_source(const ModelRequest& original,const std::vector<std::optional<Receipt>>& receipts,Source source){
    ModelRequest request=original;bool found=false;
    for(std::size_t index=0;index<receipts.size();++index){
        if(receipts[index]&&receipts[index]->source==source)found=true;
        else request.messages[index].provider_items_json="[]";
    }
    if(!found)return;
    try{
        if(source==Source::anthropic){for(std::size_t i=0;i<receipts.size();++i)if(receipts[i]&&receipts[i]->source==source)(void)anthropic_history_content(original.messages[i]);return;}
        if(source==Source::gemini){for(std::size_t i=0;i<receipts.size();++i)if(receipts[i]&&receipts[i]->source==source)validate_gemini_history_message(original.messages[i]);return;}
        if(source==Source::responses){ChatProviderConfig config;config.endpoint="https://history-validation.invalid";config.model="history-validation";config.wire=ProviderWire::responses;config.tools=Capability::supported;config.stream_usage=Capability::supported;config.output_limit=Capability::supported;(void)serialize_responses_request(config,request);return;}
        for(std::size_t i=0;i<receipts.size();++i)if(receipts[i]&&receipts[i]->source==source)validate_deepseek_history_message(original.messages[i],receipts[i]->model);
    }catch(const IncompatibleProviderHistory&){throw;}catch(const std::exception&){incompatible();}
}
}
ModelRequest project_provider_history(const ChatProviderConfig& target,const ModelRequest& request){
    if(request.canonical_window&&target.wire!=ProviderWire::responses)
        throw IncompatibleProviderHistory("Canonical context requires the Responses wire");
    ModelRequest projected=request;std::vector<std::optional<Receipt>> receipts;receipts.reserve(request.messages.size());bool needs_projection=false;
    for(const auto& message:request.messages){auto value=receipt(message);if(value&&!native_for(target,*value)){if(!value->portable)incompatible();needs_projection=true;}receipts.push_back(std::move(value));}
    if(!needs_projection)return projected;
    for(const auto source:{Source::anthropic,Source::gemini,Source::responses,Source::deepseek})validate_source(request,receipts,source);
    for(std::size_t index=0;index<receipts.size();++index)if(receipts[index]&&!native_for(target,*receipts[index]))projected.messages[index].provider_items_json="[]";
    return projected;
}
ModelRequest reproject_authority_history(const ModelRequest& request,const std::vector<std::size_t>& indices){
    if(indices.empty())return request;
    std::vector<std::optional<Receipt>> receipts;receipts.reserve(request.messages.size());
    for(const auto& message:request.messages)receipts.push_back(receipt(message));
    for(const auto source:{Source::anthropic,Source::gemini,Source::responses,Source::deepseek})validate_source(request,receipts,source);
    auto projected=request;std::set<std::size_t> seen;
    for(const auto index:indices){
        if(index>=request.messages.size()||!seen.insert(index).second||
           request.messages[index].role!=MessageRole::assistant||!receipts[index]||
           receipts[index]->source!=Source::responses)incompatible();
        const auto& message=request.messages[index];
        if(message.content.empty()&&message.refusal.empty()&&message.tool_calls.empty())incompatible();
        projected.messages[index].provider_items_json="[]";
    }
    return projected;
}
}
