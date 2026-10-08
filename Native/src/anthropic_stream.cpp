#include "agentflow/anthropic_stream.hpp"
#include "nlohmann/json.hpp"
#include <set>
namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t limit=4*1024*1024;
void append(std::string& target,std::string_view bytes,std::size_t bound=limit){if(bytes.size()>bound-target.size())throw ModelProtocolError("Claude stream exceeds limits");target.append(bytes);}
Json parse(const std::string& source){std::vector<std::set<std::string>> keys;return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){if(depth>64)throw ModelProtocolError("Claude JSON nesting exceeds limits");if(event==Json::parse_event_t::object_start)keys.emplace_back();else if(event==Json::parse_event_t::object_end)keys.pop_back();else if(event==Json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)throw ModelProtocolError("Duplicate Claude JSON field");return true;});}
std::string text(const Json& value,const char* key){if(!value.is_object()||!value.contains(key)||!value[key].is_string())throw ModelProtocolError("Invalid Claude string field");return value[key].get<std::string>();}
void identity(const std::string& value){if(value.empty()||value.size()>256||value.find('\0')!=std::string::npos)throw ModelProtocolError("Invalid Claude identity");}
}
struct AnthropicStream::Impl {
    struct Block {std::string type,id,name,value;Json input;bool stopped=false;};
    ChatCompletionStream::Sink sink;std::vector<Block> blocks;std::set<std::string> calls;Json usage=Json::object();ModelCompletion result;
    std::string line,data,event,reason;std::size_t bytes=0;bool first=true,skip_lf=false,has_data=false,started=false,delta=false,done=false,failed=false,acknowledged=false;
    explicit Impl(ChatCompletionStream::Sink value):sink(std::move(value)){if(!sink)throw std::invalid_argument("Model event sink is required");}
    void emit(const std::string& kind,const Json& value){sink({kind,value.dump()});}
    void counters(const Json& value){
        if(!value.is_object())throw ModelProtocolError("Invalid Claude usage");
        for(const auto* key:{"input_tokens","output_tokens","cache_creation_input_tokens","cache_read_input_tokens"})if(value.contains(key)){
            const auto& number=value[key];if(!number.is_number_integer()||number<0||number>9007199254740991||(usage.contains(key)&&number<usage[key]))throw ModelProtocolError("Invalid Claude cumulative usage");usage[key]=number;
        }
        // Retain actual named token counters only; no invented total/estimates.
        result.usage_json=usage.dump();emit("model.usage",usage);
    }
    int position(const Json& value){if(!value.contains("index")||!value["index"].is_number_integer()||value["index"]<0||value["index"]>=64)throw ModelProtocolError("Invalid Claude block index");return value["index"].get<int>();}
    Block& active(const Json& value){const auto index=position(value);if(static_cast<std::size_t>(index)>=blocks.size()||blocks[index].stopped)throw ModelProtocolError("Claude block is not active");return blocks[index];}
    bool closed()const{for(const auto& block:blocks)if(!block.stopped)return false;return true;}
    void chunk(){
        const auto value=parse(data);const auto type=text(value,"type");if(event!=type)throw ModelProtocolError("Claude SSE type differs from payload");
        if(done)throw ModelProtocolError("Claude data follows terminal event");
        if(type=="error")throw ModelProtocolError("Claude provider stream failed");
        if(type=="ping")return;
        if(type=="message_start"){
            const auto& message=value.at("message");if(started||message.value("type",Json{})!="message"||message.value("role",Json{})!="assistant"||!message.at("content").is_array()||!message["content"].empty()||!message.at("stop_reason").is_null()||!message.at("stop_sequence").is_null())throw ModelProtocolError("Invalid Claude message start");
            identity(text(message,"id"));identity(text(message,"model"));const auto& initial=message.at("usage");if(!initial.contains("input_tokens")||!initial.contains("output_tokens"))throw ModelProtocolError("Claude initial usage is missing");counters(initial);started=true;return;
        }
        if(!started)throw ModelProtocolError("Claude message has not started");
        if(type=="message_delta"){
            const auto& change=value.at("delta");const auto& reported=value.at("usage");if(!change.is_object()||!change.contains("stop_reason")||!reported.contains("output_tokens"))throw ModelProtocolError("Claude message update is invalid");
            if(change["stop_reason"].is_null()){counters(reported);return;}
            if(!closed())throw ModelProtocolError("Claude terminal update has active blocks");const auto stop=text(change,"stop_reason");
            if(stop!="end_turn"&&stop!="stop_sequence"&&stop!="tool_use"&&stop!="max_tokens")throw ModelProtocolError("Unsupported Claude stop reason");
            if(delta&&reason!=stop)throw ModelProtocolError("Claude stop reason changed");reason=stop;delta=true;counters(reported);result.finish_reason=stop=="tool_use"?"tool_calls":stop=="max_tokens"?"length":"stop";emit("model.finish",{{"reason",result.finish_reason},{"provider_reason",stop}});return;
        }
        if(type=="message_stop"){
            if(!delta||!closed())throw ModelProtocolError("Incomplete Claude message lifecycle");
            for(const auto& block:blocks){if(block.type=="text")append(result.content,block.value);else if(reason=="tool_use")result.tool_calls.push_back({block.id,block.name,block.input.dump()});}
            if(reason=="tool_use"&&result.tool_calls.empty())throw ModelProtocolError("Claude tool stop has no calls");
            if(reason!="tool_use"&&reason!="max_tokens"&&!calls.empty())throw ModelProtocolError("Claude tool calls have incompatible stop reason");done=true;return;
        }
        if(delta)throw ModelProtocolError("Claude content follows terminal update");
        if(type=="content_block_start"){
            if(static_cast<std::size_t>(position(value))!=blocks.size())throw ModelProtocolError("Claude block sequence is invalid");const auto& content=value.at("content_block");Block block;block.type=text(content,"type");
            if(block.type=="text"){block.value=text(content,"text");if(block.value.size()>limit)throw ModelProtocolError("Claude content exceeds limits");if(!block.value.empty())emit("model.text",{{"text",block.value}});}
            else if(block.type=="tool_use"){
                block.id=text(content,"id");identity(block.id);block.name=text(content,"name");if(block.name.empty()||block.name.size()>64||block.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos||!calls.insert(block.id).second||!content.at("input").is_object())throw ModelProtocolError("Invalid Claude tool block");block.input=content["input"];
                emit("model.tool_delta",{{"index",blocks.size()},{"id",block.id},{"name",block.name},{"arguments",""}});
            }else throw ModelProtocolError("Unsupported Claude content block");
            blocks.push_back(std::move(block));return;
        }
        if(type=="content_block_delta"){
            auto& block=active(value);const auto& change=value.at("delta");const auto kind=text(change,"type");
            if(block.type=="text"&&kind=="text_delta"){const auto added=text(change,"text");append(block.value,added);emit("model.text",{{"text",added}});}
            else if(block.type=="tool_use"&&kind=="input_json_delta"){if(!block.input.empty())throw ModelProtocolError("Claude tool input has conflicting initial content");const auto added=text(change,"partial_json");append(block.value,added);emit("model.tool_delta",{{"index",position(value)},{"id",block.id},{"name",block.name},{"arguments",added}});}
            else throw ModelProtocolError("Unsupported Claude block delta");return;
        }
        if(type=="content_block_stop"){
            auto& block=active(value);if(block.type=="tool_use"&&!block.value.empty()){block.input=parse(block.value);if(!block.input.is_object())throw ModelProtocolError("Claude tool input is not an object");}block.stopped=true;return;
        }
        throw ModelProtocolError("Unsupported Claude stream event");
    }
    void end_line(){
        if(first){if(line.starts_with("\xef\xbb\xbf"))line.erase(0,3);first=false;}
        if(line.empty()){if(has_data){data.pop_back();chunk();data.clear();has_data=false;}event.clear();}
        else if(line[0]!=':'){const auto colon=line.find(':');const auto field=line.substr(0,colon);auto value=colon==std::string::npos?std::string_view{}:std::string_view(line).substr(colon+1);if(value.starts_with(' '))value.remove_prefix(1);if(field=="event"){if(value.size()>256)throw ModelProtocolError("Claude SSE event name exceeds limits");event=value;}else if(field=="data"){append(data,value);append(data,"\n");has_data=true;}}
        line.clear();
    }
    void feed(std::string_view value){if(failed||acknowledged)throw ModelProtocolError("Claude stream is invalid");if(value.size()>64*1024*1024-bytes)throw ModelProtocolError("Claude stream exceeds limits");bytes+=value.size();for(char byte:value){if(skip_lf){skip_lf=false;if(byte=='\n')continue;}if(byte=='\r'){end_line();skip_lf=true;}else if(byte=='\n')end_line();else append(line,std::string_view(&byte,1),1024*1024);}}
};
AnthropicStream::AnthropicStream(ChatCompletionStream::Sink sink):impl_(std::make_unique<Impl>(std::move(sink))){}
AnthropicStream::~AnthropicStream()=default;
void AnthropicStream::feed(std::string_view bytes){try{impl_->feed(bytes);}catch(const Json::exception&){impl_->failed=true;throw ModelProtocolError("Invalid Claude JSON event");}catch(...){impl_->failed=true;throw;}}
ModelCompletion AnthropicStream::finish(){if(impl_->failed||!impl_->done||!impl_->line.empty()||impl_->has_data||!impl_->event.empty())throw ModelProtocolError("Incomplete Claude stream");if(!impl_->acknowledged){try{impl_->emit("model.done",Json::object());impl_->acknowledged=true;}catch(...){impl_->failed=true;throw;}}return impl_->result;}
}
