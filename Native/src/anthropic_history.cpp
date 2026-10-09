#include "agentflow/anthropic_history.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <initializer_list>
#include <set>
#include <string_view>
#include <utility>
#include <vector>
namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t content_limit=8*1024*1024,text_limit=4*1024*1024,input_limit=1024*1024;
[[noreturn]] void invalid(){throw std::invalid_argument("Invalid Claude content receipt");}
Json parse(std::string_view source,std::size_t limit=content_limit,int max_depth=32){
    if(source.size()>limit)invalid();std::vector<std::set<std::string>> keys;
    try{return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>max_depth)invalid();
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::object_end)keys.pop_back();
        else if(event==Json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)invalid();
        return true;
    });}catch(const Json::exception&){invalid();}
}
void utf8(const std::string& value,std::size_t bound){if(value.size()>bound)invalid();try{(void)Json(value).dump();}catch(const Json::exception&){invalid();}}
std::string string(const Json& value,const char* field,std::size_t bound){
    if(!value.contains(field)||!value[field].is_string())invalid();auto result=value[field].get<std::string>();utf8(result,bound);return result;
}
void fields(const Json& value,std::initializer_list<std::string_view> allowed){
    if(!value.is_object())invalid();for(auto it=value.begin();it!=value.end();++it)if(std::find(allowed.begin(),allowed.end(),it.key())==allowed.end())invalid();
}
void opaque(const std::string& value,std::size_t bound){utf8(value,bound);if(value.empty()||value.find('\0')!=std::string::npos)invalid();}
void name(const std::string& value){if(value.empty()||value.size()>64||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)invalid();}
// Locate original value slices only after strict JSON validation. Never dump
// parsed floating-point tool input; escaped field names decode for comparison.
struct Tokens {
    std::string_view source;std::size_t position=0;
    void space(){while(position<source.size()&&std::string_view(" \r\n\t").find(source[position])!=std::string_view::npos)++position;}
    char take(){if(position>=source.size())invalid();return source[position++];}
    void expect(char byte){space();if(take()!=byte)invalid();}
    std::string_view value(){
        space();const auto begin=position;const auto first=take();
        if(first=='"'){for(;;){const auto byte=take();if(byte=='\\')take();else if(byte=='"')break;}}
        else if(first=='{'||first=='['){
            const auto end=first=='{'?'}':']';space();if(position>=source.size())invalid();
            if(source[position]==end)++position;
            else for(;;){value();if(first=='{'){expect(':');value();}space();const auto separator=take();if(separator==end)break;if(separator!=',')invalid();}
        }else while(position<source.size()&&std::string_view(" \r\n\t,]}:").find(source[position])==std::string_view::npos)++position;
        return source.substr(begin,position-begin);
    }
};
std::string_view member(std::string_view source,std::string_view wanted){
    Tokens tokens{source};tokens.expect('{');tokens.space();if(tokens.position>=source.size())invalid();if(source[tokens.position]=='}')return {};
    for(;;){const auto key=Json::parse(tokens.value()).get<std::string>();tokens.expect(':');const auto value=tokens.value();if(key==wanted)return value;tokens.space();const auto separator=tokens.take();if(separator=='}')return {};if(separator!=',')invalid();}
}
std::vector<std::string_view> elements(std::string_view source){
    Tokens tokens{source};tokens.expect('[');tokens.space();if(tokens.position>=source.size())invalid();std::vector<std::string_view> result;if(source[tokens.position]==']')return result;
    for(;;){result.push_back(tokens.value());tokens.space();const auto separator=tokens.take();if(separator==']')return result;if(separator!=',')invalid();}
}
struct Content {std::size_t blocks=0;std::string visible;std::vector<ModelToolCall> calls;};
Content content(const std::string& source){
    const auto parsed=parse(source);if(!parsed.is_array()||parsed.size()>64)invalid();const auto raw=elements(source);Content result;result.blocks=parsed.size();std::set<std::string> ids;
    for(std::size_t index=0;index<parsed.size();++index){
        const auto& block=parsed[index];if(!block.is_object())invalid();const auto type=string(block,"type",32);
        if(type=="text"){
            fields(block,{"type","text"});const auto text=string(block,"text",text_limit);
            if(text.size()>text_limit-result.visible.size())invalid();result.visible+=text;
        }else if(type=="tool_use"){
            fields(block,{"type","id","name","input","caller"});
            if(block.contains("caller")){
                const auto& caller=block.at("caller");fields(caller,{"type"});
                if(string(caller,"type",32)!="direct")invalid();
            }
            ModelToolCall call;call.id=string(block,"id",256);opaque(call.id,256);call.name=string(block,"name",64);name(call.name);
            if(!ids.insert(call.id).second)invalid();const auto input=member(raw[index],"input");
            if(!parse(input,input_limit,16).is_object())invalid();call.arguments_json=std::string(input);result.calls.push_back(std::move(call));
        }else if(type=="thinking"){
            fields(block,{"type","thinking","signature"});(void)string(block,"thinking",text_limit);opaque(string(block,"signature",65536),65536);
        }else if(type=="redacted_thinking"){
            fields(block,{"type","data"});opaque(string(block,"data",text_limit),text_limit);
        }else invalid();
    }
    return result;
}
void finish(const std::string& native,const std::string& provider,std::size_t calls){
    if(provider=="tool_use"){if(native!="tool_calls"||calls==0)invalid();}
    else if(provider=="end_turn"||provider=="stop_sequence"){if(native!="stop"||calls!=0)invalid();}
    else if(provider=="max_tokens"){if(native!="length"||calls!=0)invalid();}
    else invalid();
}
}
std::string anthropic_content_receipt(std::string source,std::string native,std::string provider){
    const auto checked=content(source);finish(native,provider,checked.calls.size());
    auto result=Json::array({{{"type","anthropic_content"},{"content_json",std::move(source)},{"finish_reason",std::move(native)},{"provider_finish_reason",std::move(provider)}}}).dump();
    if(result.size()>content_limit)invalid();return result;
}
std::string anthropic_history_content(const ModelMessage& message){
    try{
        if(message.role!=MessageRole::assistant||!message.refusal.empty()||!message.tool_call_id.empty())invalid();utf8(message.content,text_limit);
        const auto items=parse(message.provider_items_json,content_limit,4);
        if(!items.is_array()||items.size()!=1)invalid();const auto& item=items[0];fields(item,{"type","content_json","finish_reason","provider_finish_reason"});
        if(string(item,"type",32)!="anthropic_content")invalid();const auto source=string(item,"content_json",content_limit);const auto checked=content(source);
        finish(string(item,"finish_reason",32),string(item,"provider_finish_reason",32),checked.calls.size());
        if(checked.blocks==0||checked.visible!=message.content||checked.calls.size()!=message.tool_calls.size())invalid();
        for(std::size_t index=0;index<checked.calls.size();++index){const auto& original=checked.calls[index];const auto& actual=message.tool_calls[index];if(original.id!=actual.id||original.name!=actual.name||original.arguments_json!=actual.arguments_json)invalid();}
        return source;
    }catch(const std::invalid_argument&){throw IncompatibleProviderHistory("Claude provider receipt does not match native assistant history");}
}
}
