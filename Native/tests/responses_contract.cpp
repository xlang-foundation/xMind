#include "agentflow/responses_stream.hpp"
#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>
using namespace agentflow;using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("Responses contract failed");}
template<class F>void rejected(F call){bool failed=false;try{call();}catch(const std::exception&){failed=true;}require(failed);}
std::vector<Json> events(){
    const Json reasoning={{"id","rs_test"},{"type","reasoning"},{"summary",Json::array()},{"encrypted_content","synthetic-opaque-reasoning"}};
    const Json call={{"id","fc_test"},{"type","function_call"},{"call_id","call_test"},{"name","read_file"},{"arguments","{\"path\":\"README.md\"}"},{"status","completed"}};
    auto added_call=call;added_call["arguments"]="";added_call["status"]="in_progress";
    const Json part={{"type","output_text"},{"text","Hello \xf0\x9f\x8c\x8d"},{"annotations",Json::array()}};
    const Json message={{"id","msg_test"},{"type","message"},{"role","assistant"},{"status","completed"},{"content",Json::array({part})}};
    auto added_message=message;added_message["status"]="in_progress";added_message["content"]=Json::array();
    const Json usage={{"input_tokens",12},{"output_tokens",7},{"total_tokens",19},{"input_tokens_details",{{"cached_tokens",4}}},{"output_tokens_details",{{"reasoning_tokens",3}}}};
    const Json completed={{"id","resp_test"},{"model","synthetic"},{"status","completed"},{"output",Json::array({reasoning,call,message})},{"usage",usage}};
    return {
        {{"type","response.created"},{"response",{{"id","resp_test"},{"model","synthetic"},{"status","in_progress"}}}},
        {{"type","response.output_item.added"},{"output_index",0},{"item",{{"id","rs_test"},{"type","reasoning"},{"summary",Json::array()}}}},
        {{"type","response.output_item.done"},{"output_index",0},{"item",reasoning}},
        {{"type","response.output_item.added"},{"output_index",1},{"item",added_call}},
        {{"type","response.function_call_arguments.delta"},{"output_index",1},{"item_id","fc_test"},{"delta","{\"path\":"}},
        {{"type","response.function_call_arguments.delta"},{"output_index",1},{"item_id","fc_test"},{"delta","\"README.md\"}"}},
        {{"type","response.function_call_arguments.done"},{"output_index",1},{"item_id","fc_test"},{"arguments",call["arguments"]}},
        {{"type","response.output_item.done"},{"output_index",1},{"item",call}},
        {{"type","response.output_item.added"},{"output_index",2},{"item",added_message}},
        {{"type","response.content_part.added"},{"output_index",2},{"item_id","msg_test"},{"content_index",0},{"part",{{"type","output_text"},{"text",""}}}},
        {{"type","response.output_text.delta"},{"output_index",2},{"item_id","msg_test"},{"content_index",0},{"delta","Hello "}},
        {{"type","response.output_text.delta"},{"output_index",2},{"item_id","msg_test"},{"content_index",0},{"delta","\xf0\x9f\x8c\x8d"}},
        {{"type","response.output_text.done"},{"output_index",2},{"item_id","msg_test"},{"content_index",0},{"text","Hello \xf0\x9f\x8c\x8d"}},
        {{"type","response.content_part.done"},{"output_index",2},{"item_id","msg_test"},{"content_index",0},{"part",part}},
        {{"type","response.output_item.done"},{"output_index",2},{"item",message}},
        {{"type","response.completed"},{"response",completed}}
    };
}
std::string wire(std::vector<Json> values){std::string result="\xef\xbb\xbf: Synthetic fixture\r\n\r\n";int sequence=0;for(auto& value:values){value["sequence_number"]=sequence++;result+="event: "+value["type"].get<std::string>()+"\r\ndata: "+value.dump()+"\r\n\r\n";}return result;}
ModelCompletion decode(const std::string& bytes,std::size_t fragment=1,std::vector<ModelEvent>* observed=nullptr){ResponsesStream stream([&](const ModelEvent& event){if(observed)observed->push_back(event);});for(std::size_t i=0;i<bytes.size();i+=fragment)stream.feed(std::string_view(bytes).substr(i,fragment));return stream.finish();}
Json mismatch_diagnostic(std::vector<Json> values,std::size_t fragment=1){
    std::vector<ModelEvent> observed;ResponsesStream stream([&](const ModelEvent& event){observed.push_back(event);});const auto bytes=wire(std::move(values));bool failed=false;
    try{for(std::size_t i=0;i<bytes.size();i+=fragment)stream.feed(std::string_view(bytes).substr(i,fragment));stream.finish();}
    catch(const ModelProtocolError& error){require(model_protocol_diagnostic(error)=="responses_terminal_mismatch");failed=true;}require(failed);
    rejected([&]{stream.feed("data: {}\n\n");});rejected([&]{stream.finish();});
    Json diagnostic;std::size_t count=0;for(const auto& event:observed){require(event.kind!="model.usage"&&event.kind!="model.finish"&&event.kind!="model.done");if(event.kind=="model.protocol_diagnostic"){++count;require(event.json.size()<=4096);diagnostic=Json::parse(event.json);}}
    require(count==1&&diagnostic.size()==9&&diagnostic["code"]=="responses_terminal_mismatch"&&diagnostic["output_index"]>=0&&diagnostic["output_index"]<1024);
    require(diagnostic["item_present"].is_boolean()&&diagnostic["item_done"].is_boolean()&&diagnostic["unlisted_fields_equal"].is_boolean());
    const std::vector<std::string> labels={"id","type","status","call_id","name","arguments","role","content","summary","encrypted_content","channel","phase"};
    const std::set<std::string> types={"absent","null","object","array","string","boolean","integer","unsigned_integer","number","unsupported"};
    require(diagnostic["fields"].is_array()&&diagnostic["fields"].size()==labels.size()&&diagnostic["changed_fields"].is_array());
    Json changes=Json::array();for(std::size_t i=0;i<labels.size();++i){const auto& field=diagnostic["fields"][i];require(field.size()==6&&field["field"]==labels[i]);
        for(const auto* flag:{"completed_present","terminal_present","equal"})require(field[flag].is_boolean());
        require(types.contains(field["completed_type"].get<std::string>())&&types.contains(field["terminal_type"].get<std::string>()));if(!field["equal"].get<bool>())changes.push_back(labels[i]);
    }require(changes==diagnostic["changed_fields"]);require(types.contains(diagnostic["completed_snapshot_type"].get<std::string>())&&types.contains(diagnostic["terminal_snapshot_type"].get<std::string>()));
    return diagnostic;
}
const Json& diagnostic_field(const Json& diagnostic,const std::string& name){for(const auto& field:diagnostic.at("fields"))if(field.at("field")==name)return field;throw std::runtime_error("Missing fixed diagnostic field");}
void terminal_diagnostics_contract(const std::vector<Json>& fixture){
    std::vector<ModelEvent> unchanged;decode(wire(fixture),7,&unchanged);for(const auto& event:unchanged)require(event.kind!="model.protocol_diagnostic");
    // Build separate completed-item and terminal snapshots. Even benign-looking
    // metadata or opaque changes remain rejected; diagnosis is not normalization.
    auto optional=fixture;optional[7]["item"].erase("status");const auto optional_diagnostic=mismatch_diagnostic(optional,7);
    require(optional_diagnostic["output_index"]==1&&optional_diagnostic["item_present"]==true&&optional_diagnostic["item_done"]==true&&optional_diagnostic["changed_fields"]==Json::array({"status"})&&optional_diagnostic["unlisted_fields_equal"]==true);
    const auto& status=diagnostic_field(optional_diagnostic,"status");require(status["completed_present"]==false&&status["completed_type"]=="absent"&&status["terminal_present"]==true&&status["terminal_type"]=="string"&&status["equal"]==false);
    require(diagnostic_field(optional_diagnostic,"id")["equal"]==true&&diagnostic_field(optional_diagnostic,"name")["equal"]==true&&diagnostic_field(optional_diagnostic,"arguments")["equal"]==true);
    auto opaque=fixture;opaque[2]["item"]["encrypted_content"]="SYNTHETIC_OPAQUE_ITEM_SECRET";opaque.back()["response"]["output"][0]["encrypted_content"]="SYNTHETIC_OPAQUE_TERMINAL_SECRET";
    opaque.back()["response"]["output"][1]["arguments"]="{\"private\":\"SYNTHETIC_TERMINAL_ARGUMENT_SECRET\"}";
    const auto opaque_diagnostic=mismatch_diagnostic(opaque,129);require(opaque_diagnostic["output_index"]==0&&opaque_diagnostic["changed_fields"]==Json::array({"encrypted_content"}));
    for(const auto* marker:{"SYNTHETIC_OPAQUE_ITEM_SECRET","SYNTHETIC_OPAQUE_TERMINAL_SECRET","SYNTHETIC_TERMINAL_ARGUMENT_SECRET","rs_test","call_test","read_file"})require(opaque_diagnostic.dump().find(marker)==std::string::npos);
    auto unlisted=fixture;unlisted[7]["item"]["SYNTHETIC_PRIVATE_MEMBER_NAME"]="SYNTHETIC_UNLISTED_ITEM_VALUE";unlisted.back()["response"]["output"][1]["SYNTHETIC_PRIVATE_MEMBER_NAME"]="SYNTHETIC_UNLISTED_TERMINAL_VALUE";
    const auto unlisted_diagnostic=mismatch_diagnostic(unlisted);require(unlisted_diagnostic["output_index"]==1&&unlisted_diagnostic["changed_fields"]==Json::array()&&unlisted_diagnostic["unlisted_fields_equal"]==false);
    for(const auto* marker:{"SYNTHETIC_PRIVATE_MEMBER_NAME","SYNTHETIC_UNLISTED_ITEM_VALUE","SYNTHETIC_UNLISTED_TERMINAL_VALUE","README.md","read_file","fc_test"})require(unlisted_diagnostic.dump().find(marker)==std::string::npos);
    auto unlisted_added=fixture;unlisted_added.back()["response"]["output"][1]["SYNTHETIC_PRIVATE_ADDED_NAME"]="SYNTHETIC_PRIVATE_ADDED_VALUE";
    const auto added_diagnostic=mismatch_diagnostic(unlisted_added);require(added_diagnostic["changed_fields"]==Json::array()&&added_diagnostic["unlisted_fields_equal"]==false&&added_diagnostic.dump().find("SYNTHETIC_PRIVATE")==std::string::npos);
    for(const auto& [value,type]:std::vector<std::pair<Json,std::string>>{{nullptr,"null"},{Json::object({{"SYNTHETIC_NESTED_NAME","SYNTHETIC_NESTED_VALUE"}}),"object"},{Json::array({"SYNTHETIC_ARRAY_VALUE"}),"array"},{"SYNTHETIC_CHANNEL_VALUE","string"},{true,"boolean"},{-7,"integer"},{Json::number_unsigned_t{7},"unsigned_integer"},{1.25,"number"}}){
        auto typed=fixture;typed.back()["response"]["output"][1]["channel"]=value;const auto diagnostic=mismatch_diagnostic(typed,2);
        require(diagnostic["changed_fields"]==Json::array({"channel"})&&diagnostic_field(diagnostic,"channel")["completed_type"]=="absent"&&diagnostic_field(diagnostic,"channel")["terminal_type"]==type&&diagnostic.dump().find("SYNTHETIC_")==std::string::npos);
    }
    for(const auto* field:{"id","type","call_id","name","arguments"}){auto altered=fixture;altered.back()["response"]["output"][1][field]="SYNTHETIC_EXECUTION_FIELD_CHANGE";const auto diagnostic=mismatch_diagnostic(altered);require(diagnostic["changed_fields"]==Json::array({field})&&diagnostic.dump().find("SYNTHETIC_EXECUTION_FIELD_CHANGE")==std::string::npos);}
    for(const auto& [position,field]:std::vector<std::pair<int,const char*>>{{0,"summary"},{2,"content"}}){auto altered=fixture;altered.back()["response"]["output"][position][field]=Json::array({"SYNTHETIC_CONTENT_CHANGE"});const auto diagnostic=mismatch_diagnostic(altered);require(diagnostic["changed_fields"]==Json::array({field})&&diagnostic.dump().find("SYNTHETIC_CONTENT_CHANGE")==std::string::npos);}
    auto unfinished=fixture;unfinished.erase(unfinished.begin()+7);const auto unfinished_diagnostic=mismatch_diagnostic(unfinished);
    require(unfinished_diagnostic["output_index"]==1&&unfinished_diagnostic["item_present"]==true&&unfinished_diagnostic["item_done"]==false&&unfinished_diagnostic["completed_snapshot_type"]=="absent");
    auto sparse=fixture;for(auto& event:sparse)if(event.contains("output_index")&&event["output_index"]==2)event["output_index"]=3;const auto missing_diagnostic=mismatch_diagnostic(sparse);
    require(missing_diagnostic["output_index"]==2&&missing_diagnostic["item_present"]==false&&missing_diagnostic["item_done"]==false&&missing_diagnostic["completed_snapshot_type"]=="absent");
    auto scalar=fixture;scalar.back()["response"]["output"][1]=nullptr;const auto scalar_diagnostic=mismatch_diagnostic(scalar);
    require(scalar_diagnostic["output_index"]==1&&scalar_diagnostic["completed_snapshot_type"]=="object"&&scalar_diagnostic["terminal_snapshot_type"]=="null"&&scalar_diagnostic["unlisted_fields_equal"]==false);
}
int main(){try{
    const auto fixture=events();const auto bytes=wire(fixture);const auto result=decode(bytes);require(result.content=="Hello \xf0\x9f\x8c\x8d"&&result.tool_calls.size()==1&&result.finish_reason=="tool_calls");require(result.tool_calls[0].id=="call_test");const auto usage=Json::parse(result.usage_json);require(usage["prompt_tokens"]==12&&usage["completion_tokens"]==7&&usage["prompt_tokens_details"]["cached_tokens"]==4&&usage["completion_tokens_details"]["reasoning_tokens"]==3);
    for(std::size_t size:{2,7,129,4096})require(decode(bytes,size).provider_items_json==result.provider_items_json);
    terminal_diagnostics_contract(fixture);
    auto summarized=fixture;const Json summary={{"type","summary_text"},{"text","Synthetic reasoning summary"}};
    summarized.insert(summarized.begin()+2,{
        {{"type","response.reasoning_summary_part.added"},{"output_index",0},{"item_id","rs_test"},{"summary_index",0},{"part",{{"type","summary_text"},{"text",""}}}},
        {{"type","response.reasoning_summary_text.delta"},{"output_index",0},{"item_id","rs_test"},{"summary_index",0},{"delta","Synthetic reasoning summary"}},
        {{"type","response.reasoning_summary_text.done"},{"output_index",0},{"item_id","rs_test"},{"summary_index",0},{"text","Synthetic reasoning summary"}},
        {{"type","response.reasoning_summary_part.done"},{"output_index",0},{"item_id","rs_test"},{"summary_index",0},{"part",summary}}
    });summarized[6]["item"]["summary"]=Json::array({summary});summarized.back()["response"]["output"][0]["summary"]=Json::array({summary});require(decode(wire(summarized)).content=="Hello \xf0\x9f\x8c\x8d");
    auto parallel=fixture;auto second_added=fixture[3],second_delta=fixture[4],second_done=fixture[6],second_item=fixture[7];const std::string second_args="{}";
    for(auto* value:{&second_added,&second_delta,&second_done,&second_item})(*value)["output_index"]=3;
    second_added["item"]["id"]="fc_parallel";second_added["item"]["call_id"]="call_parallel";second_delta["item_id"]="fc_parallel";second_delta["delta"]=second_args;second_done["item_id"]="fc_parallel";second_done["arguments"]=second_args;second_item["item"]["id"]="fc_parallel";second_item["item"]["call_id"]="call_parallel";second_item["item"]["arguments"]=second_args;
    parallel.insert(parallel.end()-1,{second_added,second_delta,second_done,second_item});parallel.back()["response"]["output"].push_back(second_item["item"]);require(decode(wire(parallel)).tool_calls.size()==2);
    auto bad=fixture;bad.pop_back();rejected([&]{decode(wire(bad));});
    bad=fixture;bad[5]["item_id"]="other";rejected([&]{decode(wire(bad));});
    bad=fixture;bad[12]["text"]="changed";rejected([&]{decode(wire(bad));});
    bad=fixture;bad.back()["response"]["output"][1]["arguments"]="{}";rejected([&]{decode(wire(bad));});
    bad=fixture;bad[2]["item"].erase("encrypted_content");rejected([&]{decode(wire(bad));});
    bad=fixture;bad[3]["item"]["type"]="computer_call";rejected([&]{decode(wire(bad));});
    bad=fixture;bad.back()["response"]["usage"]["input_tokens"]=-1;rejected([&]{decode(wire(bad));});
    bad=fixture;bad.back()["type"]="response.incomplete";rejected([&]{decode(wire(bad));});
    rejected([&]{decode(bytes+"data: {}\n\n");});rejected([&]{decode("data: {\"type\":\"response.created\",\"type\":\"error\",\"sequence_number\":0}\n\n");});
    ChatProviderConfig config{"https://api.openai.com/v1/responses","synthetic",Capability::supported,Capability::supported,Capability::supported};config.wire=ProviderWire::responses;
    ModelRequest request;request.include_usage=true;request.max_output_tokens=100;request.tools.push_back({"read_file","Read a file",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"});request.messages.push_back({MessageRole::user,"Read"});request.messages.push_back({MessageRole::assistant,result.content,result.tool_calls,{},result.refusal,result.provider_items_json});request.messages.push_back({MessageRole::tool,"Actual result",{},"call_test"});
    const auto body=Json::parse(serialize_responses_request(config,request));require(body["store"]==false&&body["stream"]==true&&body["max_output_tokens"]==100&&!body.contains("messages")&&!body.contains("stream_options"));require(body["tools"][0]["name"]=="read_file"&&body["tools"][0]["strict"]==false);require(body["input"][1]["encrypted_content"]=="synthetic-opaque-reasoning"&&body["input"].back()["call_id"]=="call_test");
    require(!body.contains("reasoning")&&!body.contains("reasoning_effort"));
    config.reasoning_effort=ReasoningEffort::high;rejected([&]{serialize_responses_request(config,request);});config.reasoning=Capability::supported;
    const auto reasoningBody=Json::parse(serialize_responses_request(config,request));require(reasoningBody["reasoning"]["effort"]=="high"&&!reasoningBody.contains("reasoning_effort"));config.reasoning_effort.reset();
    rejected([&]{serialize_chat_request(config,request);});auto changed=request;changed.messages[1].content="changed";rejected([&]{serialize_responses_request(config,changed);});changed=request;changed.messages.pop_back();rejected([&]{serialize_responses_request(config,changed);});
    ModelRequest authored;authored.messages={{MessageRole::user,"Question"},{MessageRole::assistant,"Previous plain text"},{MessageRole::user,"Next"}};const auto authored_body=Json::parse(serialize_responses_request(config,authored));require(authored_body["input"][1]["role"]=="assistant"&&authored_body["input"][1]["content"][0]["type"]=="input_text");authored.messages[1].refusal="Refusal without provider items";rejected([&]{serialize_responses_request(config,authored);});
    require(model_protocol_diagnostic(ModelProtocolError("Responses final arguments differ from deltas"))=="responses_arguments_mismatch");
    require(model_protocol_diagnostic(ModelProtocolError("Incomplete Responses stream"))=="responses_stream_incomplete");
    require(model_protocol_diagnostic(ModelProtocolError("Provider response failed or was incomplete"))=="responses_provider_incomplete");
    require(model_protocol_diagnostic(ModelProtocolError("Invalid Responses JSON event"))=="responses_json_invalid");
    require(model_protocol_diagnostic(ModelProtocolError("private synthetic provider payload"))=="");
    std::cout<<"Native Responses request and SSE contract passed byte fragmentation, output identity/lifecycle validation, strict terminal consistency, bounded value-free first-mismatch structural diagnostics, usage normalization, stateless reasoning/tool continuation, safe invariant diagnostics and malformed/incomplete rejection. All model output is synthetic.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
