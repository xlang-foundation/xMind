#include "agentflow/gemini_history.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <set>
#include <utility>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t source_limit=8*1024*1024,object_limit=1024*1024;
constexpr std::uint64_t safe_integer=9007199254740991;
[[noreturn]] void invalid(){throw std::invalid_argument("Invalid Gemini history");}
void account(std::size_t& bytes,std::size_t added){if(added>source_limit-bytes)invalid();bytes+=added;}
Json parse(std::string_view source,std::size_t limit=source_limit,int depth_limit=32){
    if(source.size()>limit)invalid();std::vector<std::set<std::string>> keys;
    try{return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>depth_limit)invalid();
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::object_end)keys.pop_back();
        else if(event==Json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)invalid();
        return true;
    });}catch(const Json::exception&){invalid();}
}
void utf8(const std::string& value,std::size_t limit=4*1024*1024){
    if(value.size()>limit)invalid();try{(void)Json(value).dump();}catch(const Json::exception&){invalid();}
}
std::string string(const Json& value,const char* field,std::size_t limit=4*1024*1024){
    if(!value.contains(field)||!value[field].is_string())invalid();auto result=value[field].get<std::string>();utf8(result,limit);return result;
}
void fields(const Json& object,std::initializer_list<std::string_view> allowed){
    if(!object.is_object())invalid();for(auto it=object.begin();it!=object.end();++it)if(std::find(allowed.begin(),allowed.end(),it.key())==allowed.end())invalid();
}
void provider_id(const std::string& value,std::size_t limit=256){utf8(value,limit);if(value.empty()||value.find('\0')!=std::string::npos)invalid();}
void internal_id(const std::string& value){
    if(value.empty()||value.size()>256||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)invalid();
}
void function_name(const std::string& value){if(value.empty()||value.size()>128||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)invalid();}

// This lexer runs only after strict JSON validation. It locates original value
// slices rather than dumping a parsed floating-point value, including when a
// property name uses Unicode escapes. Every boundary remains checked.
struct Tokens {
    std::string_view source;std::size_t position=0;
    void space(){while(position<source.size()&&std::string_view(" \r\n\t").find(source[position])!=std::string_view::npos)++position;}
    char take(){if(position>=source.size())invalid();return source[position++];}
    void expect(char byte){space();if(take()!=byte)invalid();}
    std::string_view value(){
        space();const auto start=position;const auto first=take();
        if(first=='"'){
            for(;;){const auto byte=take();if(byte=='\\')take();else if(byte=='"')break;}
        }else if(first=='{'||first=='['){
            const auto end=first=='{'?'}':']';space();if(position>=source.size())invalid();
            if(source[position]==end)++position;
            else for(;;){value();if(first=='{'){expect(':');value();}space();const auto separator=take();if(separator==end)break;if(separator!=',')invalid();}
        }else{
            while(position<source.size()&&std::string_view(" \r\n\t,]}:").find(source[position])==std::string_view::npos)++position;
        }
        return source.substr(start,position-start);
    }
};
std::string_view member(std::string_view source,std::string_view wanted){
    Tokens tokens{source};tokens.expect('{');tokens.space();if(tokens.position>=source.size())invalid();if(source[tokens.position]=='}')return {};
    for(;;){const auto name=Json::parse(tokens.value()).get<std::string>();tokens.expect(':');const auto value=tokens.value();if(name==wanted)return value;tokens.space();const auto separator=tokens.take();if(separator=='}')return {};if(separator!=',')invalid();}
}
std::vector<std::string_view> elements(std::string_view source){
    Tokens tokens{source};tokens.expect('[');tokens.space();if(tokens.position>=source.size())invalid();std::vector<std::string_view> result;if(source[tokens.position]==']')return result;
    for(;;){result.push_back(tokens.value());tokens.space();const auto separator=tokens.take();if(separator==']')return result;if(separator!=',')invalid();}
}
void object(std::string_view source){if(!parse(source,object_limit,16).is_object())invalid();}
struct Call {std::size_t part_index;GeminiFunctionCall wire;};
struct Content {std::vector<GeminiPart> parts;std::vector<Call> calls;std::string visible;};
Content content(const std::string& source){
    const auto parsed=parse(source);if(!parsed.is_array()||parsed.size()>4096)invalid();
    const auto slices=elements(source);Content result;std::set<std::string> provider_ids;
    for(std::size_t index=0;index<parsed.size();++index){
        const auto& value=parsed[index];fields(value,{"text","functionCall","thought","thoughtSignature","partMetadata"});if(value.empty())invalid();
        GeminiPart part;if(value.contains("thought")){if(!value["thought"].is_boolean())invalid();part.thought=value["thought"].get<bool>();}
        if(value.contains("thoughtSignature")){part.thought_signature=string(value,"thoughtSignature",65536);provider_id(*part.thought_signature,65536);}
        if(value.contains("partMetadata")){const auto raw=member(slices[index],"partMetadata");object(raw);part.part_metadata_json=std::string(raw);}
        if(value.contains("text")&&value.contains("functionCall"))invalid();
        if(value.contains("text")){
            part.text=string(value,"text");if(!part.thought.value_or(false)){if(part.text.size()>4*1024*1024-result.visible.size())invalid();result.visible+=part.text;}
        }else if(value.contains("functionCall")){
            const auto& call=value["functionCall"];fields(call,{"name","id","args"});if(result.calls.size()>=64)invalid();
            part.kind=GeminiPartKind::function_call;part.name=string(call,"name",128);function_name(part.name);GeminiFunctionCall wire;wire.name=part.name;
            if(call.contains("id")){part.call_id=string(call,"id",256);provider_id(*part.call_id);if(!provider_ids.insert(*part.call_id).second)invalid();wire.id=part.call_id;}
            if(call.contains("args")){const auto raw=member(member(slices[index],"functionCall"),"args");object(raw);part.object_json=std::string(raw);wire.arguments_json=part.object_json;}
            else part.arguments_omitted=true;
            result.calls.push_back({index,std::move(wire)});
        }else{
            if(!part.thought_signature)invalid();part.kind=GeminiPartKind::signature;
        }
        result.parts.push_back(std::move(part));
    }
    return result;
}
void finish(const std::string& native,const std::string& provider,std::size_t calls){
    if(provider=="STOP"){if(native!=(calls?"tool_calls":"stop"))invalid();}
    else if(provider=="MAX_TOKENS"){if(native!="length")invalid();}
    else invalid();
}
std::string usage(const std::string& source){
    const auto value=parse(source,65536,4);if(value.is_null())return "null";
    fields(value,{"promptTokenCount","candidatesTokenCount","totalTokenCount","thoughtsTokenCount","cachedContentTokenCount","toolUsePromptTokenCount"});
    for(const auto& count:value){
        if(!count.is_number_integer())invalid();
        if(count.is_number_unsigned()){if(count.get<std::uint64_t>()>safe_integer)invalid();}
        else{const auto number=count.get<std::int64_t>();if(number<0||static_cast<std::uint64_t>(number)>safe_integer)invalid();}
    }
    if(value.empty())return "null";Json mapped=value;
    if(value.contains("promptTokenCount"))mapped["prompt_tokens"]=mapped["input_tokens"]=value["promptTokenCount"];
    if(value.contains("candidatesTokenCount"))mapped["completion_tokens"]=mapped["output_tokens"]=value["candidatesTokenCount"];
    if(value.contains("totalTokenCount"))mapped["total_tokens"]=value["totalTokenCount"];
    if(value.contains("cachedContentTokenCount")){mapped["prompt_tokens_details"]["cached_tokens"]=value["cachedContentTokenCount"];mapped["input_tokens_details"]["cached_tokens"]=value["cachedContentTokenCount"];}
    if(value.contains("thoughtsTokenCount")){mapped["completion_tokens_details"]["reasoning_tokens"]=value["thoughtsTokenCount"];mapped["output_tokens_details"]["reasoning_tokens"]=value["thoughtsTokenCount"];}
    return mapped.dump();
}
struct BoundCall {std::string internal_id;Call original;};
struct Replay {Content model;std::vector<BoundCall> calls;};
Replay receipt(const ModelMessage& message,const Json& items){
    try{
        if(!items.is_array()||items.size()!=1)invalid();const auto& value=items[0];fields(value,{"type","parts_json","finish_reason","provider_finish_reason","bindings","model_version","response_id"});
        if(string(value,"type",32)!="gemini_content")invalid();Replay result;result.model=content(string(value,"parts_json",source_limit));
        const auto reason=string(value,"finish_reason",32),provider=string(value,"provider_finish_reason",32);finish(reason,provider,result.model.calls.size());
        if(provider=="MAX_TOKENS"&&!result.model.calls.empty())invalid();
        for(const auto* field:{"model_version","response_id"})if(value.contains(field))provider_id(string(value,field,256));
        if(!value.contains("bindings")||!value["bindings"].is_array()||value["bindings"].size()!=result.model.calls.size()||message.tool_calls.size()!=result.model.calls.size()||message.content!=result.model.visible)invalid();
        std::set<std::string> ids;
        for(std::size_t index=0;index<result.model.calls.size();++index){
            const auto& binding=value["bindings"][index];fields(binding,{"tool_call_id","part_index"});const auto id=string(binding,"tool_call_id",256);internal_id(id);if(!ids.insert(id).second)invalid();
            const auto& original=result.model.calls[index];if(!binding.contains("part_index")||!binding["part_index"].is_number_integer()||binding["part_index"]!=original.part_index)invalid();
            const auto& call=message.tool_calls[index];if(call.id!=id||call.name!=original.wire.name||call.arguments_json!=original.wire.arguments_json.value_or("{}"))invalid();
            result.calls.push_back({id,original});
        }
        return result;
    }catch(const std::invalid_argument&){throw IncompatibleProviderHistory("Gemini receipt differs from conversation");}catch(const Json::exception&){throw IncompatibleProviderHistory("Invalid Gemini receipt");}
}
Json provider_items(const ModelMessage& message){
    try{const auto result=parse(message.provider_items_json);if(!result.is_array())invalid();return result;}
    catch(const std::invalid_argument&){throw IncompatibleProviderHistory("Invalid Gemini provider history");}
}
}

std::string gemini_model_usage(const std::string& usage_json){
    try{return usage(usage_json);}catch(const std::invalid_argument&){throw ModelProtocolError("Invalid Gemini usage");}catch(const Json::exception&){throw ModelProtocolError("Invalid Gemini usage");}
}

ModelCompletion gemini_model_completion(const GeminiCompletion& completion,const std::function<std::string()>& internal_id_factory){
    try{
        auto model=content(completion.parts_json);utf8(completion.content);if(model.visible!=completion.content)invalid();finish(completion.finish_reason,completion.provider_finish_reason,model.calls.size());
        const bool executable=completion.provider_finish_reason=="STOP";
        if(completion.function_calls.size()!=(executable?model.calls.size():0))invalid();
        if(executable)for(std::size_t index=0;index<model.calls.size();++index){
            const auto& original=model.calls[index].wire;const auto& decoded=completion.function_calls[index];if(decoded.name!=original.name||decoded.id!=original.id||decoded.arguments_json!=original.arguments_json)invalid();
        }
        ModelCompletion result;result.content=completion.content;result.finish_reason=completion.finish_reason;result.usage_json=gemini_model_usage(completion.usage_json);
        for(const auto* value:{&completion.model_version,&completion.response_id})if(!value->empty())provider_id(*value);
        Json bindings=Json::array();std::set<std::string> ids;
        if(executable)for(const auto& original:model.calls){
            if(!internal_id_factory)invalid();std::string id;try{id=internal_id_factory();}catch(...){throw ModelProtocolError("Gemini native call identity allocation failed");}
            internal_id(id);if(!ids.insert(id).second)invalid();result.tool_calls.push_back({id,original.wire.name,original.wire.arguments_json.value_or("{}")});bindings.push_back({{"tool_call_id",id},{"part_index",original.part_index}});
        }
        Json saved={{"type","gemini_content"},{"parts_json",completion.parts_json},{"finish_reason",completion.finish_reason},{"provider_finish_reason",completion.provider_finish_reason},{"bindings",std::move(bindings)}};
        for(const auto& field:{std::pair{"model_version",&completion.model_version},std::pair{"response_id",&completion.response_id}})if(!field.second->empty())saved[field.first]=*field.second;
        result.provider_items_json=Json::array({saved}).dump();if(result.provider_items_json.size()>source_limit)invalid();return result;
    }catch(const std::invalid_argument&){throw ModelProtocolError("Invalid Gemini completion history");}catch(const Json::exception&){throw ModelProtocolError("Invalid Gemini completion history");}
}

GeminiRequest gemini_model_request(const ModelRequest& request,Capability function_calls){
    if(request.canonical_window)throw IncompatibleProviderHistory("Canonical context requires the Responses wire");
    if(request.messages.empty()||request.messages.size()>4096||request.tools.size()>64)invalid();GeminiRequest result;result.tools=request.tools;result.function_calls=function_calls;result.max_output_tokens=request.max_output_tokens;
    bool leading_system=true;std::size_t bytes=0;std::set<std::string> native_ids;
    for(std::size_t index=0;index<request.messages.size();++index){
        const auto& message=request.messages[index];utf8(message.content);account(bytes,message.content.size());account(bytes,message.provider_items_json.size());
        if(message.tool_calls.size()>64||!message.refusal.empty())invalid();const auto items=provider_items(message);
        if(message.role!=MessageRole::tool&&!message.tool_call_id.empty())invalid();
        if(message.role!=MessageRole::assistant&&!message.tool_calls.empty())invalid();
        if(message.role!=MessageRole::assistant&&!items.empty())throw IncompatibleProviderHistory("Gemini history requires assistant receipts");
        if(message.role==MessageRole::system){if(!leading_system)invalid();result.system_instructions.push_back(message.content);continue;}
        leading_system=false;
        if(message.role==MessageRole::user){result.contents.push_back({GeminiRole::user,{GeminiPart{GeminiPartKind::text,message.content}}});continue;}
        if(message.role!=MessageRole::assistant)invalid();
        if(items.empty()){
            if(!message.tool_calls.empty())throw IncompatibleProviderHistory("Gemini tool history requires an original receipt");
            result.contents.push_back({GeminiRole::model,{GeminiPart{GeminiPartKind::text,message.content}}});continue;
        }
        auto replay=receipt(message,items);for(const auto& call:message.tool_calls){account(bytes,call.id.size());account(bytes,call.name.size());account(bytes,call.arguments_json.size());}
        // A provider may naturally stop without producing a part. Its receipt
        // remains durable, but there is no content to replay and no fabricated
        // empty text is needed. Signed/metadata-bearing parts are never omitted.
        if(!replay.model.parts.empty())result.contents.push_back({GeminiRole::model,std::move(replay.model.parts)});if(replay.calls.empty())continue;
        if(function_calls!=Capability::supported)invalid();std::map<std::string,std::size_t> pending;for(std::size_t call=0;call<replay.calls.size();++call){if(!native_ids.insert(replay.calls[call].internal_id).second)throw IncompatibleProviderHistory("Duplicate Gemini native call identity");pending.emplace(replay.calls[call].internal_id,call);}
        std::vector<std::optional<GeminiPart>> responses(replay.calls.size());
        for(std::size_t answered=0;answered<replay.calls.size();++answered){
            if(++index>=request.messages.size())invalid();const auto& answer=request.messages[index];if(answer.role!=MessageRole::tool||!answer.tool_calls.empty()||!answer.refusal.empty())invalid();
            utf8(answer.content);account(bytes,answer.content.size());account(bytes,answer.provider_items_json.size());account(bytes,answer.tool_call_id.size());if(!provider_items(answer).empty())throw IncompatibleProviderHistory("Gemini tool outputs cannot carry provider history");
            internal_id(answer.tool_call_id);const auto found=pending.find(answer.tool_call_id);if(found==pending.end()||responses[found->second])invalid();const auto& original=replay.calls[found->second].original.wire;
            GeminiPart part;part.kind=GeminiPartKind::function_response;part.name=original.name;part.call_id=original.id;part.object_json=Json{{"output",answer.content}}.dump();responses[found->second]=std::move(part);
        }
        GeminiContent turn;turn.role=GeminiRole::user;for(auto& response:responses){if(!response)invalid();turn.parts.push_back(std::move(*response));}result.contents.push_back(std::move(turn));
    }
    // Reuse the native wire serializer's declaration, result-correlation, output
    // limit and total-body gates; this bridge never chooses endpoint or auth.
    (void)serialize_gemini_request(result);return result;
}
}
