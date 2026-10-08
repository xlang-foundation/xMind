#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <set>
#include <string_view>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t request_limit=8*1024*1024;
std::string array_inner(std::string_view source){
    const auto first=source.find_first_not_of(" \r\n\t"),last=source.find_last_not_of(" \r\n\t");
    if(first==std::string_view::npos||source[first]!='['||source[last]!=']')
        throw std::invalid_argument("Invalid Responses item array");
    return std::string(source.substr(first+1,last-first-1));
}
}
std::string serialize_responses_request(const ChatProviderConfig& config,const ModelRequest& request){
    if(request.canonical_window&&config.wire!=ProviderWire::responses)
        throw IncompatibleProviderHistory("Canonical context requires the Responses wire");
    // The original tail retains the ordinary conversation/tool guards. A
    // canonical window is a separate validated type, never an assistant receipt.
    auto validation=request;validation.canonical_window.reset();
    for(auto& message:validation.messages)message.provider_items_json="[]";
    const auto common=Json::parse(serialize_responses_request_fields(config,validation));
    Json body={{"model",config.model},{"stream",true},{"store",false},
        {"include",Json::array({"reasoning.encrypted_content"})}};
    if(request.max_output_tokens)body["max_output_tokens"]=*request.max_output_tokens;
    if(common.contains("reasoning_effort"))body["reasoning"]={{"effort",common["reasoning_effort"]}};
    if(common.contains("tools")){
        body["tools"]=Json::array();
        for(const auto& tool:common["tools"]){
            auto value=tool.at("function");value["type"]="function";value["strict"]=false;
            body["tools"].push_back(std::move(value));
        }
    }
    std::set<std::string> item_ids,call_ids;std::size_t trusted_count=0;
    if(request.canonical_window){
        while(trusted_count<request.messages.size()&&
            (request.messages[trusted_count].role==MessageRole::system||
             request.messages[trusted_count].role==MessageRole::developer))++trusted_count;
        for(std::size_t i=trusted_count;i<request.messages.size();++i)
            if(request.messages[i].role==MessageRole::system||request.messages[i].role==MessageRole::developer)
                throw IncompatibleProviderHistory("Trusted Responses instructions must precede canonical context");
        // Only collect identities here. Splice exact validated bytes below.
        const auto items=Json::parse(request.canonical_window->items_json());
        for(const auto& item:items){
            if(item.contains("id"))item_ids.insert(item.at("id").get<std::string>());
            if(item.at("type")=="function_call")call_ids.insert(item.at("call_id").get<std::string>());
        }
    }
    std::string input;
    auto add=[&](std::string part){
        if(part.empty())return;
        const auto separator=input.empty()?0u:1u;
        if(part.size()>request_limit||separator>request_limit-input.size()||
           part.size()>request_limit-input.size()-separator)
            throw ModelRequestCapacityExceeded("Responses request exceeds limits");
        if(separator)input+=',';
        input+=part;
    };
    auto add_canonical=[&]{if(request.canonical_window)add(array_inner(request.canonical_window->items_json()));};
    for(std::size_t index=0;index<request.messages.size();++index){
        if(index==trusted_count)add_canonical();
        const auto& message=request.messages[index];const auto& validated=common.at("messages").at(index);
        if(message.provider_items_json!="[]"){
            if(message.role!=MessageRole::assistant||message.provider_items_json.size()>4*1024*1024)
                throw std::invalid_argument("Invalid Responses continuation");
            Json items;std::vector<std::set<std::string>> fields;
            try{
                items=Json::parse(message.provider_items_json,[&](int depth,Json::parse_event_t event,Json& value){
                    if(depth>64)throw std::invalid_argument("Responses continuation nesting exceeds limits");
                    if(event==Json::parse_event_t::object_start)fields.emplace_back();
                    else if(event==Json::parse_event_t::object_end)fields.pop_back();
                    else if(event==Json::parse_event_t::key&&!fields.back().insert(value.get<std::string>()).second)
                        throw std::invalid_argument("Duplicate Responses continuation field");
                    return true;
                });
            }catch(const Json::exception&){throw std::invalid_argument("Invalid Responses continuation");}
            if(!items.is_array()||items.empty()||items.size()>1024)throw std::invalid_argument("Invalid Responses continuation");
            std::string text,refusal;std::vector<ModelToolCall> calls;
            for(const auto& item:items){
                if(!item.is_object()||!item.contains("id")||!item["id"].is_string()||
                   item["id"].get<std::string>().empty()||!item_ids.insert(item["id"].get<std::string>()).second)
                    throw std::invalid_argument("Invalid Responses item identity");
                const auto type=item.value("type",std::string{});
                if(type=="message"){
                    if(item.value("role",std::string{})!="assistant"||!item.contains("content")||!item["content"].is_array())
                        throw std::invalid_argument("Invalid Responses message");
                    for(const auto& part:item["content"]){
                        const auto kind=part.value("type",std::string{});
                        if(kind=="output_text")text+=part.at("text").get<std::string>();
                        else if(kind=="refusal")refusal+=part.at("refusal").get<std::string>();
                        else throw std::invalid_argument("Unsupported Responses content");
                    }
                }else if(type=="function_call"){
                    const auto call_id=item.at("call_id").get<std::string>();
                    if(!call_ids.insert(call_id).second)throw std::invalid_argument("Duplicate Responses call identity");
                    calls.push_back({call_id,item.at("name").get<std::string>(),item.at("arguments").get<std::string>()});
                }else if(type=="reasoning"){
                    if(!item.contains("summary")||!item["summary"].is_array()||
                       (item.contains("content")&&item["content"]!=Json::array()))
                        throw std::invalid_argument("Unsupported Responses reasoning content");
                }else throw std::invalid_argument("Unsupported Responses continuation item");
            }
            if(text!=message.content||refusal!=message.refusal||calls.size()!=message.tool_calls.size())
                throw std::invalid_argument("Responses continuation differs from conversation");
            for(std::size_t i=0;i<calls.size();++i)
                if(calls[i].id!=message.tool_calls[i].id||calls[i].name!=message.tool_calls[i].name||
                   calls[i].arguments_json!=message.tool_calls[i].arguments_json)
                    throw std::invalid_argument("Responses call differs from conversation");
            // Ordinary validated receipts keep exact provider material too.
            add(array_inner(message.provider_items_json));
        }else if(message.role==MessageRole::tool){
            add(Json{{"type","function_call_output"},{"call_id",message.tool_call_id},{"output",message.content}}.dump());
        }else{
            if(!message.refusal.empty())throw IncompatibleProviderHistory("Responses refusal history requires original output items");
            if(!message.content.empty()||message.tool_calls.empty()){
                auto parts=Json::array({{{"type",message.role==MessageRole::assistant?"output_text":"input_text"},{"text",message.content}}});
                add(Json{{"type","message"},{"role",validated["role"]},{"content",std::move(parts)}}.dump());
            }
            for(const auto& call:message.tool_calls){
                if(!call_ids.insert(call.id).second)throw std::invalid_argument("Duplicate Responses call identity");
                add(Json{{"type","function_call"},{"call_id",call.id},{"name",call.name},{"arguments",call.arguments_json}}.dump());
            }
        }
    }
    if(trusted_count==request.messages.size())add_canonical();
    try{
        auto encoded=body.dump();encoded.pop_back();encoded+=",\"input\":[";encoded+=input;encoded+="]}";
        if(encoded.size()>request_limit)throw ModelRequestCapacityExceeded("Responses request exceeds limits");
        return encoded;
    }catch(const Json::exception&){throw std::invalid_argument("Invalid Responses request JSON");}
}
}
