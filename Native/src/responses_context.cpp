#include "agentflow/responses_context.hpp"
#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <set>
#include <vector>

namespace agentflow {
namespace {
using Json=nlohmann::json;
using Error=ResponsesContextErrorCode;
constexpr std::size_t argument_limit=1024*1024;
constexpr std::int64_t exact_integer_limit=9007199254740991LL;
[[noreturn]] void fail(Error code){throw ResponsesContextError(code);}
Json parse(std::string_view source,std::size_t limit=responses_context_response_limit){
    if(source.empty()||source.size()>limit)fail(Error::exceeds_limits);
    std::vector<std::set<std::string>> keys;
    try{return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>64)fail(Error::exceeds_limits);
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::object_end)keys.pop_back();
        else if(event==Json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)fail(Error::invalid_json);
        return true;
    });}catch(const Json::exception&){fail(Error::invalid_json);}
}
void whitespace(std::string_view source,std::size_t& cursor){while(cursor<source.size()&&(source[cursor]==' '||source[cursor]=='\t'||source[cursor]=='\r'||source[cursor]=='\n'))++cursor;}
// Callers first validate the complete source with the strict parser above.
// This scanner selects raw spans only; it cannot introduce another JSON value.
std::string span(std::string_view source,std::size_t& cursor){
    whitespace(source,cursor);const auto begin=cursor;std::size_t depth=0;bool quoted=false,escaped=false;
    for(;cursor<source.size();++cursor){const auto c=source[cursor];
        if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"')quoted=true;else if(c=='{'||c=='[')++depth;else if(c=='}'||c==']'){if(!depth)break;--depth;}else if(c==','&&!depth)break;
    }
    auto end=cursor;while(end>begin&&(source[end-1]==' '||source[end-1]=='\t'||source[end-1]=='\r'||source[end-1]=='\n'))--end;
    return std::string(source.substr(begin,end-begin));
}
std::optional<std::string> member(std::string_view object,std::string_view key){
    std::size_t cursor=0;whitespace(object,cursor);if(cursor>=object.size()||object[cursor++]!='{')fail(Error::invalid_json);
    while(true){whitespace(object,cursor);if(cursor>=object.size())fail(Error::invalid_json);if(object[cursor]=='}')return {};
        const auto begin=cursor++;bool escaped=false;
        while(cursor<object.size()){const auto c=object[cursor++];if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')break;}
        const auto field=Json::parse(object.substr(begin,cursor-begin)).get<std::string>();whitespace(object,cursor);
        if(cursor>=object.size()||object[cursor++]!=':')fail(Error::invalid_json);auto value=span(object,cursor);
        if(field==key)return value;whitespace(object,cursor);if(cursor>=object.size())fail(Error::invalid_json);if(object[cursor]=='}')return {};if(object[cursor++]!=',')fail(Error::invalid_json);
    }
}
const std::string& string(const Json& value,const char* field,Error code){const auto found=value.find(field);if(found==value.end()||!found->is_string())fail(code);return found->get_ref<const std::string&>();}
void identity(const std::string& value){if(value.empty()||value.size()>256)fail(Error::invalid_identity);for(const auto c:value)if(static_cast<unsigned char>(c)<0x21||static_cast<unsigned char>(c)>0x7e)fail(Error::invalid_identity);}
void status(const Json& item){if(item.contains("status")&&item["status"]!="completed")fail(Error::unsupported_item);}
void bounded_text(const std::string& value,bool nonempty=false){if((nonempty&&value.empty())||value.size()>responses_context_window_limit||value.find('\0')!=std::string::npos)fail(Error::unsupported_item);}
void opaque(const std::string& value){if(value.empty()||value.size()>responses_context_window_limit)fail(Error::unsupported_item);}
std::int64_t number(const Json& value,Error code){
    if(!value.is_number_integer()||value<0||value>exact_integer_limit)fail(code);return value.get<std::int64_t>();
}
void usage_details(const Json& group,const std::set<std::string>& allowed){
    if(!group.is_object())fail(Error::invalid_usage);for(auto it=group.begin();it!=group.end();++it){if(!allowed.contains(it.key()))fail(Error::invalid_usage);number(*it,Error::invalid_usage);}
}
void usage(const Json& value){
    if(!value.is_object())fail(Error::invalid_usage);
    for(auto it=value.begin();it!=value.end();++it){if(it.key()=="input_tokens_details")usage_details(*it,{"cached_tokens","cache_write_tokens"});
        else if(it.key()=="output_tokens_details")usage_details(*it,{"reasoning_tokens"});
        else if(it.key()=="input_tokens"||it.key()=="output_tokens"||it.key()=="total_tokens")number(*it,Error::invalid_usage);else fail(Error::invalid_usage);
    }
    if(value.contains("input_tokens")&&value.contains("output_tokens")&&value.contains("total_tokens")){
        const auto input=number(value["input_tokens"],Error::invalid_usage),output=number(value["output_tokens"],Error::invalid_usage),total=number(value["total_tokens"],Error::invalid_usage);
        if(input>exact_integer_limit-output||input+output!=total)fail(Error::invalid_usage);
    }
}
void validate_window(const Json& values){
    if(!values.is_array()||values.empty()||values.size()>4096)fail(Error::unsupported_item);
    std::set<std::string> ids,calls,pending;std::size_t compactions=0;
    for(const auto& item:values){if(!item.is_object())fail(Error::unsupported_item);const auto& type=string(item,"type",Error::unsupported_item);
        if(item.contains("id")){const auto& id=string(item,"id",Error::invalid_identity);identity(id);if(!ids.insert(id).second)fail(Error::invalid_identity);}
        else if(type!="function_call_output")fail(Error::invalid_identity);
        status(item);
        if(type=="message"){
            if(!pending.empty())fail(Error::invalid_correlation);const auto& role=string(item,"role",Error::unsupported_item);if(role!="user"&&role!="assistant")fail(Error::unsupported_item);
            if(!item.contains("content")||!item["content"].is_array()||item["content"].size()>1024)fail(Error::unsupported_item);
            if(item.contains("phase")&&!item["phase"].is_null()&&
               (!item["phase"].is_string()||(item["phase"]!="commentary"&&item["phase"]!="final_answer")))fail(Error::unsupported_item);
            for(const auto& part:item["content"]){if(!part.is_object())fail(Error::unsupported_item);const auto& kind=string(part,"type",Error::unsupported_item);
                if(kind=="input_text"&&role=="user"){
                    bounded_text(string(part,"text",Error::unsupported_item));
                    // User content is authority-bearing input. Limit its
                    // representation to supported string/enum fields so a
                    // numeric parse/dump comparison cannot accept changed
                    // future content metadata as unchanged user input.
                    for(auto field=part.begin();field!=part.end();++field)if(field.key()!="type"&&field.key()!="text"&&field.key()!="prompt_cache_breakpoint")fail(Error::unsupported_item);
                    if(part.contains("prompt_cache_breakpoint")){const auto& point=part["prompt_cache_breakpoint"];if(!point.is_object()||point.size()!=1||point.value("mode",Json{})!="explicit")fail(Error::unsupported_item);}
                }
                else if(kind=="output_text"&&role=="assistant")bounded_text(string(part,"text",Error::unsupported_item));
                else if(kind=="refusal"&&role=="assistant")bounded_text(string(part,"refusal",Error::unsupported_item));else fail(Error::unsupported_item);
            }
        }else if(type=="compaction"){
            if(!pending.empty())fail(Error::invalid_correlation);++compactions;opaque(string(item,"encrypted_content",Error::unsupported_item));
        }else if(type=="reasoning"){
            opaque(string(item,"encrypted_content",Error::unsupported_item));
            if(!item.contains("summary")||!item["summary"].is_array()||item["summary"].size()>1024||(item.contains("content")&&item["content"]!=Json::array()))fail(Error::unsupported_item);
            for(const auto& part:item["summary"]){if(!part.is_object()||string(part,"type",Error::unsupported_item)!="summary_text")fail(Error::unsupported_item);bounded_text(string(part,"text",Error::unsupported_item));}
        }else if(type=="function_call"){
            const auto& call=string(item,"call_id",Error::invalid_identity);identity(call);identity(string(item,"name",Error::invalid_identity));
            if(!calls.insert(call).second||!pending.insert(call).second)fail(Error::invalid_correlation);const auto& arguments=string(item,"arguments",Error::unsupported_item);
            if(arguments.size()>argument_limit)fail(Error::exceeds_limits);if(!parse(arguments,argument_limit).is_object())fail(Error::invalid_json);
        }else if(type=="function_call_output"){
            const auto& call=string(item,"call_id",Error::invalid_identity);identity(call);if(!pending.erase(call))fail(Error::invalid_correlation);bounded_text(string(item,"output",Error::unsupported_item));
        }else fail(Error::unsupported_item);
    }
    if(!pending.empty()||!compactions)fail(Error::invalid_correlation);
}
std::string serialize(const ChatProviderConfig& config,const ModelRequest& request,bool compact){
    if(config.wire!=ProviderWire::responses)fail(Error::invalid_request);
    std::string source;try{source=serialize_responses_request(config,request);}
    catch(const ModelRequestCapacityExceeded&){throw;}
    catch(const std::invalid_argument&){fail(Error::invalid_request);}
    const auto parsed=parse(source);if(!parsed.is_object())fail(Error::invalid_request);
    // The ordinary Responses serializer owns typed conversation/receipt and
    // tail correlation checks. Only allowed endpoint parameters are selected;
    // input is spliced raw so canonical state is not reparsed and rounded.
    auto input=member(source,"input");if(!input)fail(Error::invalid_request);
    std::string result="{\"model\":"+parsed.at("model").dump()+",\"input\":"+*input;
    if(const auto instructions=member(source,"instructions"))result+=",\"instructions\":"+*instructions;
    if(!compact)for(const auto* key:{"tools","reasoning"})if(const auto value=member(source,key))result+=",\""+std::string(key)+"\":"+*value;
    result+='}';parse(result);return result;
}
}
ResponsesContextError::ResponsesContextError(ResponsesContextErrorCode code):ModelProtocolError("Responses context protocol rejected"),code_(code) {}
ResponsesCanonicalWindow parse_responses_canonical_window(std::string_view array){auto parsed=parse(array,responses_context_window_limit);validate_window(parsed);return ResponsesCanonicalWindow(std::string(array));}
ResponsesCompactionResult parse_responses_compaction_response(std::string_view response){
    const auto value=parse(response);if(!value.is_object()||value.value("object",Json{})!="response.compaction")fail(Error::invalid_compaction_response);
    for(auto it=value.begin();it!=value.end();++it)if(it.key()!="id"&&it.key()!="object"&&it.key()!="created_at"&&it.key()!="output"&&it.key()!="usage")fail(Error::invalid_compaction_response);
    const auto id=string(value,"id",Error::invalid_identity);identity(id);if(!value.contains("created_at"))fail(Error::invalid_compaction_response);const auto created=number(value["created_at"],Error::invalid_compaction_response);
    const auto output=member(response,"output");if(!output)fail(Error::invalid_compaction_response);auto window=parse_responses_canonical_window(*output);
    std::string actual_usage="null";if(value.contains("usage")&&!value["usage"].is_null()){usage(value["usage"]);actual_usage=*member(response,"usage");}
    return {std::move(window),id,std::move(actual_usage),created,std::string(response)};
}
ResponsesTokenCount parse_responses_input_tokens_response(std::string_view response){
    const auto value=parse(response,65536);if(!value.is_object()||value.size()!=2||value.value("object",Json{})!="response.input_tokens"||!value.contains("input_tokens"))fail(Error::invalid_token_count);
    return {number(value["input_tokens"],Error::invalid_token_count),std::string(response)};
}
std::string serialize_responses_compaction_request(const ChatProviderConfig& config,const ModelRequest& request){return serialize(config,request,true);}
std::string serialize_responses_input_tokens_request(const ChatProviderConfig& config,const ModelRequest& request){return serialize(config,request,false);}
void validate_responses_compaction_result(std::string_view exact_serialized_request,const ResponsesCompactionResult& result){
    const auto actual=parse_responses_compaction_response(result.response_json);
    if(actual.response_id!=result.response_id||actual.created_at!=result.created_at||actual.usage_json!=result.usage_json||actual.window.items_json()!=result.window.items_json())fail(Error::invalid_compaction_response);
    const auto source=parse(exact_serialized_request);
    if(!source.is_object()||!source.contains("model")||!source["model"].is_string()||source["model"].get_ref<const std::string&>().empty()||source["model"].get_ref<const std::string&>().size()>512||!source.contains("input")||!source["input"].is_array())fail(Error::invalid_request);
    for(auto field=source.begin();field!=source.end();++field)if(field.key()!="model"&&field.key()!="input"&&field.key()!="instructions")fail(Error::invalid_request);
    if(source.contains("instructions")&&!source["instructions"].is_string())fail(Error::invalid_request);
    const auto retained=parse(result.window.items_json(),responses_context_window_limit);
    std::vector<const Json*> original_users,retained_users;
    for(const auto& item:source.at("input")){if(!item.is_object())fail(Error::invalid_request);if(item.value("type",Json{})=="message"&&item.value("role",Json{})=="user")original_users.push_back(&item);}
    for(const auto& item:retained)if(item.value("type",Json{})=="message"&&item.value("role",Json{})=="user")retained_users.push_back(&item);
    if(original_users.size()!=retained_users.size())fail(Error::invalid_correlation);
    for(std::size_t index=0;index<original_users.size();++index){const auto& before=*original_users[index];const auto& after=*retained_users[index];
        // Native input uses the documented text-content array representation.
        // Newly assigned provider IDs/status may be present; existing IDs and
        // complete ordered content must be preserved. No role/text rewriting,
        // summary or removal is treated as representational normalization.
        if(!before.contains("content")||before["content"]!=after["content"]||(before.contains("id")&&before["id"]!=after["id"]))fail(Error::invalid_correlation);
    }
}
void validate_responses_compaction_result(const ChatProviderConfig& config,const ModelRequest& request,const ResponsesCompactionResult& result){validate_responses_compaction_result(serialize_responses_compaction_request(config,request),result);}
}
