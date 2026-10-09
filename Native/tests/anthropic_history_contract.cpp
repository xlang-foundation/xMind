// Pure synthetic DTO/receipt contract; actual agent/SQLite acceptance is separate.
#include "agentflow/anthropic_history.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <string>
#include <utility>
using namespace agentflow;using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){try{action();}catch(const std::invalid_argument&){return;}throw std::runtime_error("Expected bounded incompatible Claude history rejection");}
const std::string input=R"({"pa\u0074h":"quoted","fraction":1.2345678901234567890123456789,"large":18446744073709551615})";
const std::string raw=std::string(R"([{"type":"thinking","thinking":"Hidden fixture thought","signature":"opaque-signed-fixture"},{"type":"text","text":"Before "},{"type":"tool_use","id":"toolu-left","name":"measure","\u0069nput":)")+input+R"(},{"type":"text","text":"middle "},{"type":"tool_use","id":"toolu-right","name":"measure","input":{}},{"type":"redacted_thinking","data":"opaque-redacted-fixture"},{"type":"text","text":"after"}])";
ModelMessage assistant(){
    ModelMessage result;result.role=MessageRole::assistant;result.content="Before middle after";result.tool_calls={{"toolu-left","measure",input},{"toolu-right","measure","{}"}};
    result.provider_items_json=anthropic_content_receipt(raw,"tool_calls","tool_use");return result;
}
ModelRequest request(const ModelMessage& message){
    ModelRequest result;result.max_output_tokens=64;result.tools={{"measure","Synthetic component tool",R"({"type":"object","additionalProperties":true})"}};
    result.messages={{MessageRole::system,"Native component instructions"},{MessageRole::user,"Synthetic caller"},message,
        {MessageRole::tool,R"({"number":900719925474099312345})",{},"toolu-right"},{MessageRole::tool,"ordinary native tool output",{},"toolu-left"},{MessageRole::user,"Continue"}};
    return result;
}
ModelMessage with_content(ModelMessage message,const std::string& replacement){
    auto items=Json::parse(message.provider_items_json);items[0]["content_json"]=replacement;message.provider_items_json=items.dump();return message;
}
}
int main(){try{
    ChatProviderConfig config{"http://127.0.0.1:1/messages","synthetic-claude",Capability::supported,Capability::supported,Capability::supported};config.wire=ProviderWire::anthropic_messages;
    {
        const std::string caller=R"({ "\u0074ype" : "\u0064irect" })";
        const auto source=std::string(R"([{"type":"tool_use","id":"caller-direct","name":"measure","input":)")+input+",\"caller\":"+caller+"}]";
        ModelMessage direct;direct.role=MessageRole::assistant;direct.tool_calls={{"caller-direct","measure",input}};direct.provider_items_json=anthropic_content_receipt(source,"tool_calls","tool_use");
        require(anthropic_history_content(direct)==source,"Validated direct caller metadata must survive receipt validation exactly");
        ModelRequest next;next.max_output_tokens=64;next.tools={{"measure","Synthetic direct-call component",R"({"type":"object"})"}};next.messages={{MessageRole::user,"Synthetic direct caller"},direct,{MessageRole::tool,"Synthetic tool result",{},"caller-direct"}};
        const auto encoded=serialize_anthropic_request(config,next);require(encoded.find(source)!=std::string::npos&&encoded.find(caller)!=std::string::npos,"Next Claude request must replay the original direct caller rather than drop or normalize it");
        for(const auto* invalid:{"null","[]","{}",R"({"type":"DIRECT"})",R"({"type":"direct","extra":true})",R"({"type":"code_execution_20260120","tool_id":"server-call"})",R"({"type":"direct","type":"direct"})"}){
            const auto bad=std::string(R"([{"type":"tool_use","id":"caller-direct","name":"measure","input":)")+input+",\"caller\":"+invalid+"}]";
            rejects([&]{anthropic_content_receipt(bad,"tool_calls","tool_use");});rejects([&]{anthropic_history_content(with_content(direct,bad));});
        }
    }
    auto message=assistant();require(anthropic_history_content(message)==raw,"Receipt must retain exact block order and raw argument/escaped-key tokens");
    // This is the agent's real parse/dump envelope shape, without claiming SQL.
    const auto envelope=Json{{"provider_items",Json::parse(message.provider_items_json)},{"tool_calls",Json::array({{{"id","toolu-left"},{"name","measure"},{"arguments",input}},{{"id","toolu-right"},{"name","measure"},{"arguments","{}"}}})},{"content",message.content}}.dump();
    const auto reloaded=Json::parse(envelope);message.provider_items_json=reloaded.at("provider_items").dump();message.tool_calls[0].arguments_json=reloaded.at("tool_calls")[0].at("arguments").get<std::string>();
    require(anthropic_history_content(message)==raw,"Agent-shaped receipt persistence must not round numerical/escaped source strings");
    auto encoded=serialize_anthropic_request(config,request(message));require(encoded.find(raw)!=std::string::npos&&encoded.find(input)!=std::string::npos,"Final wire must splice checked original blocks without floating-point re-dump");
    const auto wire=Json::parse(encoded);const auto& blocks=wire.at("messages")[1].at("content");
    require(blocks.size()==7&&blocks[0].at("type")=="thinking"&&blocks[1].at("type")=="text"&&blocks[2].at("type")=="tool_use"&&blocks[3].at("type")=="text"&&blocks[4].at("type")=="tool_use"&&blocks[5].at("type")=="redacted_thinking"&&blocks[6].at("type")=="text","Interleaved signed/text/tool/redacted order must survive outgoing serialization");
    require(wire.at("messages")[2].at("content")[0].at("tool_use_id")=="toolu-right"&&wire.at("messages")[2].at("content")[0].at("content")==R"({"number":900719925474099312345})","Ordinary tool output must remain an opaque string with original result identity");
    require(!wire.contains("thinking")&&!wire.contains("reasoning_effort"),"Receipt replay cannot fabricate thinking controls");
    // Adjacent assistant blocks retain each original segment instead of flattening.
    auto joined=request(message);ModelMessage prefix;prefix.role=MessageRole::assistant;prefix.content="Prefix";prefix.provider_items_json=anthropic_content_receipt(R"([{"type":"text","text":"Prefix"}])","stop","end_turn");joined.messages.insert(joined.messages.begin()+2,prefix);
    const auto joined_body=serialize_anthropic_request(config,joined);require(joined_body.find(input)!=std::string::npos&&Json::parse(joined_body).at("messages")[1].at("content").size()==8,"Coalescing adjacent assistant turns must retain every raw block");
    ModelMessage hidden;hidden.role=MessageRole::assistant;hidden.provider_items_json=anthropic_content_receipt(R"([{"type":"thinking","thinking":"","signature":"opaque-signature"},{"type":"redacted_thinking","data":"opaque-data"}])","stop","end_turn");
    ModelRequest hidden_request;hidden_request.max_output_tokens=64;hidden_request.messages={{MessageRole::user,"Question"},hidden,{MessageRole::user,"Followup"}};
    const auto hidden_body=Json::parse(serialize_anthropic_request(config,hidden_request));require(hidden_body.at("messages")[1].at("content").size()==2&&hidden_body.at("messages")[1].at("content")[0].at("type")=="thinking"&&hidden_body.at("messages")[1].at("content")[1].at("type")=="redacted_thinking","Signed/opaque blocks must remain even when ordinary answer content is empty");
    auto legacy=message;legacy.provider_items_json="[]";require(serialize_anthropic_request(config,request(legacy)).find(input)!=std::string::npos,"Legacy plain DTO history must preserve raw precise input without requiring a new receipt");
    for(int kind=0;kind<7;++kind){auto bad=message;
        if(kind==0)bad.content+="changed";if(kind==1)bad.tool_calls[0].id="foreign";if(kind==2)bad.tool_calls[0].name="different";
        if(kind==3)bad.tool_calls[0].arguments_json=Json::parse(input).dump();if(kind==4)std::swap(bad.tool_calls[0],bad.tool_calls[1]);if(kind==5)bad.role=MessageRole::user;if(kind==6)bad.refusal="unsupported";
        rejects([&]{anthropic_history_content(bad);});rejects([&]{serialize_anthropic_request(config,request(bad));});
    }
    for(const auto* replacement:{
        R"([{"type":"thinking","thinking":"unsigned"}])",R"([{"type":"thinking","thinking":"unsigned","signature":""}])",
        R"([{"type":"thinking","thinking":"unsigned","signature":7}])",R"([{"type":"redacted_thinking","data":""}])",
        R"([{"type":"text","text":"x","signature":"foreign"}])",R"([{"type":"image","source":{}}])",
        R"([{"type":"tool_result","tool_use_id":"x","content":"x"}])",R"([{"type":"text","type":"text","text":"x"}])",
        R"([{"type":"tool_use","id":"x","name":"measure","input":{"a":1,"a":2}}])",
        R"([{"type":"tool_use","id":"x","name":"measure","input":[]}])"}){
        rejects([&]{anthropic_history_content(with_content(message,replacement));});
        rejects([&]{anthropic_content_receipt(replacement,"stop","end_turn");});
    }
    auto altered=Json::parse(message.provider_items_json);altered[0]["finish_reason"]="stop";auto bad=message;bad.provider_items_json=altered.dump();rejects([&]{anthropic_history_content(bad);});
    altered=Json::parse(message.provider_items_json);altered[0]["type"]="gemini_content";bad.provider_items_json=altered.dump();rejects([&]{serialize_anthropic_request(config,request(bad));});
    altered=Json::parse(message.provider_items_json);altered[0]["endpoint"]="forbidden";bad.provider_items_json=altered.dump();rejects([&]{anthropic_history_content(bad);});
    altered=Json::parse(message.provider_items_json);altered.push_back(altered[0]);bad.provider_items_json=altered.dump();rejects([&]{anthropic_history_content(bad);});
    bad=message;bad.provider_items_json=R"([{"type":"anthropic_content","type":"anthropic_content","content_json":"[]","finish_reason":"stop","provider_finish_reason":"end_turn"}])";rejects([&]{anthropic_history_content(bad);});
    auto missing=request(message);missing.messages.erase(missing.messages.begin()+3);rejects([&]{serialize_anthropic_request(config,missing);});
    auto wrong_result=request(message);wrong_result.messages[3].tool_call_id="orphan";rejects([&]{serialize_anthropic_request(config,wrong_result);});
    auto unsupported=config;unsupported.tools=Capability::unknown;rejects([&]{serialize_anthropic_request(unsupported,request(message));});
    unsupported=config;unsupported.reasoning_effort=ReasoningEffort::low;rejects([&]{serialize_anthropic_request(unsupported,hidden_request);});
    rejects([&]{anthropic_content_receipt(raw,"length","max_tokens");});
    ModelMessage empty;empty.role=MessageRole::assistant;empty.provider_items_json=anthropic_content_receipt("[]","stop","end_turn");
    rejects([&]{anthropic_history_content(empty);});hidden_request.messages[1]=empty;rejects([&]{serialize_anthropic_request(config,hidden_request);});
    ModelMessage length;length.role=MessageRole::assistant;length.content="Partial";length.provider_items_json=anthropic_content_receipt(R"([{"type":"text","text":"Partial"}])","length","max_tokens");require(!anthropic_history_content(length).empty(),"Checked text truncation may retain its real receipt without fabricating tool calls");
    const auto many=Json::array();auto too_many=many;for(int index=0;index<65;++index)too_many.push_back({{"type","text"},{"text",""}});rejects([&]{anthropic_content_receipt(too_many.dump(),"stop","end_turn");});
    auto huge_input=Json{{"padding",std::string(1024*1024,'x')}}.dump();rejects([&]{anthropic_content_receipt("[{\"type\":\"tool_use\",\"id\":\"large\",\"name\":\"measure\",\"input\":"+huge_input+"}]","tool_calls","tool_use");});
    std::string depth="{}";for(int index=0;index<18;++index)depth="{\"nested\":"+depth+"}";rejects([&]{anthropic_content_receipt("[{\"type\":\"tool_use\",\"id\":\"deep\",\"name\":\"measure\",\"input\":"+depth+"}]","tool_calls","tool_use");});
    auto oversized_signature=Json::array({{{"type","thinking"},{"thinking",""},{"signature",std::string(65537,'s')}}}).dump();rejects([&]{anthropic_content_receipt(oversized_signature,"stop","end_turn");});
    auto invalid_utf=std::string("[{\"type\":\"text\",\"text\":\"")+char(0xc3)+"\"}]";rejects([&]{anthropic_content_receipt(invalid_utf,"stop","end_turn");});
    auto visible_overflow=Json::array({{{"type","text"},{"text",std::string(2*1024*1024,'a')}},{{"type","text"},{"text",std::string(2*1024*1024+1,'b')}}}).dump();rejects([&]{anthropic_content_receipt(visible_overflow,"stop","end_turn");});
    auto huge_hidden=hidden;huge_hidden.provider_items_json=anthropic_content_receipt(Json::array({{{"type","thinking"},{"thinking",std::string(4*1024*1024,'x')},{"signature","opaque"}}}).dump(),"stop","end_turn");
    hidden_request.messages={{MessageRole::user,std::string(4*1024*1024,'y')},huge_hidden};rejects([&]{serialize_anthropic_request(config,hidden_request);});
    auto excess_count=hidden_request;excess_count.messages.assign(4097,ModelMessage{MessageRole::user,"x"});rejects([&]{serialize_anthropic_request(config,excess_count);});
    excess_count.messages={{MessageRole::user,"x"}};excess_count.tools.assign(65,ModelToolDefinition{"measure","",R"({"type":"object"})"});rejects([&]{serialize_anthropic_request(config,excess_count);});
    auto individually_valid=hidden;individually_valid.provider_items_json=anthropic_content_receipt(Json::array({{{"type","thinking"},{"thinking",std::string(3*1024*1024,'x')},{"signature","opaque"}}}).dump(),"stop","end_turn");
    require(!anthropic_history_content(individually_valid).empty(),"Each hidden receipt is individually valid before the aggregate gate");
    ModelRequest aggregate;aggregate.max_output_tokens=64;aggregate.messages={{MessageRole::user,"Question"},individually_valid,individually_valid,individually_valid};
    rejects([&]{serialize_anthropic_request(config,aggregate);});
    std::cout<<"Native Claude history component passed ordered signed/redacted/text/tool receipts, precise escaped numeric input through agent-shaped JSON round trips, native DTO/identity/role/foreign mismatch rejection, legacy compatibility and bounded raw wire serialization. No network, SQLite, thinking control, signature cryptography, model availability or live inference claimed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
