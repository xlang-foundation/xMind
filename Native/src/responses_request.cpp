#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <set>
namespace agentflow {
std::string serialize_responses_request(const ChatProviderConfig& config,const ModelRequest& request){
    using Json=nlohmann::json;
    // Reuse the shared typed conversation/tool validation. This validated
    // intermediate is never sent to a Chat Completions endpoint.
    auto validation=request;for(auto& message:validation.messages)message.provider_items_json="[]";
    const auto common=Json::parse(serialize_chat_request(config,validation));
    Json body={{"model",config.model},{"stream",true},{"store",false},{"include",Json::array({"reasoning.encrypted_content"})},{"input",Json::array()}};
    if(request.max_output_tokens)body["max_output_tokens"]=*request.max_output_tokens;
    if(common.contains("reasoning_effort"))body["reasoning"]={{"effort",common["reasoning_effort"]}};
    if(common.contains("tools")){body["tools"]=Json::array();for(const auto& tool:common["tools"]){auto value=tool.at("function");value["type"]="function";value["strict"]=false;body["tools"].push_back(std::move(value));}}
    std::set<std::string> item_ids;
    for(std::size_t index=0;index<request.messages.size();++index){const auto& message=request.messages[index];const auto& validated=common.at("messages").at(index);
        if(message.provider_items_json!="[]"){
            if(message.role!=MessageRole::assistant||message.provider_items_json.size()>4*1024*1024)throw std::invalid_argument("Invalid Responses continuation");
            Json items;try{items=Json::parse(message.provider_items_json);}catch(...){throw std::invalid_argument("Invalid Responses continuation");}
            if(!items.is_array()||items.empty()||items.size()>1024)throw std::invalid_argument("Invalid Responses continuation");
            std::string text,refusal;std::vector<ModelToolCall> calls;
            for(const auto& item:items){if(!item.is_object()||!item.contains("id")||!item["id"].is_string()||item["id"].get<std::string>().empty()||!item_ids.insert(item["id"].get<std::string>()).second)throw std::invalid_argument("Invalid Responses item identity");
                const auto type=item.value("type",std::string{});
                if(type=="message"){if(item.value("role",std::string{})!="assistant"||!item.contains("content")||!item["content"].is_array())throw std::invalid_argument("Invalid Responses message");for(const auto& part:item["content"]){const auto kind=part.value("type",std::string{});if(kind=="output_text")text+=part.at("text").get<std::string>();else if(kind=="refusal")refusal+=part.at("refusal").get<std::string>();else throw std::invalid_argument("Unsupported Responses content");}}
                else if(type=="function_call")calls.push_back({item.at("call_id").get<std::string>(),item.at("name").get<std::string>(),item.at("arguments").get<std::string>()});
                else if(type=="reasoning"){if(!item.contains("summary")||!item["summary"].is_array()||(item.contains("content")&&item["content"]!=Json::array()))throw std::invalid_argument("Unsupported Responses reasoning content");}
                else throw std::invalid_argument("Unsupported Responses continuation item");
                body["input"].push_back(item);
            }
            if(text!=message.content||refusal!=message.refusal||calls.size()!=message.tool_calls.size())throw std::invalid_argument("Responses continuation differs from conversation");
            for(std::size_t i=0;i<calls.size();++i)if(calls[i].id!=message.tool_calls[i].id||calls[i].name!=message.tool_calls[i].name||calls[i].arguments_json!=message.tool_calls[i].arguments_json)throw std::invalid_argument("Responses call differs from conversation");
        }else if(message.role==MessageRole::tool)body["input"].push_back({{"type","function_call_output"},{"call_id",message.tool_call_id},{"output",message.content}});
        else {
            if(!message.refusal.empty())throw IncompatibleProviderHistory("Responses refusal history requires original output items");
            if(!message.content.empty()||!message.refusal.empty()||message.tool_calls.empty()){
                Json parts=Json::array({{{"type","input_text"},{"text",message.content}}});
                body["input"].push_back({{"type","message"},{"role",validated["role"]},{"content",std::move(parts)}});
            }
            for(const auto& call:message.tool_calls)body["input"].push_back({{"type","function_call"},{"call_id",call.id},{"name",call.name},{"arguments",call.arguments_json}});
        }
    }
    try{const auto encoded=body.dump();if(encoded.size()>8*1024*1024)throw std::invalid_argument("Responses request exceeds limits");return encoded;}catch(const Json::exception&){throw std::invalid_argument("Invalid Responses request JSON");}
}
}
