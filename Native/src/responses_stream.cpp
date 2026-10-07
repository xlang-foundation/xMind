#include "agentflow/responses_stream.hpp"
#include "nlohmann/json.hpp"
#include <map>
#include <set>
namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t max_value=4*1024*1024;
Json parse(const std::string& source){std::vector<std::set<std::string>> keys;return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){if(depth>64)throw ModelProtocolError("Responses JSON exceeds nesting limits");if(event==Json::parse_event_t::object_start)keys.emplace_back();else if(event==Json::parse_event_t::object_end)keys.pop_back();else if(event==Json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)throw ModelProtocolError("Duplicate Responses JSON field");return true;});}
void append(std::string& target,std::string_view value,std::size_t limit=max_value){if(value.size()>limit-target.size())throw ModelProtocolError("Responses stream exceeds limits");target.append(value);}
std::string text(const Json& value,const char* key){if(!value.contains(key)||!value[key].is_string())throw ModelProtocolError("Missing Responses string field");return value[key].get<std::string>();}
int index(const Json& value,const char* key,int limit=1024){if(!value.contains(key)||!value[key].is_number_integer()||value[key]<0||value[key]>=limit)throw ModelProtocolError("Invalid Responses item position");return value[key].get<int>();}
void identity(const std::string& value){if(value.empty()||value.size()>256||value.find('\0')!=std::string::npos)throw ModelProtocolError("Invalid Responses identity");}
}
struct ResponsesStream::Impl {
    struct Part {std::string type,value;bool done=false;};
    struct Item {std::string id,type,call_id,name,arguments;std::map<int,Part> parts,summaries;Json final;bool done=false,args_done=false;};
    ChatCompletionStream::Sink sink;std::map<int,Item> items;std::set<std::string> ids,calls;
    std::string line,data,event_name,response_id,model;std::int64_t sequence=-1;std::size_t bytes=0;
    bool skip_lf=false,first_line=true,has_data=false,created=false,done=false,failed=false;
    ModelCompletion completion;
    explicit Impl(ChatCompletionStream::Sink callback):sink(std::move(callback)){if(!sink)throw std::invalid_argument("Model event sink is required");}
    void emit(const std::string& kind,const Json& value){sink({kind,value.dump()});}
    Item& item(const Json& value){const auto pos=index(value,"output_index");const auto found=items.find(pos);if(found==items.end()||found->second.done)throw ModelProtocolError("Responses event has no active item");auto& result=found->second;if(value.contains("item_id")&&text(value,"item_id")!=result.id)throw ModelProtocolError("Responses item identity changed");return result;}
    void response_identity(const Json& response){if(!response.is_object()||text(response,"id")!=response_id)throw ModelProtocolError("Responses response identity changed");if(response.contains("model")){const auto current=text(response,"model");if(!model.empty()&&model!=current)throw ModelProtocolError("Responses model identity changed");model=current;}}
    void final_item(Item& current,const Json& value){
        if(text(value,"id")!=current.id||text(value,"type")!=current.type)throw ModelProtocolError("Responses final item identity changed");
        if(value.contains("status")&&value["status"]!="completed")throw ModelProtocolError("Responses item did not complete");
        if(current.type=="message"){
            if(value.value("role",Json{})!="assistant"||!value.contains("content")||!value["content"].is_array()||value["content"].size()!=current.parts.size())throw ModelProtocolError("Responses final message differs from stream");
            for(std::size_t i=0;i<value["content"].size();++i){const auto found=current.parts.find(static_cast<int>(i));if(found==current.parts.end()||!found->second.done)throw ModelProtocolError("Responses content did not complete");const auto& part=value["content"][i];const auto kind=text(part,"type");if(kind!=found->second.type||text(part,kind=="output_text"?"text":"refusal")!=found->second.value)throw ModelProtocolError("Responses final text differs from stream");}
        }else if(current.type=="function_call"){
            if(!current.args_done||text(value,"call_id")!=current.call_id||text(value,"name")!=current.name||text(value,"arguments")!=current.arguments||!parse(current.arguments).is_object())throw ModelProtocolError("Responses final function differs from stream");
        }else if(current.type=="reasoning"){
            if((value.contains("content")&&value["content"]!=Json::array())||!value.contains("summary")||!value["summary"].is_array()||value["summary"].size()!=current.summaries.size())throw ModelProtocolError("Unsupported or incomplete Responses reasoning");
            for(std::size_t i=0;i<value["summary"].size();++i){const auto found=current.summaries.find(static_cast<int>(i));if(found==current.summaries.end()||!found->second.done||value["summary"][i].value("type",Json{})!="summary_text"||text(value["summary"][i],"text")!=found->second.value)throw ModelProtocolError("Responses summary differs from stream");}
            if(!value.contains("encrypted_content")||!value["encrypted_content"].is_string()||value["encrypted_content"].get<std::string>().empty())throw ModelProtocolError("Responses reasoning lacks stateless continuation");
        }
        current.final=value;current.done=true;
    }
    void terminal(const Json& event){
        const auto& response=event.at("response");response_identity(response);
        if(response.value("status",Json{})!="completed"||(response.contains("error")&&!response["error"].is_null())||!response.contains("output")||!response["output"].is_array()||response["output"].size()!=items.size())throw ModelProtocolError("Responses turn did not complete");
        for(std::size_t i=0;i<response["output"].size();++i){const auto found=items.find(static_cast<int>(i));if(found==items.end()||!found->second.done||found->second.final!=response["output"][i])throw ModelProtocolError("Responses terminal output differs from completed items");const auto& current=found->second;
            if(current.type=="message")for(const auto& [pos,part]:current.parts){(void)pos;append(part.type=="output_text"?completion.content:completion.refusal,part.value);}
            else if(current.type=="function_call")completion.tool_calls.push_back({current.call_id,current.name,current.arguments});
        }
        if(response.contains("usage")&&!response["usage"].is_null()){
            auto usage=response["usage"];if(!usage.is_object())throw ModelProtocolError("Invalid Responses usage");
            for(const auto* key:{"input_tokens","output_tokens","total_tokens"})if(!usage.contains(key)||!usage[key].is_number_integer()||usage[key]<0)throw ModelProtocolError("Invalid Responses token count");
            for(const auto& [group,key]:{std::pair{"input_tokens_details","cached_tokens"},std::pair{"output_tokens_details","reasoning_tokens"}})if(usage.contains(group)){if(!usage[group].is_object()||(usage[group].contains(key)&&(!usage[group][key].is_number_integer()||usage[group][key]<0)))throw ModelProtocolError("Invalid Responses token details");}
            usage["prompt_tokens"]=usage["input_tokens"];usage["completion_tokens"]=usage["output_tokens"];
            if(usage.contains("input_tokens_details"))usage["prompt_tokens_details"]=usage["input_tokens_details"];
            if(usage.contains("output_tokens_details"))usage["completion_tokens_details"]=usage["output_tokens_details"];
            completion.usage_json=usage.dump();emit("model.usage",usage);
        }
        completion.provider_items_json=response["output"].dump();if(completion.provider_items_json.size()>max_value)throw ModelProtocolError("Responses continuation exceeds limits");
        completion.finish_reason=completion.tool_calls.empty()?"stop":"tool_calls";emit("model.finish",{{"reason",completion.finish_reason}});done=true;emit("model.done",Json::object());
    }
    void chunk(){
        if(done)throw ModelProtocolError("Responses data after terminal event");const auto value=parse(data);if(!value.is_object())throw ModelProtocolError("Invalid Responses event");const auto type=text(value,"type");if(!event_name.empty()&&event_name!=type)throw ModelProtocolError("Responses SSE event type differs from payload");
        if(!value.contains("sequence_number")||!value["sequence_number"].is_number_integer()||value["sequence_number"]<0||value["sequence_number"]<=sequence)throw ModelProtocolError("Invalid Responses event sequence");sequence=value["sequence_number"].get<std::int64_t>();
        if(type=="response.created"){if(created)throw ModelProtocolError("Duplicate Responses creation");const auto& response=value.at("response");response_id=text(response,"id");identity(response_id);created=true;response_identity(response);if(response.value("status",Json{})!="in_progress")throw ModelProtocolError("Responses creation state is unsupported");return;}
        if(!created)throw ModelProtocolError("Responses event before creation");
        if(type=="response.in_progress"){response_identity(value.at("response"));if(value.at("response").value("status",Json{})!="in_progress")throw ModelProtocolError("Invalid Responses progress");return;}
        if(type=="response.failed"||type=="response.incomplete"||type=="error")throw ModelProtocolError("Provider response failed or was incomplete");
        if(type=="response.completed"){terminal(value);return;}
        if(type=="response.output_item.added"){
            const auto pos=index(value,"output_index");const auto& added=value.at("item");Item current;current.id=text(added,"id");identity(current.id);current.type=text(added,"type");if(items.contains(pos)||!ids.insert(current.id).second)throw ModelProtocolError("Duplicate Responses output item");
            if(current.type=="function_call"){current.call_id=text(added,"call_id");identity(current.call_id);current.name=text(added,"name");identity(current.name);if(!calls.insert(current.call_id).second||calls.size()>64)throw ModelProtocolError("Duplicate or excessive Responses calls");current.arguments=text(added,"arguments");if(!current.arguments.empty())throw ModelProtocolError("Responses function starts with unexpected arguments");}
            else if(current.type=="message"){if(added.value("role",Json{})!="assistant"||added.value("content",Json{})!=Json::array())throw ModelProtocolError("Invalid Responses message start");}
            else if(current.type=="reasoning"){if(added.value("summary",Json{})!=Json::array()||(added.contains("content")&&added["content"]!=Json::array()))throw ModelProtocolError("Unsupported Responses reasoning start");}
            else throw ModelProtocolError("Unsupported Responses output item");items.emplace(pos,std::move(current));return;
        }
        auto& current=item(value);
        if(type=="response.output_item.done"){final_item(current,value.at("item"));return;}
        if(type=="response.function_call_arguments.delta"||type=="response.function_call_arguments.done"){
            if(current.type!="function_call"||current.args_done)throw ModelProtocolError("Invalid Responses function lifecycle");
            if(type.ends_with(".delta")){const auto delta=text(value,"delta");append(current.arguments,delta);emit("model.tool_delta",{{"index",index(value,"output_index")},{"id",current.call_id},{"name",current.name},{"arguments",delta}});}
            else {if(text(value,"arguments")!=current.arguments)throw ModelProtocolError("Responses final arguments differ from deltas");current.args_done=true;}return;
        }
        const bool summary=type.starts_with("response.reasoning_summary_");auto& parts=summary?current.summaries:current.parts;
        if((summary&&current.type!="reasoning")||(!summary&&current.type!="message"))throw ModelProtocolError("Responses content belongs to wrong item type");
        const auto pos=index(value,summary?"summary_index":"content_index");
        if(type=="response.content_part.added"||type=="response.reasoning_summary_part.added"){
            const auto& part=value.at("part");const auto kind=text(part,"type");if(parts.contains(pos)||(summary?kind!="summary_text":kind!="output_text"&&kind!="refusal")||text(part,kind=="refusal"?"refusal":"text")!="")throw ModelProtocolError("Invalid Responses content start");parts.emplace(pos,Part{kind});return;
        }
        const auto found=parts.find(pos);if(found==parts.end())throw ModelProtocolError("Responses delta has no content part");auto& part=found->second;
        if(type=="response.output_text.delta"||type=="response.refusal.delta"||type=="response.reasoning_summary_text.delta"){
            const auto kind=summary?"summary_text":type=="response.refusal.delta"?"refusal":"output_text";if(part.type!=kind||part.done)throw ModelProtocolError("Invalid Responses text lifecycle");const auto delta=text(value,"delta");append(part.value,delta);emit(summary?"model.reasoning":part.type=="refusal"?"model.refusal":"model.text",{{"text",delta}});return;
        }
        if(type=="response.output_text.done"||type=="response.refusal.done"||type=="response.reasoning_summary_text.done"){
            const auto kind=summary?"summary_text":type=="response.refusal.done"?"refusal":"output_text";if(part.type!=kind||part.done||text(value,kind==std::string_view("refusal")?"refusal":"text")!=part.value)throw ModelProtocolError("Responses completed text differs from deltas");part.done=true;return;
        }
        if(type=="response.content_part.done"||type=="response.reasoning_summary_part.done"){const auto& final=value.at("part");if(!part.done||text(final,"type")!=part.type||text(final,part.type=="refusal"?"refusal":"text")!=part.value)throw ModelProtocolError("Responses final content differs from stream");return;}
        // Annotation payloads are retained in the completed output item. Only
        // supported text annotations may decorate an existing content part.
        if(type=="response.output_text.annotation.added"&&part.type=="output_text"&&!part.done)return;
        throw ModelProtocolError("Unsupported Responses event");
    }
    void end_line(){
        if(first_line){if(line.starts_with("\xef\xbb\xbf"))line.erase(0,3);first_line=false;}
        if(line.empty()){if(has_data){if(!data.empty())data.pop_back();chunk();data.clear();has_data=false;}event_name.clear();}
        else if(line[0]!=':'){const auto colon=line.find(':');const auto field=line.substr(0,colon);auto value=colon==std::string::npos?std::string_view{}:std::string_view(line).substr(colon+1);if(value.starts_with(' '))value.remove_prefix(1);if(field=="data"){append(data,value);append(data,"\n");has_data=true;}else if(field=="event"){if(value.size()>256)throw ModelProtocolError("Responses event name exceeds limits");event_name=value;}}
        line.clear();
    }
    void feed(std::string_view input){if(failed)throw ModelProtocolError("Responses stream is invalid");if(input.size()>64*1024*1024-bytes)throw ModelProtocolError("Responses stream exceeds limits");bytes+=input.size();for(char byte:input){if(skip_lf){skip_lf=false;if(byte=='\n')continue;}if(byte=='\r'){end_line();skip_lf=true;}else if(byte=='\n')end_line();else append(line,std::string_view(&byte,1),1024*1024);}}
};
ResponsesStream::ResponsesStream(ChatCompletionStream::Sink sink):impl_(std::make_unique<Impl>(std::move(sink))){}
ResponsesStream::~ResponsesStream()=default;
void ResponsesStream::feed(std::string_view bytes){try{impl_->feed(bytes);}catch(const Json::exception&){impl_->failed=true;throw ModelProtocolError("Invalid Responses JSON event");}catch(...){impl_->failed=true;throw;}}
ModelCompletion ResponsesStream::finish(){if(impl_->failed||!impl_->done||!impl_->line.empty()||impl_->has_data)throw ModelProtocolError("Incomplete Responses stream");return impl_->completion;}
}
