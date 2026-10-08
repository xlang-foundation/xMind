#include "agentflow/anthropic_stream.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <initializer_list>
#include <set>
namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t limit=4*1024*1024,object_limit=1024*1024,receipt_limit=8*1024*1024,signature_limit=65536;
void append(std::string& target,std::string_view bytes,std::size_t bound=limit){if(target.size()>bound||bytes.size()>bound-target.size())throw ModelProtocolError("Claude stream exceeds limits");target.append(bytes);}
Json parse(std::string_view source,int maximum_depth=64){std::vector<std::set<std::string>> keys;return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){if(depth>maximum_depth)throw ModelProtocolError("Claude JSON nesting exceeds limits");if(event==Json::parse_event_t::object_start)keys.emplace_back();else if(event==Json::parse_event_t::object_end)keys.pop_back();else if(event==Json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)throw ModelProtocolError("Duplicate Claude JSON field");return true;});}
std::string text(const Json& value,const char* key){if(!value.is_object()||!value.contains(key)||!value[key].is_string())throw ModelProtocolError("Invalid Claude string field");return value[key].get<std::string>();}
void identity(const std::string& value){if(value.empty()||value.size()>256||value.find('\0')!=std::string::npos)throw ModelProtocolError("Invalid Claude identity");}
void fields(const Json& value,std::initializer_list<std::string_view> allowed){if(!value.is_object())throw ModelProtocolError("Invalid Claude block object");for(auto it=value.begin();it!=value.end();++it)if(std::find(allowed.begin(),allowed.end(),it.key())==allowed.end())throw ModelProtocolError("Unsupported Claude block field");}
void opaque(const std::string& value,std::size_t bound){if(value.empty()||value.size()>bound||value.find('\0')!=std::string::npos)throw ModelProtocolError("Invalid Claude opaque content");}
// Locate token slices only after strict JSON validation. Parsing an input object
// into doubles and dumping it would alter tool arguments and signed replay.
struct Tokens {
    std::string_view source;std::size_t position=0;
    void space(){while(position<source.size()&&std::string_view(" \r\n\t").find(source[position])!=std::string_view::npos)++position;}
    char take(){if(position>=source.size())throw ModelProtocolError("Invalid Claude token boundary");return source[position++];}
    void expect(char byte){space();if(take()!=byte)throw ModelProtocolError("Invalid Claude token boundary");}
    std::string_view value(){
        space();const auto start=position;const auto first=take();
        if(first=='"'){for(;;){const auto byte=take();if(byte=='\\')take();else if(byte=='"')break;}}
        else if(first=='{'||first=='['){
            const auto end=first=='{'?'}':']';space();if(position>=source.size())throw ModelProtocolError("Invalid Claude token boundary");
            if(source[position]==end)++position;
            else for(;;){value();if(first=='{'){expect(':');value();}space();const auto separator=take();if(separator==end)break;if(separator!=',')throw ModelProtocolError("Invalid Claude token boundary");}
        }else while(position<source.size()&&std::string_view(" \r\n\t,]}:").find(source[position])==std::string_view::npos)++position;
        return source.substr(start,position-start);
    }
};
std::string_view member(std::string_view source,std::string_view wanted){
    Tokens tokens{source};tokens.expect('{');tokens.space();if(tokens.position>=source.size())throw ModelProtocolError("Invalid Claude token boundary");if(source[tokens.position]=='}')return {};
    for(;;){const auto name=Json::parse(tokens.value()).get<std::string>();tokens.expect(':');const auto value=tokens.value();if(name==wanted)return value;tokens.space();const auto separator=tokens.take();if(separator=='}')return {};if(separator!=',')throw ModelProtocolError("Invalid Claude token boundary");}
}
Json tool_input(std::string_view source){if(source.size()>object_limit)throw ModelProtocolError("Claude tool input exceeds limits");auto value=parse(source,16);if(!value.is_object())throw ModelProtocolError("Claude tool input is not an object");return value;}
}
struct AnthropicStream::Impl {
    struct Block {std::string type,id,name,value,input,signature;bool stopped=false,initial_input_empty=false,input_delta=false,signature_phase=false;};
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
            if(delta)throw ModelProtocolError("Claude terminal update repeated");
            const auto& change=value.at("delta");const auto& reported=value.at("usage");if(!change.is_object()||!change.contains("stop_reason")||!reported.contains("output_tokens"))throw ModelProtocolError("Claude message update is invalid");
            if(change["stop_reason"].is_null()){counters(reported);return;}
            if(!closed())throw ModelProtocolError("Claude terminal update has active blocks");const auto stop=text(change,"stop_reason");
            if(stop!="end_turn"&&stop!="stop_sequence"&&stop!="tool_use"&&stop!="max_tokens")throw ModelProtocolError("Unsupported Claude stop reason");
            reason=stop;delta=true;counters(reported);result.finish_reason=stop=="tool_use"?"tool_calls":stop=="max_tokens"?"length":"stop";emit("model.finish",{{"reason",result.finish_reason},{"provider_reason",stop}});return;
        }
        if(type=="message_stop"){
            if(!delta||!closed())throw ModelProtocolError("Incomplete Claude message lifecycle");
            std::string content="[";bool first_block=true;
            for(const auto& block:blocks){
                if(!first_block)append(content,",",receipt_limit);first_block=false;std::string encoded;
                if(block.type=="text"){append(result.content,block.value);encoded=Json{{"type","text"},{"text",block.value}}.dump();}
                else if(block.type=="tool_use"){
                    if(reason=="tool_use")result.tool_calls.push_back({block.id,block.name,block.input});
                    encoded="{\"type\":\"tool_use\",\"id\":"+Json(block.id).dump()+",\"name\":"+Json(block.name).dump()+",\"input\":"+block.input+'}';
                }else if(block.type=="thinking")encoded=Json{{"type","thinking"},{"thinking",block.value},{"signature",block.signature}}.dump();
                else if(block.type=="redacted_thinking")encoded=Json{{"type","redacted_thinking"},{"data",block.value}}.dump();
                append(content,encoded,receipt_limit);
            }
            append(content,"]",receipt_limit);
            if(reason=="tool_use"&&result.tool_calls.empty())throw ModelProtocolError("Claude tool stop has no calls");
            if(reason!="tool_use"&&reason!="max_tokens"&&!calls.empty())throw ModelProtocolError("Claude tool calls have incompatible stop reason");
            // Truncated tool blocks are never executable and cannot masquerade
            // as a receipt matching the empty executable-call list.
            if(reason!="max_tokens"||calls.empty()){
                const Json saved={{"type","anthropic_content"},{"content_json",content},{"finish_reason",result.finish_reason},{"provider_finish_reason",reason}};
                result.provider_items_json=Json::array({saved}).dump();
                if(result.provider_items_json.size()>receipt_limit)throw ModelProtocolError("Claude receipt exceeds limits");
            }
            done=true;return;
        }
        if(delta)throw ModelProtocolError("Claude content follows terminal update");
        if(type=="content_block_start"){
            if(static_cast<std::size_t>(position(value))!=blocks.size())throw ModelProtocolError("Claude block sequence is invalid");const auto& content=value.at("content_block");Block block;block.type=text(content,"type");
            if(block.type=="text"){fields(content,{"type","text"});block.value=text(content,"text");if(block.value.size()>limit)throw ModelProtocolError("Claude content exceeds limits");if(!block.value.empty())emit("model.text",{{"text",block.value}});}
            else if(block.type=="tool_use"){
                fields(content,{"type","id","name","input"});block.id=text(content,"id");identity(block.id);block.name=text(content,"name");if(block.name.empty()||block.name.size()>64||block.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos||!calls.insert(block.id).second)throw ModelProtocolError("Invalid Claude tool block");
                block.input=member(member(data,"content_block"),"input");block.initial_input_empty=tool_input(block.input).empty();
                emit("model.tool_delta",{{"index",blocks.size()},{"id",block.id},{"name",block.name},{"arguments",""}});
            }else if(block.type=="thinking"){
                fields(content,{"type","thinking","signature"});block.value=text(content,"thinking");if(block.value.size()>limit)throw ModelProtocolError("Claude thinking exceeds limits");
                if(content.contains("signature")){block.signature=text(content,"signature");if(!block.signature.empty())opaque(block.signature,signature_limit);block.signature_phase=!block.signature.empty();}
            }else if(block.type=="redacted_thinking"){
                fields(content,{"type","data"});block.value=text(content,"data");opaque(block.value,limit);
            }else throw ModelProtocolError("Unsupported Claude content block");
            blocks.push_back(std::move(block));return;
        }
        if(type=="content_block_delta"){
            auto& block=active(value);const auto& change=value.at("delta");const auto kind=text(change,"type");
            if(block.type=="text"&&kind=="text_delta"){fields(change,{"type","text"});const auto added=text(change,"text");append(block.value,added);emit("model.text",{{"text",added}});}
            else if(block.type=="tool_use"&&kind=="input_json_delta"){fields(change,{"type","partial_json"});if(!block.initial_input_empty)throw ModelProtocolError("Claude tool input has conflicting initial content");const auto added=text(change,"partial_json");block.input_delta=true;append(block.value,added,object_limit);emit("model.tool_delta",{{"index",position(value)},{"id",block.id},{"name",block.name},{"arguments",added}});}
            else if(block.type=="thinking"&&kind=="thinking_delta"){fields(change,{"type","thinking"});if(block.signature_phase)throw ModelProtocolError("Claude thinking follows signature");append(block.value,text(change,"thinking"));}
            else if(block.type=="thinking"&&kind=="signature_delta"){fields(change,{"type","signature"});block.signature_phase=true;const auto added=text(change,"signature");if(added.find('\0')!=std::string::npos)throw ModelProtocolError("Invalid Claude signature delta");append(block.signature,added,signature_limit);}
            else throw ModelProtocolError("Unsupported Claude block delta");return;
        }
        if(type=="content_block_stop"){
            auto& block=active(value);
            if(block.type=="tool_use"&&block.input_delta){tool_input(block.value);block.input=std::move(block.value);}
            if(block.type=="thinking")opaque(block.signature,signature_limit);
            block.stopped=true;return;
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
