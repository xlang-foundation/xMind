#include "agentflow/gemini_stream.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t limit=8*1024*1024;
void append(std::string& to,std::string_view from,std::size_t bound=limit){if(from.size()>bound-to.size())throw ModelProtocolError("Gemini stream exceeds limits");to.append(from);}
Json parse(const std::string& source,int maximum_depth=32){
    std::vector<std::set<std::string>> keys;
    return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>maximum_depth)throw ModelProtocolError("Gemini JSON nesting exceeds limits");
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::object_end)keys.pop_back();
        else if(event==Json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)throw ModelProtocolError("Duplicate Gemini JSON field");
        return true;
    });
}
// Walk already validated JSON, retaining token slices rather than dumping
// parsed floating-point values. Escaped member names are decoded for lookup.
struct Tokens {
    std::string_view source;std::size_t pos=0;
    void space(){while(pos<source.size()&&std::string_view(" \r\n\t").find(source[pos])!=std::string_view::npos)++pos;}
    std::string_view value(){
        space();const auto start=pos;if(pos==source.size())throw ModelProtocolError("Invalid Gemini token boundary");
        const auto first=source[pos++];
        if(first=='"'){while(pos<source.size()){const auto byte=source[pos++];if(byte=='\\')++pos;else if(byte=='"')break;}}
        else if(first=='{'||first=='['){space();const char close=first=='{'?'}':']';while(pos<source.size()&&source[pos]!=close){value();space();if(pos<source.size()&&(source[pos]==':'||source[pos]==','))++pos;space();}++pos;}
        else while(pos<source.size()&&std::string_view(" ,}:]\r\n\t").find(source[pos])==std::string_view::npos)++pos;
        if(pos>source.size())throw ModelProtocolError("Invalid Gemini token boundary");return source.substr(start,pos-start);
    }
};
std::string_view member(std::string_view source,std::string_view key){Tokens tokens{source};tokens.space();if(tokens.pos==source.size()||source[tokens.pos++]!='{')throw ModelProtocolError("Invalid Gemini object slice");tokens.space();while(tokens.pos<source.size()&&source[tokens.pos]!='}'){
    const auto name=Json::parse(tokens.value()).get<std::string>();tokens.space();++tokens.pos;const auto value=tokens.value();if(name==key)return value;tokens.space();if(source[tokens.pos]==',')++tokens.pos;tokens.space();
}return {};}
std::vector<std::string_view> elements(std::string_view source){Tokens tokens{source};tokens.space();if(tokens.pos==source.size()||source[tokens.pos++]!='[')throw ModelProtocolError("Invalid Gemini array slice");std::vector<std::string_view> result;tokens.space();while(tokens.pos<source.size()&&source[tokens.pos]!=']'){result.push_back(tokens.value());tokens.space();if(source[tokens.pos]==',')++tokens.pos;tokens.space();}return result;}
std::string text(const Json& value,const char* field){if(!value.contains(field)||!value[field].is_string())throw ModelProtocolError("Invalid Gemini string field");return value[field].get<std::string>();}
void identity(const std::string& value,std::size_t bound=256){if(value.empty()||value.size()>bound||value.find('\0')!=std::string::npos)throw ModelProtocolError("Invalid Gemini identity");}
}
struct GeminiStream::Impl {
    ChatCompletionStream::Sink sink;GeminiCompletion result;Json usage=Json::object();
    std::vector<std::string> parts;std::set<std::string> ids;std::string line,data,event;
    std::size_t bytes=0;bool first_line=true,skip_lf=false,has_data=false,terminal=false,failed=false,acknowledged=false;
    explicit Impl(ChatCompletionStream::Sink value):sink(std::move(value)){if(!sink)throw std::invalid_argument("Model event sink is required");}
    void emit(const std::string& kind,const Json& value){sink({kind,value.dump()});}
    void metadata(const Json& value,const char* field,std::string& target){if(!value.contains(field))return;const auto observed=text(value,field);identity(observed);if(!target.empty()&&target!=observed)throw ModelProtocolError("Gemini response identity changed");target=observed;}
    void counters(const Json& value){
        if(!value.is_object())throw ModelProtocolError("Invalid Gemini usage");
        for(const auto* field:{"promptTokenCount","candidatesTokenCount","totalTokenCount","thoughtsTokenCount","cachedContentTokenCount","toolUsePromptTokenCount"})if(value.contains(field)){
            const auto& count=value[field];if(!count.is_number_integer()||count<0||count>9007199254740991||(usage.contains(field)&&count<usage[field]))throw ModelProtocolError("Invalid Gemini cumulative usage");usage[field]=count;
        }
        if(!usage.empty()){result.usage_json=usage.dump();emit("model.usage",usage);}
    }
    void part(const Json& value,std::string_view raw){
        if(!value.is_object()||value.empty())throw ModelProtocolError("Invalid Gemini content part");
        for(auto it=value.begin();it!=value.end();++it)if(it.key()!="text"&&it.key()!="functionCall"&&it.key()!="thought"&&it.key()!="thoughtSignature"&&it.key()!="partMetadata")throw ModelProtocolError("Unsupported Gemini content part");
        if(value.contains("thought")&&!value["thought"].is_boolean())throw ModelProtocolError("Invalid Gemini thought flag");
        if(value.contains("thoughtSignature"))identity(text(value,"thoughtSignature"),65536);
        if(value.contains("partMetadata")&&!value["partMetadata"].is_object())throw ModelProtocolError("Invalid Gemini part metadata");
        if(value.contains("text")&&value.contains("functionCall"))throw ModelProtocolError("Conflicting Gemini part data");
        if(value.contains("text")){
            const auto added=text(value,"text");if(!value.value("thought",false)){append(result.content,added);if(!added.empty())emit("model.text",{{"text",added}});}
        }else if(value.contains("functionCall")){
            const auto& call=value["functionCall"];if(!call.is_object()||result.function_calls.size()>=64)throw ModelProtocolError("Invalid Gemini function call");
            for(auto it=call.begin();it!=call.end();++it)if(it.key()!="id"&&it.key()!="name"&&it.key()!="args")throw ModelProtocolError("Unsupported Gemini function field");
            GeminiFunctionCall decoded;decoded.name=text(call,"name");if(decoded.name.empty()||decoded.name.size()>128||decoded.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw ModelProtocolError("Invalid Gemini function name");
            if(call.contains("id")){decoded.id=text(call,"id");identity(*decoded.id);if(!ids.insert(*decoded.id).second)throw ModelProtocolError("Duplicate Gemini function identity");}
            if(call.contains("args")){if(!call["args"].is_object())throw ModelProtocolError("Invalid Gemini function arguments");decoded.arguments_json=std::string(member(member(raw,"functionCall"),"args"));if(decoded.arguments_json->size()>1024*1024)throw ModelProtocolError("Gemini function arguments exceed limits");parse(*decoded.arguments_json,16);}
            result.function_calls.push_back(std::move(decoded));
        }else if(!value.contains("thoughtSignature"))throw ModelProtocolError("Gemini part has no supported data");
        if(parts.size()>=4096)throw ModelProtocolError("Gemini part count exceeds limits");parts.emplace_back(raw);
    }
    void dispatch(){
        if(!has_data)return;data.pop_back();if(!event.empty()&&event!="message")throw ModelProtocolError("Unsupported Gemini SSE event");
        const auto value=parse(data);if(!value.is_object())throw ModelProtocolError("Invalid Gemini response");
        if(value.contains("error"))throw ModelProtocolError("Gemini provider returned an error");
        metadata(value,"modelVersion",result.model_version);metadata(value,"responseId",result.response_id);
        if(value.contains("promptFeedback")){const auto& feedback=value["promptFeedback"];if(!feedback.is_object())throw ModelProtocolError("Invalid Gemini prompt feedback");if(feedback.contains("blockReason")&&text(feedback,"blockReason")!="BLOCK_REASON_UNSPECIFIED")throw ModelProtocolError("Gemini prompt was blocked");}
        if(value.contains("usageMetadata"))counters(value["usageMetadata"]);
        if(value.contains("candidates")){
            const auto& candidates=value["candidates"];if(!candidates.is_array()||candidates.size()>1)throw ModelProtocolError("Unsupported Gemini candidate count");
            if(!candidates.empty()){
                if(terminal)throw ModelProtocolError("Gemini candidate follows terminal response");const auto& candidate=candidates[0];if(!candidate.is_object())throw ModelProtocolError("Invalid Gemini candidate");
                if(candidate.contains("index")&&(!candidate["index"].is_number_integer()||candidate["index"]!=0))throw ModelProtocolError("Unsupported Gemini candidate index");
                if(candidate.contains("content")){
                    const auto& content=candidate["content"];if(!content.is_object()||(content.contains("role")&&text(content,"role")!="model")||!content.contains("parts")||!content["parts"].is_array())throw ModelProtocolError("Invalid Gemini model content");
                    const auto raw_candidates=elements(member(data,"candidates"));const auto raw_parts=elements(member(member(raw_candidates[0],"content"),"parts"));
                    for(std::size_t index=0;index<raw_parts.size();++index)part(content["parts"][index],raw_parts[index]);
                }
                if(candidate.contains("finishReason")){
                    const auto reason=text(candidate,"finishReason");if(reason.empty()||reason=="FINISH_REASON_UNSPECIFIED"){data.clear();event.clear();has_data=false;return;}if(reason!="STOP"&&reason!="MAX_TOKENS")throw ModelProtocolError("Gemini candidate did not complete successfully");
                    result.provider_finish_reason=reason;result.finish_reason=reason=="MAX_TOKENS"?"length":result.function_calls.empty()?"stop":"tool_calls";
                    if(reason=="MAX_TOKENS")result.function_calls.clear();terminal=true;
                }
            }
        }
        data.clear();event.clear();has_data=false;
    }
    void end_line(){
        if(first_line){first_line=false;if(line.starts_with("\xEF\xBB\xBF"))line.erase(0,3);}
        if(line.empty()){dispatch();event.clear();}
        else if(!line.starts_with(':')){const auto colon=line.find(':');const auto field=line.substr(0,colon);auto value=colon==std::string::npos?std::string_view{}:std::string_view(line).substr(colon+1);if(value.starts_with(' '))value.remove_prefix(1);if(field=="data"){append(data,value);append(data,"\n");has_data=true;}else if(field=="event"){if(value.size()>256)throw ModelProtocolError("Gemini SSE event exceeds limits");event=value;}}
        line.clear();
    }
    void feed(std::string_view value){if(failed||acknowledged)throw ModelProtocolError("Gemini stream is invalid");if(value.size()>64*1024*1024-bytes)throw ModelProtocolError("Gemini stream exceeds limits");bytes+=value.size();for(char byte:value){if(skip_lf){skip_lf=false;if(byte=='\n')continue;}if(byte=='\r'){end_line();skip_lf=true;}else if(byte=='\n')end_line();else append(line,std::string_view(&byte,1),1024*1024);}}
};
GeminiStream::GeminiStream(ChatCompletionStream::Sink sink):impl_(std::make_unique<Impl>(std::move(sink))){}
GeminiStream::~GeminiStream()=default;
void GeminiStream::feed(std::string_view bytes){try{impl_->feed(bytes);}catch(const Json::exception&){impl_->failed=true;throw ModelProtocolError("Invalid Gemini JSON event");}catch(...){impl_->failed=true;throw;}}
GeminiCompletion GeminiStream::finish(){
    if(impl_->failed||!impl_->terminal||!impl_->line.empty()||impl_->has_data||!impl_->event.empty())throw ModelProtocolError("Incomplete Gemini stream");
    if(!impl_->acknowledged){try{
        impl_->result.parts_json="[";bool first=true;for(const auto& part:impl_->parts){if(!first)append(impl_->result.parts_json,",");first=false;append(impl_->result.parts_json,part);}append(impl_->result.parts_json,"]");
        impl_->emit("model.finish",{{"reason",impl_->result.finish_reason},{"provider_reason",impl_->result.provider_finish_reason}});impl_->emit("model.done",Json::object());impl_->acknowledged=true;
    }catch(...){impl_->failed=true;throw;}}
    return impl_->result;
}
}
