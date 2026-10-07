#include "agentflow/responses_stream.hpp"
#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <stdexcept>
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
ModelCompletion decode(const std::string& bytes,std::size_t fragment=1){ResponsesStream stream([](const ModelEvent&){});for(std::size_t i=0;i<bytes.size();i+=fragment)stream.feed(std::string_view(bytes).substr(i,fragment));return stream.finish();}
int main(){try{
    const auto fixture=events();const auto bytes=wire(fixture);const auto result=decode(bytes);require(result.content=="Hello \xf0\x9f\x8c\x8d"&&result.tool_calls.size()==1&&result.finish_reason=="tool_calls");require(result.tool_calls[0].id=="call_test");const auto usage=Json::parse(result.usage_json);require(usage["prompt_tokens"]==12&&usage["completion_tokens"]==7&&usage["prompt_tokens_details"]["cached_tokens"]==4&&usage["completion_tokens_details"]["reasoning_tokens"]==3);
    for(std::size_t size:{2,7,129,4096})require(decode(bytes,size).provider_items_json==result.provider_items_json);
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
    rejected([&]{serialize_chat_request(config,request);});auto changed=request;changed.messages[1].content="changed";rejected([&]{serialize_responses_request(config,changed);});changed=request;changed.messages.pop_back();rejected([&]{serialize_responses_request(config,changed);});
    ModelRequest authored;authored.messages={{MessageRole::user,"Question"},{MessageRole::assistant,"Previous plain text"},{MessageRole::user,"Next"}};const auto authored_body=Json::parse(serialize_responses_request(config,authored));require(authored_body["input"][1]["role"]=="assistant"&&authored_body["input"][1]["content"][0]["type"]=="input_text");authored.messages[1].refusal="Refusal without provider items";rejected([&]{serialize_responses_request(config,authored);});
    std::cout<<"Native Responses request and SSE contract passed byte fragmentation, output identity/lifecycle validation, terminal consistency, usage normalization, stateless reasoning/tool continuation and malformed/incomplete rejection. All model output is synthetic.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
