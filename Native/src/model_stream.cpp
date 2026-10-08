#include "agentflow/model_stream.hpp"
#include "nlohmann/json.hpp"
#include <map>
#include <set>
#include <initializer_list>
#include <utility>

namespace agentflow {
std::string_view model_protocol_diagnostic(const ModelProtocolError& error) {
    const std::string_view message=error.what();
    for(const auto& [text,code]:std::initializer_list<std::pair<std::string_view,std::string_view>>{
        {"Responses final function differs from stream","responses_function_final_mismatch"},
        {"Responses final arguments differ from deltas","responses_arguments_mismatch"},
        {"Responses terminal output differs from completed items","responses_terminal_mismatch"},
        {"Unsupported Responses event","responses_event_unsupported"},
        {"Unsupported Responses output item","responses_item_unsupported"},
        {"Responses reasoning lacks stateless continuation","responses_reasoning_continuation_missing"},
        {"Unsupported or incomplete Responses reasoning","responses_reasoning_incomplete"},
        {"Responses item did not complete","responses_item_incomplete"},
        {"Responses turn did not complete","responses_turn_incomplete"},
        {"Responses event has no active item","responses_item_lifecycle_invalid"},
        {"Invalid Responses function lifecycle","responses_function_lifecycle_invalid"},
        {"Responses function starts with unexpected arguments","responses_function_start_invalid"},
        {"Invalid provider JSON event","provider_json_invalid"},
        {"Invalid Responses JSON event","responses_json_invalid"},
        {"Incomplete Responses stream","responses_stream_incomplete"},
        {"Provider response failed or was incomplete","responses_provider_incomplete"},
        {"Invalid Responses event sequence","responses_sequence_invalid"},
        {"Responses SSE event type differs from payload","responses_event_type_mismatch"},
        {"Responses item identity changed","responses_item_identity_mismatch"},
        {"Responses response identity changed","responses_identity_mismatch"},
        {"Responses model identity changed","responses_model_identity_mismatch"},
        {"Responses final item identity changed","responses_final_identity_mismatch"},
        {"Responses final message differs from stream","responses_message_mismatch"},
        {"Responses final text differs from stream","responses_text_mismatch"},
        {"Responses summary differs from stream","responses_summary_mismatch"},
        {"Invalid Responses usage","responses_usage_invalid"},
        {"Invalid Responses token count","responses_usage_invalid"},
        {"Invalid Responses token details","responses_usage_invalid"},
        {"Responses stream exceeds limits","responses_limit_exceeded"},
        {"Responses continuation exceeds limits","responses_limit_exceeded"},
        {"Incomplete or invalid model stream","chat_stream_incomplete"}})
        if(message==text)return code;
    return {};
}
namespace {
using Json=nlohmann::json;
constexpr std::size_t max_line=1024*1024,max_event=4*1024*1024,max_stream=64*1024*1024,max_value=4*1024*1024;
Json protocol_json(const std::string& source) {
    std::vector<std::set<std::string>> fields;
    return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value) {
        if(depth>64) throw ModelProtocolError("Model JSON nesting exceeds configured limits");
        if(event==Json::parse_event_t::object_start) fields.emplace_back();
        else if(event==Json::parse_event_t::object_end) fields.pop_back();
        else if(event==Json::parse_event_t::key && !fields.back().insert(value.get<std::string>()).second)
            throw ModelProtocolError("Duplicate model JSON field");
        return true;
    });
}
void bounded_append(std::string& target,std::string_view value,std::size_t limit) {
    if(value.size()>limit-target.size()) throw ModelProtocolError("Model stream exceeds configured limits");
    target.append(value);
}
std::string optional_string(const Json& object,const char* key) {
    if(!object.contains(key) || object[key].is_null()) return {};
    if(!object[key].is_string()) throw ModelProtocolError("Invalid model string field");
    return object[key].get<std::string>();
}
}
struct ChatCompletionStream::Impl {
    Sink sink;
    ModelCompletion completion;
    std::map<int,ModelToolCall> calls;
    std::string line,data,response_id,model;
    std::size_t total=0;
    bool skip_lf=false,first_line=true,has_data=false,done=false,finished=false,failed=false;
    explicit Impl(Sink callback):sink(std::move(callback)) {if(!sink) throw std::invalid_argument("Model event sink is required");}
    void emit(const std::string& kind,const Json& value) {sink({kind,value.dump()});}
    void validate_calls() {
        std::set<std::string> identities;
        if(completion.finish_reason=="tool_calls" && calls.empty()) throw ModelProtocolError("Tool finish without tool calls");
        for(const auto& [index,call]:calls) {
            (void)index;
            if(call.id.empty() || call.id.size()>256 || call.name.empty() || call.name.size()>256 || !identities.insert(call.id).second)
                throw ModelProtocolError("Incomplete or duplicate model tool identity");
            if(completion.finish_reason=="tool_calls") {
                const auto arguments=protocol_json(call.arguments_json);
                if(!arguments.is_object()) throw ModelProtocolError("Tool arguments must be a JSON object");
            }
        }
        if(!calls.empty() && completion.finish_reason=="stop") throw ModelProtocolError("Tool calls with an inconsistent finish reason");
    }
    void chunk(const std::string& value) {
        if(done) throw ModelProtocolError("Model data after end marker");
        if(value=="[DONE]") {
            if(!finished) throw ModelProtocolError("Model end marker without a finish reason");
            validate_calls();done=true;emit("model.done",Json::object());return;
        }
        const auto object=protocol_json(value);
        if(!object.is_object() || object.contains("error")) throw ModelProtocolError("Provider returned an invalid or error event");
        for(auto pair:{std::pair{"id",&response_id},std::pair{"model",&model}}) {
            const auto current=optional_string(object,pair.first);
            if(!current.empty()) {
                if(!pair.second->empty() && current!=*pair.second) throw ModelProtocolError("Model response identity changed");
                *pair.second=current;
            }
        }
        if(object.contains("usage") && !object["usage"].is_null()) {
            if(!object["usage"].is_object()) throw ModelProtocolError("Invalid model usage");
            for(const auto* key:{"prompt_tokens","completion_tokens","total_tokens"}) {
                if(object["usage"].contains(key) && (!object["usage"][key].is_number_integer() || object["usage"][key]<0))
                    throw ModelProtocolError("Invalid model token count");
            }
            completion.usage_json=object["usage"].dump();emit("model.usage",object["usage"]);
        }
        if(!object.contains("choices") || !object["choices"].is_array()) throw ModelProtocolError("Missing model choices");
        for(const auto& choice:object["choices"]) {
            if(!choice.is_object() || !choice.contains("index") || !choice["index"].is_number_integer() || choice["index"]!=0)
                throw ModelProtocolError("This request supports one completion choice");
            if(finished) throw ModelProtocolError("Choice data after its finish reason");
            const auto delta=choice.value("delta",Json::object());
            if(!delta.is_object()) throw ModelProtocolError("Invalid model delta");
            const auto role=optional_string(delta,"role");
            if(!role.empty() && role!="assistant") throw ModelProtocolError("Invalid model response role");
            for(auto pair:{std::pair{"content",&completion.content},std::pair{"refusal",&completion.refusal}}) {
                const auto text=optional_string(delta,pair.first);
                if(!text.empty()) {bounded_append(*pair.second,text,max_value);emit(pair.first==std::string_view("content")?"model.text":"model.refusal",{{"text",text}});}
            }
            if(delta.contains("tool_calls") && !delta["tool_calls"].is_null()) {
                if(!delta["tool_calls"].is_array()) throw ModelProtocolError("Invalid model tool delta");
                for(const auto& tool:delta["tool_calls"]) {
                    if(!tool.is_object() || !tool.contains("index") || !tool["index"].is_number_integer()) throw ModelProtocolError("Missing tool index");
                    const auto number=tool["index"].get<std::int64_t>();
                    if(number<0 || number>=64) throw ModelProtocolError("Tool index exceeds configured limits");
                    const auto type=optional_string(tool,"type");
                    if(!type.empty() && type!="function") throw ModelProtocolError("Unsupported tool type");
                    auto& call=calls[static_cast<int>(number)];
                    const auto id=optional_string(tool,"id");
                    if(!id.empty()) {
                        if(!call.id.empty() && call.id!=id) throw ModelProtocolError("Tool identity changed");call.id=id;
                    }
                    const auto function=tool.value("function",Json::object());
                    if(!function.is_object()) throw ModelProtocolError("Invalid tool function");
                    const auto name=optional_string(function,"name"),arguments=optional_string(function,"arguments");
                    bounded_append(call.name,name,256);bounded_append(call.arguments_json,arguments,max_value);
                    emit("model.tool_delta",{{"index",number},{"id",id},{"name",name},{"arguments",arguments}});
                }
            }
            // Preserve extensions such as provider reasoning for provider-specific
            // continuation; never silently turn them into ordinary answer text.
            auto extensions=delta;
            for(const auto* key:{"role","content","refusal","tool_calls"}) extensions.erase(key);
            if(!extensions.empty()) emit("model.extension",extensions);
            const auto reason=optional_string(choice,"finish_reason");
            if(!reason.empty()) {
                if(reason!="stop" && reason!="tool_calls" && reason!="length" && reason!="content_filter")
                    throw ModelProtocolError("Unsupported model finish reason");
                completion.finish_reason=reason;finished=true;emit("model.finish",{{"reason",reason}});
            }
        }
    }
    void end_line() {
        if(first_line) {
            if(line.starts_with("\xef\xbb\xbf")) line.erase(0,3);
            first_line=false;
        }
        if(line.empty()) {
            if(has_data) {
                if(!data.empty()) data.pop_back();
                chunk(data);data.clear();has_data=false;
            }
        } else if(line[0]!=':') {
            const auto colon=line.find(':');const auto field=line.substr(0,colon);
            auto value=colon==std::string::npos?std::string_view{}:std::string_view(line).substr(colon+1);
            if(value.starts_with(' ')) value.remove_prefix(1);
            if(field=="data") {bounded_append(data,value,max_event);bounded_append(data,"\n",max_event);has_data=true;}
        }
        line.clear();
    }
    void feed(std::string_view bytes) {
        if(failed) throw ModelProtocolError("Model stream is invalid");
        if(bytes.size()>max_stream-total) throw ModelProtocolError("Model stream exceeds configured limits");
        total+=bytes.size();
        for(char c:bytes) {
            if(skip_lf) {skip_lf=false;if(c=='\n') continue;}
            if(c=='\r') {end_line();skip_lf=true;}
            else if(c=='\n') end_line();
            else bounded_append(line,std::string_view(&c,1),max_line);
        }
    }
};
ChatCompletionStream::ChatCompletionStream(Sink sink):impl_(std::make_unique<Impl>(std::move(sink))) {}
ChatCompletionStream::~ChatCompletionStream()=default;
void ChatCompletionStream::feed(std::string_view bytes) {
    try {impl_->feed(bytes);}
    catch(const Json::exception&) {impl_->failed=true;throw ModelProtocolError("Invalid provider JSON event");}
    catch(...) {impl_->failed=true;throw;}
}
ModelCompletion ChatCompletionStream::finish() {
    if(impl_->failed || !impl_->done || !impl_->line.empty() || impl_->has_data) throw ModelProtocolError("Incomplete or invalid model stream");
    auto result=impl_->completion;
    // Truncated/filtered tool fragments are not executable calls.
    if(result.finish_reason=="tool_calls") for(const auto& [index,call]:impl_->calls) {(void)index;result.tool_calls.push_back(call);}
    return result;
}
}
