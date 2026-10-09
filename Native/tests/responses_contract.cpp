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
    // Build separate completed-item and terminal snapshots. Metadata changes
    // remain rejected, including after an allowed reasoning opaque difference.
    auto optional=fixture;optional[7]["item"].erase("status");const auto optional_diagnostic=mismatch_diagnostic(optional,7);
    require(optional_diagnostic["output_index"]==1&&optional_diagnostic["item_present"]==true&&optional_diagnostic["item_done"]==true&&optional_diagnostic["changed_fields"]==Json::array({"status"})&&optional_diagnostic["unlisted_fields_equal"]==true);
    const auto& status=diagnostic_field(optional_diagnostic,"status");require(status["completed_present"]==false&&status["completed_type"]=="absent"&&status["terminal_present"]==true&&status["terminal_type"]=="string"&&status["equal"]==false);
    require(diagnostic_field(optional_diagnostic,"id")["equal"]==true&&diagnostic_field(optional_diagnostic,"name")["equal"]==true&&diagnostic_field(optional_diagnostic,"arguments")["equal"]==true);
    auto opaque=fixture;opaque[2]["item"]["encrypted_content"]="SYNTHETIC_OPAQUE_ITEM_SECRET";opaque.back()["response"]["output"][0]["encrypted_content"]="SYNTHETIC_OPAQUE_TERMINAL_SECRET";
    opaque.back()["response"]["output"][1]["arguments"]="{\"private\":\"SYNTHETIC_TERMINAL_ARGUMENT_SECRET\"}";
    const auto opaque_diagnostic=mismatch_diagnostic(opaque,129);require(opaque_diagnostic["output_index"]==1&&opaque_diagnostic["changed_fields"]==Json::array({"arguments"}));
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
void reasoning_continuation_contract(const std::vector<Json>& fixture){
    // These snapshots are independently authored, with unchanged known and
    // unlisted members. The opaque strings are explicitly synthetic and have
    // unrelated bytes; no normalization or terminal replacement is permitted.
    std::string done_cipher="SYNTHETIC_DONE_CIPHER_\"\\\n\t\xf0\x9f\x8c\x8d";done_cipher.push_back('\0');done_cipher+="tail";
    const Json completed={{"id","rs_test"},{"type","reasoning"},{"status","completed"},{"summary",Json::array()},{"content",Json::array()},
        {"encrypted_content",done_cipher},{"channel","analysis"},{"phase","commentary"},{"SYNTHETIC_RECEIPT_METADATA",{{"flag",true},{"values",Json::array({nullptr,7,"synthetic"})}}}};
    const Json terminal={{"id","rs_test"},{"type","reasoning"},{"status","completed"},{"summary",Json::array()},{"content",Json::array()},
        {"encrypted_content","SYNTHETIC_TERMINAL_CIPHER_UNRELATED"},{"channel","analysis"},{"phase","commentary"},{"SYNTHETIC_RECEIPT_METADATA",{{"flag",true},{"values",Json::array({nullptr,7,"synthetic"})}}}};
    auto variation=fixture;variation[2]["item"]=completed;variation.back()["response"]["output"][0]=terminal;
    const Json expected=Json::array({completed,fixture[7]["item"],fixture[14]["item"]});
    for(std::size_t fragment:{1,2,7,129,4096}){
        std::vector<ModelEvent> observed;const auto result=decode(wire(variation),fragment,&observed);
        require(result.content=="Hello \xf0\x9f\x8c\x8d"&&result.tool_calls.size()==1&&result.tool_calls[0].arguments_json==fixture[7]["item"]["arguments"].get<std::string>()&&result.finish_reason=="tool_calls");
        require(result.provider_items_json==expected.dump());const auto receipt=Json::parse(result.provider_items_json);
        require(receipt[0].dump()==completed.dump()&&receipt[0]["encrypted_content"].get<std::string>()==done_cipher&&receipt[0]!=terminal);
        require(result.provider_items_json.find("SYNTHETIC_TERMINAL_CIPHER_UNRELATED")==std::string::npos);
        std::size_t usage=0,finish=0,done=0;for(const auto& event:observed){require(event.kind!="model.protocol_diagnostic");usage+=event.kind=="model.usage";finish+=event.kind=="model.finish";done+=event.kind=="model.done";}require(usage==1&&finish==1&&done==1);
        // A serialized JSON roundtrip and the next request retain the entire
        // completed receipt, including escaped opaque bytes and unknown metadata.
        ChatProviderConfig config{"https://api.openai.com/v1/responses","synthetic",Capability::supported,Capability::supported,Capability::supported};config.wire=ProviderWire::responses;
        ModelRequest request;request.messages.push_back({MessageRole::user,"Read"});request.messages.push_back({MessageRole::assistant,result.content,result.tool_calls,{},result.refusal,receipt.dump()});request.messages.push_back({MessageRole::tool,"Actual result",{},"call_test"});
        const auto request_body=Json::parse(serialize_responses_request(config,request));require(request_body["store"]==false&&request_body["include"]==Json::array({"reasoning.encrypted_content"}));
        for(std::size_t i=0;i<expected.size();++i)require(request_body["input"][i+1].dump()==expected[i].dump());
    }
    // The allowed opaque difference cannot hide any other field change.
    for(const auto& [field,value]:std::vector<std::pair<const char*,Json>>{{"id","SYNTHETIC_CHANGED_ID"},{"type","message"},{"status","in_progress"},{"summary",Json::array({"SYNTHETIC_CHANGED_SUMMARY"})},{"content",Json::array({"SYNTHETIC_CHANGED_CONTENT"})},{"channel","final"},{"phase","final"}}){
        auto altered=variation;altered.back()["response"]["output"][0][field]=value;const auto diagnostic=mismatch_diagnostic(altered,7);
        require(diagnostic["output_index"]==0&&diagnostic["changed_fields"].size()==2&&diagnostic_field(diagnostic,field)["equal"]==false&&diagnostic_field(diagnostic,"encrypted_content")["equal"]==false);
        require(diagnostic.dump().find("SYNTHETIC_")==std::string::npos);
    }
    for(const auto& invalid:std::vector<Json>{nullptr,"",false,7,1.25,Json::array(),Json::object()}){
        auto altered=variation;altered.back()["response"]["output"][0]["encrypted_content"]=invalid;
        const auto diagnostic=mismatch_diagnostic(altered);require(diagnostic["output_index"]==0&&diagnostic["changed_fields"]==Json::array({"encrypted_content"}));
        altered=variation;altered[2]["item"]["encrypted_content"]=invalid;std::vector<ModelEvent> observed;bool failed=false;
        try{decode(wire(altered),7,&observed);}catch(const ModelProtocolError& error){require(std::string(error.what())=="Responses reasoning lacks stateless continuation");failed=true;}require(failed);
        for(const auto& event:observed)require(event.kind!="model.protocol_diagnostic"&&event.kind!="model.usage"&&event.kind!="model.finish"&&event.kind!="model.done");
    }
    auto missing=variation;missing.back()["response"]["output"][0].erase("encrypted_content");require(mismatch_diagnostic(missing)["changed_fields"]==Json::array({"encrypted_content"}));
    for(const auto* field:{"status","summary","content","channel","phase","SYNTHETIC_RECEIPT_METADATA"}){
        auto removed=variation;removed.back()["response"]["output"][0].erase(field);const auto diagnostic=mismatch_diagnostic(removed);require(diagnostic["output_index"]==0&&diagnostic_field(diagnostic,"encrypted_content")["equal"]==false);
    }
    for(int change=0;change<3;++change){
        auto altered=variation;auto& reasoning=altered.back()["response"]["output"][0];
        if(change==0)reasoning["SYNTHETIC_RECEIPT_METADATA"]["flag"]=false;
        else if(change==1)reasoning["SYNTHETIC_EXTRA_FIELD"]="SYNTHETIC_EXTRA_VALUE";
        else {reasoning.erase("SYNTHETIC_RECEIPT_METADATA");reasoning["SYNTHETIC_REPLACEMENT_FIELD"]="SYNTHETIC_REPLACEMENT_VALUE";}
        const auto diagnostic=mismatch_diagnostic(altered);require(diagnostic["changed_fields"]==Json::array({"encrypted_content"})&&diagnostic["unlisted_fields_equal"]==false&&diagnostic.dump().find("SYNTHETIC_")==std::string::npos);
    }
    for(const auto& [done_index,output_index]:std::vector<std::pair<int,int>>{{7,1},{14,2}}){
        auto altered=fixture;altered[done_index]["item"]["encrypted_content"]="SYNTHETIC_NON_REASONING_DONE";altered.back()["response"]["output"][output_index]["encrypted_content"]="SYNTHETIC_NON_REASONING_TERMINAL";
        const auto diagnostic=mismatch_diagnostic(altered);require(diagnostic["output_index"]==output_index&&diagnostic["changed_fields"]==Json::array({"encrypted_content"})&&diagnostic.dump().find("SYNTHETIC_")==std::string::npos);
        altered=fixture;altered.back()["response"]["output"][output_index]["encrypted_content"]="SYNTHETIC_NON_REASONING_EXTRA";require(mismatch_diagnostic(altered)["changed_fields"]==Json::array({"encrypted_content"}));
    }
    // Each done event fits existing SSE limits, but their ordered receipt is
    // over 4MiB while the terminal receipt is small. It must still fail closed.
    std::vector<Json> oversized={fixture[0]};Json terminal_items=Json::array();
    for(int i=0;i<6;++i){
        const auto id="rs_synthetic_large_"+std::to_string(i);const Json added={{"id",id},{"type","reasoning"},{"summary",Json::array()}};
        auto completed_large=added;completed_large["encrypted_content"]=std::string(700*1024,'x');auto terminal_small=added;terminal_small["encrypted_content"]="synthetic-small";
        oversized.push_back({{"type","response.output_item.added"},{"output_index",i},{"item",added}});oversized.push_back({{"type","response.output_item.done"},{"output_index",i},{"item",completed_large}});terminal_items.push_back(terminal_small);
    }
    oversized.push_back({{"type","response.completed"},{"response",{{"id","resp_test"},{"model","synthetic"},{"status","completed"},{"output",terminal_items},{"usage",fixture.back()["response"]["usage"]}}}});
    std::vector<ModelEvent> oversize_observed;bool exceeded=false;try{decode(wire(oversized),4096,&oversize_observed);}catch(const ModelProtocolError& error){require(std::string(error.what())=="Responses continuation exceeds limits");exceeded=true;}require(exceeded);
    for(const auto& event:oversize_observed)require(event.kind!="model.protocol_diagnostic"&&event.kind!="model.usage"&&event.kind!="model.finish"&&event.kind!="model.done");
}
void plain_history_contract(const ChatProviderConfig& config){
    // Independently authored historical text, not invented provider receipts.
    const std::string legacy="Synthetic legacy answer with quote \" and slash \\\n"+std::string(3956,'x')+" \xf0\x9f\x8c\x8d";
    const std::string graph=R"({"source":"graph_join","outputs":{"read":{"content":"Synthetic graph bytes\n"}}})";
    ModelRequest request;request.messages={{MessageRole::system,"Synthetic system instructions."},{MessageRole::developer,"Synthetic developer instructions."},{MessageRole::user,"Original question."},{MessageRole::assistant,legacy},{MessageRole::assistant,graph},{MessageRole::assistant,""},{MessageRole::user,"Next question."}};
    const auto body=Json::parse(serialize_responses_request(config,request));const auto& input=body.at("input");require(input.size()==request.messages.size());
    for(std::size_t i=0;i<input.size();++i){const auto& item=input[i];require(!item.contains("id")&&!item.contains("status")&&item.size()==3&&item["type"]=="message"&&item["content"].size()==1&&item["content"][0].size()==2&&item["content"][0]["text"]==request.messages[i].content);
        require(item["content"][0]["type"]==(i>=3&&i<=5?"output_text":"input_text"));}
    require(input[3]["role"]=="assistant"&&input[4]["role"]=="assistant"&&input[5]["role"]=="assistant");
    require(legacy.size()>3956&&input[3]["content"][0]["text"].get<std::string>()==legacy&&input[4]["content"][0]["text"].get<std::string>()==graph);
    const std::string arguments=R"({"pa\u0074h":"README.md","decimal":1.00000000000000000001})";
    request.messages={{MessageRole::user,"Read"},{MessageRole::assistant,"Synthetic plain tool preface.",{{"plain_call","read_file",arguments}}},{MessageRole::tool,"Synthetic actual-boundary result",{},"plain_call"}};
    const auto tool=Json::parse(serialize_responses_request(config,request)).at("input");require(tool.size()==4&&tool[1]["content"][0]["type"]=="output_text"&&!tool[1].contains("id")&&tool[2]["type"]=="function_call"&&tool[2]["arguments"]==arguments&&tool[3]["call_id"]=="plain_call");
    request.messages[1].content.clear();const auto empty=Json::parse(serialize_responses_request(config,request)).at("input");require(empty.size()==3&&empty[1]["type"]=="function_call"&&empty[1]["arguments"]==arguments);
}
void provider_failure_contract(){
    const Json created={{"type","response.created"},{"response",{{"id","resp_failure"},{"model","synthetic"},{"status","in_progress"}}}};
    const Json usage={{"input_tokens",17},{"output_tokens",8},{"total_tokens",25},{"input_tokens_details",{{"cached_tokens",5},{"cache_write_tokens",2},{"private","DO_NOT_ECHO"}}},{"output_tokens_details",{{"reasoning_tokens",3}}},{"private","DO_NOT_ECHO"}};
    for(const auto* type:{"response.incomplete","response.failed","error"})for(const auto* reason:{"max_output_tokens","content_filter","server_error","rate_limit_exceeded","DO_NOT_ECHO"}){
        const bool incomplete=std::string_view(type)=="response.incomplete";
        Json response={{"id","resp_failure"},{"model","synthetic"},{"status",incomplete?"incomplete":"failed"},{"usage",usage},{"output","DO_NOT_ECHO"}};
        response[incomplete?"incomplete_details":"error"]={{incomplete?"reason":"code",reason},{"message","DO_NOT_ECHO"}};
        const Json failure=std::string_view(type)=="error"?Json{{"type",type},{"code",reason},{"message","DO_NOT_ECHO"}}:Json{{"type",type},{"response",response}};
        std::vector<ModelEvent> observed;bool failed=false;std::string code;
        try{decode(wire({created,failure}),1,&observed);}catch(const ModelProtocolError& error){code=model_protocol_diagnostic(error);failed=true;}require(failed);
        const std::string expected=incomplete&&std::string_view(reason)=="max_output_tokens"?"responses_output_token_limit"
            :incomplete&&std::string_view(reason)=="content_filter"?"responses_content_filter"
            :!incomplete&&std::string_view(reason)=="server_error"?"responses_server_error"
            :!incomplete&&std::string_view(reason)=="rate_limit_exceeded"?"responses_rate_limit":"responses_provider_incomplete";
        require(code==expected&&observed.size()==(std::string_view(type)=="error"?1:2));
        require(observed[0].kind=="model.protocol_diagnostic");const auto diagnostic=Json::parse(observed[0].json);
        require(diagnostic.size()==4&&diagnostic["code"]=="responses_provider_incomplete"&&diagnostic["event"]==type&&diagnostic["usage_state"]==(std::string_view(type)=="error"?"absent":"supplied"));
        for(const auto& event:observed){require(event.json.find("DO_NOT_ECHO")==std::string::npos);require(event.kind!="model.done"&&event.kind!="model.finish");}
        if(observed.size()==2){const auto actual=Json::parse(observed[1].json);require(observed[1].kind=="model.usage"&&actual["prompt_tokens"]==17&&actual["completion_tokens"]==8&&actual["total_tokens"]==25&&actual["prompt_tokens_details"]["cache_write_tokens"]==2&&actual["completion_tokens_details"]["reasoning_tokens"]==3);}
    }
    Json terminal={{"type","response.incomplete"},{"response",{{"id","resp_failure"},{"model","synthetic"},{"status","incomplete"},{"incomplete_details",{{"reason","max_output_tokens"}}},{"usage",usage}}}};
    for(const auto& invalid:std::vector<Json>{Json(nullptr),Json::object(),Json{{"input_tokens",-1},{"output_tokens",8},{"total_tokens",7}},Json{{"input_tokens",17},{"output_tokens",8},{"total_tokens",24}},Json{{"input_tokens",17},{"output_tokens",8},{"total_tokens",25},{"output_tokens_details",{{"reasoning_tokens",9}}}}}){
        auto altered=terminal;altered["response"]["usage"]=invalid;std::vector<ModelEvent> observed;rejected([&]{decode(wire({created,altered}),7,&observed);});require(observed.size()==1&&Json::parse(observed[0].json)["usage_state"]==(invalid.is_null()?"unavailable":"invalid"));
    }
    const Json added={{"type","response.output_item.added"},{"output_index",0},{"item",{{"id","fc_partial"},{"type","function_call"},{"call_id","call_partial"},{"name","edit_file"},{"arguments",""}}}};
    const Json delta={{"type","response.function_call_arguments.delta"},{"output_index",0},{"item_id","fc_partial"},{"delta","{\"path\":"}};
    const Json done={{"type","response.output_item.done"},{"output_index",0},{"item",{{"id","fc_partial"},{"type","function_call"},{"status","incomplete"},{"arguments","{\"path\":"}}}};
    std::vector<ModelEvent> observed;bool classified=false;try{decode(wire({created,added,delta,done,terminal}),7,&observed);}catch(const ModelProtocolError& error){classified=model_protocol_diagnostic(error)=="responses_output_token_limit";}require(classified&&observed.size()==3&&observed[0].kind=="model.tool_delta"&&observed[1].kind=="model.protocol_diagnostic"&&observed[2].kind=="model.usage");
    auto false_success=terminal;false_success["type"]="response.completed";false_success["response"]["status"]="completed";false_success["response"]["output"]=Json::array({done["item"]});observed.clear();rejected([&]{decode(wire({created,added,delta,done,false_success}),7,&observed);});require(observed.size()==1);
    for(const auto* field:{"id","model","status"}){auto altered=terminal;altered["response"][field]="wrong";observed.clear();rejected([&]{decode(wire({created,altered}),7,&observed);});require(observed.empty());}
}
int main(){try{
    const auto fixture=events();const auto bytes=wire(fixture);const auto result=decode(bytes);require(result.content=="Hello \xf0\x9f\x8c\x8d"&&result.tool_calls.size()==1&&result.finish_reason=="tool_calls");require(result.tool_calls[0].id=="call_test");const auto usage=Json::parse(result.usage_json);require(usage["prompt_tokens"]==12&&usage["completion_tokens"]==7&&usage["prompt_tokens_details"]["cached_tokens"]==4&&usage["completion_tokens_details"]["reasoning_tokens"]==3);
    for(std::size_t size:{2,7,129,4096})require(decode(bytes,size).provider_items_json==result.provider_items_json);
    terminal_diagnostics_contract(fixture);
    reasoning_continuation_contract(fixture);
    auto summarized=fixture;const Json summary={{"type","summary_text"},{"text","Synthetic reasoning summary"}};
    summarized.insert(summarized.begin()+2,{
        {{"type","response.reasoning_summary_part.added"},{"output_index",0},{"item_id","rs_test"},{"summary_index",0},{"part",{{"type","summary_text"},{"text",""}}}},
        {{"type","response.reasoning_summary_text.delta"},{"output_index",0},{"item_id","rs_test"},{"summary_index",0},{"delta","Synthetic reasoning summary"}},
        {{"type","response.reasoning_summary_text.done"},{"output_index",0},{"item_id","rs_test"},{"summary_index",0},{"text","Synthetic reasoning summary"}},
        {{"type","response.reasoning_summary_part.done"},{"output_index",0},{"item_id","rs_test"},{"summary_index",0},{"part",summary}}
    });summarized[6]["item"]["summary"]=Json::array({summary});summarized.back()["response"]["output"][0]["summary"]=Json::array({summary});summarized.back()["response"]["output"][0]["encrypted_content"]="synthetic-different-terminal-with-summary";const auto summarized_result=decode(wire(summarized));require(summarized_result.content=="Hello \xf0\x9f\x8c\x8d"&&Json::parse(summarized_result.provider_items_json)[0].dump()==summarized[6]["item"].dump());
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
    plain_history_contract(config);
    provider_failure_contract();
    ModelRequest authored;authored.messages={{MessageRole::user,"Question"},{MessageRole::assistant,"Previous plain text"},{MessageRole::user,"Next"}};const auto authored_body=Json::parse(serialize_responses_request(config,authored));require(authored_body["input"][1]["role"]=="assistant"&&authored_body["input"][1]["content"][0]["type"]=="output_text");authored.messages[1].refusal="Refusal without provider items";rejected([&]{serialize_responses_request(config,authored);});
    require(model_protocol_diagnostic(ModelProtocolError("Responses final arguments differ from deltas"))=="responses_arguments_mismatch");
    require(model_protocol_diagnostic(ModelProtocolError("Incomplete Responses stream"))=="responses_stream_incomplete");
    require(model_protocol_diagnostic(ModelProtocolError("Provider response failed or was incomplete"))=="responses_provider_incomplete");
    require(model_protocol_diagnostic(ModelProtocolError("Invalid Responses JSON event"))=="responses_json_invalid");
    require(model_protocol_diagnostic(ModelProtocolError("private synthetic provider payload"))=="");
    std::cout<<"Native Responses request and SSE contract passed byte fragmentation, output identity/lifecycle validation, reasoning-only nonempty encrypted-content variation with exact bounded done-source continuation, strict other-field rejection, bounded value-free first-mismatch structural diagnostics, usage normalization, stateless reasoning/tool continuation, safe invariant diagnostics and malformed/incomplete rejection. All model output is synthetic.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
