#include "agentflow/responses_context.hpp"
#include "agentflow/model_provider.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "agentflow/gemini_history.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <thread>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
using Json=nlohmann::json;
using Code=ResponsesContextErrorCode;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action> void context_rejects(Code expected,Action action){
    try{action();}catch(const ResponsesContextError& error){require(error.code()==expected,"Expected fixed context protocol code");require(std::string(error.what())=="Responses context protocol rejected","Context errors must not expose private payloads");return;}
    throw std::runtime_error("Expected context rejection did not occur");
}
template<class Error,class Action> void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected rejection did not occur");}
const std::string canonical=R"([
 {"id":"msg_original","type":"message","role":"user","status":"completed","content":[{"type":"input_text","text":"Synthetic original task. )"+std::string("\xf0\x9f\x8c\x8d")+R"("}]},
 {"id":"msg_continue","type":"message","role":"user","status":"completed","content":[{"type":"input_text","text":"Continue the synthetic task."}]},
 {"id":"cmp_retained","type":"compaction","encrypted_content":"synthetic-opaque-\u0061","retained_metadata":{"decimal":1.00000000000000000001,"i\u0064":"escaped","large":18446744073709551617}}
])";
const std::string recanonical=R"([
 {"id":"msg_original","type":"message","role":"user","status":"completed","content":[{"type":"input_text","text":"Synthetic original task. )"+std::string("\xf0\x9f\x8c\x8d")+R"("}]},
 {"id":"msg_continue","type":"message","role":"user","status":"completed","content":[{"type":"input_text","text":"Continue the synthetic task."}]},
 {"id":"msg_followup","type":"message","role":"user","status":"completed","content":[{"type":"input_text","text":"Synthetic follow-up after compact."}]},
 {"id":"cmp_recompacted","type":"compaction","encrypted_content":"synthetic-next-opaque-\u0061","retained_metadata":{"decimal":1.00000000000000000001}}
])";
std::string envelope(std::string output=canonical,std::string actual_usage=R"({"input_tokens":19,"input_tokens_details":{"cached_tokens":0,"cache_write_tokens":2},"output_tokens":7,"output_tokens_details":{"reasoning_tokens":3},"total_tokens":26})"){
    return R"({"id":"resp_synthetic_context","object":"response.compaction","created_at":1764967971,"output":)"+output+",\"usage\":"+actual_usage+"}";
}
ChatProviderConfig configuration(std::string endpoint){ChatProviderConfig value;value.endpoint=std::move(endpoint);value.model="synthetic-responses-context-model";value.wire=ProviderWire::responses;
    value.tools=Capability::supported;value.stream_usage=Capability::supported;value.output_limit=Capability::supported;value.reasoning=Capability::supported;value.reasoning_effort=ReasoningEffort::medium;value.deadline=5s;value.idle_timeout=2s;return value;
}
ModelRequest ordinary(){ModelRequest value;value.messages={{MessageRole::system,"Synthetic repository instructions."},{MessageRole::user,"Synthetic original task. "+std::string("\xf0\x9f\x8c\x8d")}};
    value.tools={{"read_file","Synthetic read definition.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false})"}};
    ModelMessage assistant;assistant.role=MessageRole::assistant;assistant.tool_calls={{"call_original","read_file",R"({"pa\u0074h":"README.md","decimal":1.00000000000000000001})"}};
    value.messages.push_back(assistant);ModelMessage result;result.role=MessageRole::tool;result.tool_call_id="call_original";result.content="Synthetic original file result.";value.messages.push_back(result);
    value.messages.push_back({MessageRole::assistant,"Synthetic legacy plain answer with quote \" and slash \\."});
    value.messages.push_back({MessageRole::assistant,R"({"source":"graph_join","outputs":{"read":{"content":"Synthetic graph data\n"}}})"});
    value.messages.push_back({MessageRole::user,"Continue the synthetic task."});
    value.include_usage=true;value.max_output_tokens=128;return value;
}
ModelRequest projected(const ResponsesCanonicalWindow& window){ModelRequest value;value.canonical_window=window;value.messages={{MessageRole::system,"Synthetic repository instructions."},{MessageRole::developer,"Synthetic backend instructions."},{MessageRole::user,"Synthetic follow-up after compact."}};
    value.tools=ordinary().tools;value.max_output_tokens=128;value.include_usage=true;return value;
}
void pure_contract(){
    const auto window=parse_responses_canonical_window(canonical);require(window.items_json()==canonical,"Canonical window must preserve every array byte and all opaque/number/string lexemes");
    const auto compact=parse_responses_compaction_response(envelope());require(compact.window.items_json()==canonical&&compact.response_id=="resp_synthetic_context"&&compact.created_at==1764967971&&compact.response_json==envelope(),"Compaction envelope must preserve complete canonical output, raw response and actual identity/time");
    const auto supplied=Json::parse(compact.usage_json);require(supplied.at("input_tokens")==19&&supplied.at("output_tokens")==7&&supplied.at("total_tokens")==26&&supplied.at("input_tokens_details").at("cache_write_tokens")==2,"Compaction usage must retain actual supplied counters");
    auto absent=Json::parse(envelope());absent.erase("usage");require(parse_responses_compaction_response(absent.dump()).usage_json=="null","Missing usage must not create metrics");
    require(parse_responses_compaction_response(envelope(canonical,"null")).usage_json=="null","Null usage must remain absent");
    require(parse_responses_compaction_response(envelope(canonical,R"({"input_tokens":0})")).usage_json==R"({"input_tokens":0})","Partial actual usage must not invent output or total tokens");
    const auto retained=R"([{"id":"rs_retained","type":"reasoning","summary":[{"type":"summary_text","text":"Synthetic private history."}],"content":[],"encrypted_content":"synthetic-signed-reasoning"},
 {"id":"fc_retained","type":"function_call","call_id":"call_retained","name":"read_file","arguments":"{\"path\":\"README.md\",\"decimal\":1.00000000000000000001}","status":"completed"},
 {"type":"function_call_output","call_id":"call_retained","output":"Synthetic observed data."},
 {"id":"msg_retained","type":"message","role":"assistant","phase":"final_answer","content":[{"type":"output_text","text":"Synthetic prior conclusion.","annotations":[]}],"private_metadata":{"same":true}},
 {"id":"cmp_retained","type":"compaction","encrypted_content":"synthetic-canonical"}])";
    require(parse_responses_canonical_window(retained).items_json()==retained,"Known retained reasoning/function/message items must remain complete and ordered");
    {const std::string nullable=R"([{"id":"msg_nullable_phase","type":"message","role":"assistant","phase":null,"content":[{"type":"output_text","text":"Synthetic prior answer."}]},{"id":"cmp_nullable_phase","type":"compaction","encrypted_content":"synthetic-nullable-phase"}])";
     require(parse_responses_canonical_window(nullable).items_json()==nullable,"Documented nullable message phase must remain exact in canonical context");}
    {const std::string opaque_window=R"([{"id":"rs_opaque","type":"reasoning","summary":[],"encrypted_content":"synthetic-\u0000-\u0061-)"+std::string("\xf0\x9f\x8c\x8d")+R"("},{"id":"cmp_opaque","type":"compaction","encrypted_content":"synthetic-\u0000-\u0062"}])";
     require(parse_responses_compaction_response(envelope(opaque_window)).window.items_json()==opaque_window,"Opaque JSON strings must retain escaped NUL, UTF-8 and exact escapes without C-string truncation");}
    {Json many=Json::array();for(int i=0;i<256;++i)many.push_back({{"id","msg_many_"+std::to_string(i)},{"type","message"},{"role","user"},{"content",Json::array({{{"type","input_text"},{"text","Synthetic user "+std::to_string(i)}}})}});
     many.push_back({{"id","cmp_many"},{"type","compaction"},{"encrypted_content","synthetic-many"}});const auto bytes=many.dump();require(parse_responses_compaction_response(envelope(bytes)).window.items_json()==bytes,"Canonical windows with more than MCP's 128 array entries must retain every item");}
    {Json summaries=Json::array(),parts=Json::array();for(int i=0;i<256;++i){summaries.push_back({{"type","summary_text"},{"text","Synthetic summary "+std::to_string(i)}});parts.push_back({{"type","input_text"},{"text","Synthetic part "+std::to_string(i)}});}
     const Json many=Json::array({{{"id","msg_parts"},{"type","message"},{"role","user"},{"content",parts}},{{"id","rs_parts"},{"type","reasoning"},{"summary",summaries},{"encrypted_content","synthetic-parts"}},{{"id","cmp_parts"},{"type","compaction"},{"encrypted_content","synthetic-canonical-parts"}}});const auto bytes=many.dump();require(parse_responses_compaction_response(envelope(bytes)).window.items_json()==bytes,"Supported nested content/summary arrays must exceed unrelated MCP collection bounds without pruning");}
    auto values=Json::parse(canonical);const auto compaction_index=values.size()-1;
    auto reject_item=[&](const Json& changed,Code code){context_rejects(code,[&]{parse_responses_canonical_window(changed.dump());});};
    {auto changed=values;changed[compaction_index]["type"]="future_opaque_item";reject_item(changed,Code::unsupported_item);}
    {auto changed=values;changed[compaction_index]["encrypted_content"]="";reject_item(changed,Code::unsupported_item);}
    for(const auto opaque:{Json(nullptr),Json(7),Json::object(),Json::array()}){auto changed=values;changed[compaction_index]["encrypted_content"]=opaque;reject_item(changed,Code::unsupported_item);}
    {auto changed=values;changed[compaction_index].erase("encrypted_content");reject_item(changed,Code::unsupported_item);}
    {auto changed=values;changed[compaction_index]["id"]=changed[0]["id"];reject_item(changed,Code::invalid_identity);}
    {auto changed=values;changed[compaction_index]["id"]="private synthetic identity with whitespace";reject_item(changed,Code::invalid_identity);}
    {auto changed=values;changed[0].erase("id");reject_item(changed,Code::invalid_identity);}
    for(const auto* role:{"system","developer","tool"}){auto changed=values;changed[0]["role"]=role;reject_item(changed,Code::unsupported_item);}
    {auto changed=values;changed[0]["content"][0]["type"]="input_image";reject_item(changed,Code::unsupported_item);}
    {auto changed=values;changed[0]["content"][0]["future_numeric_metadata"]=1.0001;reject_item(changed,Code::unsupported_item);}
    {auto changed=values;changed[0]["status"]="in_progress";reject_item(changed,Code::unsupported_item);}
    {auto changed=values;changed.push_back(changed[compaction_index]);changed.back()["id"]="cmp_next";const auto encoded=changed.dump();require(parse_responses_canonical_window(encoded).items_json()==encoded,"All bounded canonical compaction items must be retained without pruning");}
    context_rejects(Code::invalid_correlation,[&]{parse_responses_canonical_window(R"([{"id":"message_only","type":"message","role":"user","content":[]}])");});
    context_rejects(Code::invalid_correlation,[&]{parse_responses_canonical_window(R"([{"id":"fc","type":"function_call","call_id":"call_unanswered","name":"read_file","arguments":"{}"},{"id":"cmp","type":"compaction","encrypted_content":"synthetic"}])");});
    context_rejects(Code::invalid_correlation,[&]{parse_responses_canonical_window(R"([{"type":"function_call_output","call_id":"orphan","output":"synthetic"},{"id":"cmp","type":"compaction","encrypted_content":"synthetic"}])");});
    context_rejects(Code::invalid_json,[&]{parse_responses_canonical_window(R"([{"id":"cmp","i\u0064":"other","type":"compaction","encrypted_content":"synthetic"}])");});
    context_rejects(Code::invalid_json,[&]{parse_responses_canonical_window(R"([{"id":"cmp","type":"compaction","encrypted_content":"\ud800"}])");});
    {auto invalid=canonical;invalid[invalid.find("Synthetic original")]='\xff';context_rejects(Code::invalid_json,[&]{parse_responses_canonical_window(invalid);});}
    {std::string nested="0";for(int i=0;i<65;++i)nested="["+nested+"]";context_rejects(Code::exceeds_limits,[&]{parse_responses_canonical_window("[{\"id\":\"cmp\",\"type\":\"compaction\",\"encrypted_content\":\"synthetic\",\"metadata\":"+nested+"}]");});}
    const std::string boundary_prefix=R"([{"id":"cmp_boundary","type":"compaction","encrypted_content":")",boundary_suffix=R"("}])";
    const auto boundary=boundary_prefix+std::string(responses_context_window_limit-boundary_prefix.size()-boundary_suffix.size(),'x')+boundary_suffix;
    require(parse_responses_canonical_window(boundary).items_json().size()==responses_context_window_limit,"Canonical window must support its declared 4 MiB byte boundary independently of MCP framing");
    context_rejects(Code::exceeds_limits,[&]{parse_responses_canonical_window(boundary+" ");});
    for(const auto actual:{R"({"input_tokens":-1})",R"({"input_tokens":1.5})",R"({"input_tokens":9007199254740992})",R"({"input_tokens":1,"output_tokens":1,"total_tokens":3})",R"({"input_tokens_details":{"cached_tokens":-1}})",R"({"output_tokens_details":{"reasoning_tokens":null}})"})context_rejects(Code::invalid_usage,[&]{parse_responses_compaction_response(envelope(canonical,actual));});
    context_rejects(Code::invalid_json,[&]{parse_responses_compaction_response(envelope(canonical,R"({"input_tokens":1,"input_\u0074okens":2})"));});
    for(const auto* field:{"object","output","created_at"}){auto invalid=Json::parse(envelope());invalid.erase(field);context_rejects(Code::invalid_compaction_response,[&]{parse_responses_compaction_response(invalid.dump());});}
    {auto invalid=Json::parse(envelope());invalid["created_at"]=1.0;context_rejects(Code::invalid_compaction_response,[&]{parse_responses_compaction_response(invalid.dump());});}
    {auto invalid=Json::parse(envelope());invalid["error"]="private synthetic provider message";context_rejects(Code::invalid_compaction_response,[&]{parse_responses_compaction_response(invalid.dump());});}
    require(parse_responses_input_tokens_response(R"({"object":"response.input_tokens","input_tokens":0})").input_tokens==0,"Actual zero token count must remain zero");
    {const std::string raw=R"( { "ob\u006aect" : "response.input_tokens", "input_tokens" : 7 } )";const auto count=parse_responses_input_tokens_response(raw);require(count.input_tokens==7&&count.response_json==raw,"Actual count acknowledgement must retain exact raw private bytes independently of its value");}
    require(parse_responses_input_tokens_response(R"({"object":"response.input_tokens","input_tokens":9007199254740991})").input_tokens==9007199254740991LL,"Token count exact integer boundary must remain exact");
    for(const auto* invalid:{R"({"object":"response.input_tokens","input_tokens":-1})",R"({"object":"response.input_tokens","input_tokens":1.0})",R"({"object":"response.input_tokens","input_tokens":9007199254740992})",R"({"object":"response.input_tokens","input_tokens":null})",R"({"object":"response.input_tokens"})",R"({"object":"response.input_tokens","input_tokens":1,"usage":{}})"})context_rejects(Code::invalid_token_count,[&]{parse_responses_input_tokens_response(invalid);});
    const auto config=configuration("https://example.invalid/v1/responses");const auto request=ordinary();const auto count=Json::parse(serialize_responses_input_tokens_request(config,request)),compact_request=Json::parse(serialize_responses_compaction_request(config,request));
    const auto inference=Json::parse(serialize_responses_request(config,request));
    require(count.size()==4&&count.contains("tools")&&count.contains("reasoning")&&compact_request.size()==2&&count["input"]==compact_request["input"]&&count["input"]==inference["input"],"Inference/count/compact must share exact original input and role-aware historical text; endpoint parameters stay separate");
    for(const auto index:{4,5})require(count["input"][index]["role"]=="assistant"&&count["input"][index]["content"][0]["type"]=="output_text"&&count["input"][index]["content"][0]["text"]==request.messages[index].content&&!count["input"][index].contains("id"),"Historical plain assistants are output text without invented provider IDs");
    const auto projected_request=projected(window);const auto serialized=serialize_responses_input_tokens_request(config,projected_request);
    require(serialized.find(canonical.substr(1,canonical.size()-2))!=std::string::npos,"Canonical input raw array interior must reach request unchanged");
    {const auto input=Json::parse(serialized).at("input");require(input[0]["role"]=="system"&&input[1]["role"]=="developer"&&input[2]["id"]=="msg_original"&&input[4]["id"]=="cmp_retained"&&input[5]["role"]=="user","Current trusted instructions must precede canonical context and the original tail");}
    {auto instructions_only=projected_request;instructions_only.messages.pop_back();const auto input=Json::parse(serialize_responses_request(config,instructions_only))["input"];require(input.size()==5&&input[1]["role"]=="developer"&&input.back()["id"]=="cmp_retained","All-trusted instruction tails must still include the complete canonical window");}
    {auto late=projected_request;late.messages.push_back({MessageRole::developer,"Late synthetic instruction."});rejects<IncompatibleProviderHistory>([&]{serialize_responses_request(config,late);});}
    validate_responses_compaction_result(config,request,compact);
    validate_responses_compaction_result(serialize_responses_compaction_request(config,request),compact);
    context_rejects(Code::invalid_request,[&]{validate_responses_compaction_result(R"({"model":"synthetic-responses-context-model","input":[],"tools":[]})",compact);});
    validate_responses_compaction_result(config,projected_request,parse_responses_compaction_response(envelope(recanonical)));
    for(const auto mode:{0,1,2,3}){auto altered=values;
        if(mode==0)altered[0]["content"][0]["text"]="Invented private user authority.";
        else if(mode==1)altered.erase(altered.begin());
        else if(mode==2){altered.insert(altered.begin(),altered[0]);altered[0]["id"]="msg_added";}
        else std::swap(altered[0],altered[1]);
        const auto invalid=parse_responses_compaction_response(envelope(altered.dump()));context_rejects(Code::invalid_correlation,[&]{validate_responses_compaction_result(config,request,invalid);});
    }
    {auto changed=Json::parse(recanonical);changed[0]["id"]="msg_reassigned";const auto invalid=parse_responses_compaction_response(envelope(changed.dump()));context_rejects(Code::invalid_correlation,[&]{validate_responses_compaction_result(config,projected_request,invalid);});}
    {auto inconsistent=compact;inconsistent.usage_json="null";context_rejects(Code::invalid_compaction_response,[&]{validate_responses_compaction_result(config,request,inconsistent);});}
    {auto invalid=config;invalid.wire=ProviderWire::chat_completions;context_rejects(Code::invalid_request,[&]{serialize_responses_compaction_request(invalid,request);});}
    for(const auto wire:{ProviderWire::chat_completions,ProviderWire::anthropic_messages,ProviderWire::gemini_generate_content}){auto incompatible=config;incompatible.wire=wire;rejects<IncompatibleProviderHistory>([&]{serialize_responses_request(incompatible,projected_request);});}
    rejects<IncompatibleProviderHistory>([&]{serialize_chat_request(config,projected_request);});
    rejects<IncompatibleProviderHistory>([&]{serialize_anthropic_request(config,projected_request);});
    rejects<IncompatibleProviderHistory>([&]{gemini_model_request(projected_request,Capability::supported);});
    {auto invalid=request;invalid.messages[2].provider_items_json=canonical;rejects<std::invalid_argument>([&]{serialize_responses_request(config,invalid);});}
    {auto invalid=projected_request;ModelMessage collision;collision.role=MessageRole::assistant;collision.content="Synthetic collision.";collision.provider_items_json=R"([{"id":"msg_original","type":"message","role":"assistant","content":[{"type":"output_text","text":"Synthetic collision."}]}])";invalid.messages.push_back(collision);rejects<std::invalid_argument>([&]{serialize_responses_request(config,invalid);});}
    {auto invalid=projected(parse_responses_canonical_window(retained));ModelMessage collision;collision.role=MessageRole::assistant;collision.tool_calls={{"call_retained","read_file","{}"}};invalid.messages.push_back(collision);ModelMessage observed;observed.role=MessageRole::tool;observed.tool_call_id="call_retained";observed.content="Synthetic duplicate call result.";invalid.messages.push_back(observed);rejects<std::invalid_argument>([&]{serialize_responses_request(config,invalid);});}
    {
        const std::string raw=R"([{"id":"msg_exact_tail","type":"message","role":"assistant","phase":"final_answer","content":[{"type":"output_text","text":"Synthetic exact receipt.","annotations":[]}],"synthetic_metadata":{"decimal":1.00000000000000000001,"k\u0065y":"escaped"}},
 {"id":"rs_exact_tail","type":"reasoning","summary":[],"content":[],"encrypted_content":"synthetic-done-\u0062"}])";
        ModelMessage assistant;assistant.role=MessageRole::assistant;assistant.content="Synthetic exact receipt.";assistant.provider_items_json=raw;auto exact=projected_request;exact.messages.push_back(assistant);
        require(serialize_responses_request(config,exact).find(raw.substr(1,raw.size()-2))!=std::string::npos,"Ordinary validated receipts must also retain exact opaque/number/escaped metadata bytes");
        for(const auto* duplicate:{R"([{"id":"msg_first","i\u0064":"msg_second","type":"message","role":"assistant","content":[{"type":"output_text","text":"Synthetic exact receipt."}]}])",R"([{"id":"msg_exact","type":"message","role":"assistant","content":[{"type":"output_text","text":"hidden","t\u0065xt":"Synthetic exact receipt."}]}])"}){
            auto malformed=exact;malformed.messages.back().provider_items_json=duplicate;rejects<std::invalid_argument>([&]{serialize_responses_request(config,malformed);});
        }
        std::string nested="0";for(int i=0;i<65;++i)nested="["+nested+"]";auto deep=exact;deep.messages.back().provider_items_json=R"([{"id":"msg_deep","type":"message","role":"assistant","content":[{"type":"output_text","text":"Synthetic exact receipt."}],"synthetic_metadata":)"+nested+"}]";
        rejects<std::invalid_argument>([&]{serialize_responses_request(config,deep);});
    }
    {auto oversized=projected(parse_responses_canonical_window(boundary));oversized.messages.back().content=std::string(responses_context_window_limit,'x');
        rejects<ModelRequestCapacityExceeded>([&]{serialize_responses_request(config,oversized);});
        rejects<ModelRequestCapacityExceeded>([&]{serialize_responses_compaction_request(config,oversized);});
        rejects<ModelRequestCapacityExceeded>([&]{serialize_responses_input_tokens_request(config,oversized);});}
    {ModelRequest many;many.messages.assign(4097,ModelMessage{MessageRole::user,"Synthetic bounded message."});
        require(Json::parse(serialize_responses_input_tokens_request(config,many)).at("input").size()==4097,"Context counting must support the registered original-message bound beyond the legacy Chat limit");
        require(Json::parse(serialize_responses_compaction_request(config,many)).at("input").size()==4097,"Eligible-prefix preparation must retain every original message without truncation");
        rejects<ModelRequestCapacityExceeded>([&]{serialize_chat_request(config,many);});
        many.messages.resize(8193,ModelMessage{MessageRole::user,"Synthetic bounded message."});
        rejects<ModelRequestCapacityExceeded>([&]{serialize_responses_input_tokens_request(config,many);});}
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        pure_contract();const std::string base=argv[1],synthetic="context-fixture-token-not-a-real-key";SecretBytes credential({reinterpret_cast<const std::uint8_t*>(synthetic.data()),synthetic.size()});
        auto request=ordinary();auto config=configuration(base+"/initial/responses");
        {const auto measured=count_responses_context(config,request,&credential);require(measured.input_tokens==43&&measured.response_json==R"({"object":"response.input_tokens","input_tokens":43})","Native count adapter must return actual counter and exact HTTP response independently of usage");}
        {const auto answer=complete_model(config,request,&credential,[](const ModelEvent&){});require(answer.content=="Synthetic plain-history continuation."&&Json::parse(answer.usage_json)["input_tokens"]==43,"Ordinary native inference must accept the same role-aware legacy/plain-graph history as exact counting");}
        const auto compact=compact_responses_context(config,request,&credential);require(compact.window.items_json()==canonical,"Native compact must preserve the complete actual HTTP canonical array");
        require(Json::parse(compact.usage_json)["total_tokens"]==26,"Native compact actual HTTP usage must be retained separately from count");
        auto followup=projected(compact.window);config.endpoint=base+"/continuation/responses";
        require(count_responses_context(config,followup,&credential).input_tokens==27,"Projected follow-up count must use the canonical window and actual tail");
        const auto completion=complete_model(config,followup,&credential,[](const ModelEvent&){});
        require(completion.content=="Synthetic compact continuation."&&Json::parse(completion.usage_json)["input_tokens"]==27,"Ordinary native Responses continuation must consume the canonical window and actual metrics");
        const auto next=compact_responses_context(config,followup,&credential);require(next.window.items_json()==recanonical,"Recompaction must preserve all prior users and follow-up input without manual pruning");
        config.endpoint=base+"/unsupported/responses";context_rejects(Code::unsupported_item,[&]{compact_responses_context(config,request,&credential);});
        config.endpoint=base+"/invalid-count/responses";context_rejects(Code::invalid_token_count,[&]{count_responses_context(config,request,&credential);});
        config.endpoint=base+"/duplicate/responses";context_rejects(Code::invalid_json,[&]{compact_responses_context(config,request,&credential);});
        config.endpoint=base+"/user-drift/responses";context_rejects(Code::invalid_correlation,[&]{compact_responses_context(config,request,&credential);});
        config.endpoint=base+"/redirect/responses";rejects<ProviderHttpError>([&]{compact_responses_context(config,request,&credential);});
        config.endpoint=base+"/limit/responses";bool limited=false;try{compact_responses_context(config,request,&credential);}catch(const ProviderHttpError& error){limited=true;require(error.status==400&&error.code=="context_length_exceeded"&&std::string(error.what()).find("private-context-fixture")==std::string::npos,"Context HTTP error must preserve safe diagnostic without leaking body");}require(limited,"Context limit must not retry or silently truncate");
        config.endpoint=base+"/cancel/responses";{std::stop_source stop;std::jthread canceller([&]{std::this_thread::sleep_for(100ms);stop.request_stop();});rejects<TransportCancelled>([&]{compact_responses_context(config,request,&credential,stop.get_token());});}
        config.endpoint=base+"/timeout/responses";config.deadline=150ms;rejects<TransportTimeout>([&]{count_responses_context(config,request,&credential);});
        config.endpoint=base+"/pre-cancel/responses";{std::stop_source stop;stop.request_stop();rejects<TransportCancelled>([&]{compact_responses_context(config,request,&credential,stop.get_token());});}
        for(const auto* suffix:{"/initial/responses?target=private","/initial/responses#private","/initial/responses/compact","/initial/%72esponses","/initial/responses/"}){config.endpoint=base+suffix;context_rejects(Code::invalid_endpoint,[&]{compact_responses_context(config,request,&credential);});}
        config=configuration(base+"/after-failure/responses");require(count_responses_context(config,followup,&credential).input_tokens==27,"Fresh native context adapter must remain usable after failures");
        std::cout<<"Native Responses context codec and HTTP contracts passed against an independent synthetic peer; no live model, persistence or compaction orchestration was exercised\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
