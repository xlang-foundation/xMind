#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
using namespace agentflow;using Json=nlohmann::json;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>void rejects(F function){try{function();}catch(const std::invalid_argument&){return;}throw std::runtime_error("Expected Claude request rejection");}
}
int main(){try{
    ChatProviderConfig config{"https://provider.example.invalid/v1/messages","explicit-fixture-model"};config.output_limit=Capability::supported;config.tools=Capability::supported;config.stream_usage=Capability::supported;
    ModelRequest request;request.max_output_tokens=2048;request.include_usage=true;
    request.messages={{MessageRole::system,"Backend instructions 🌍"},{MessageRole::user,"Read both files"}};
    request.tools={{"read_file","Read repository files",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"}};
    ModelMessage assistant{MessageRole::assistant,""};assistant.tool_calls={{"call-left","read_file",R"({"path":"left.txt"})"},{"call-right","read_file",R"({"path":"right.txt"})"}};request.messages.push_back(assistant);
    ModelMessage right{MessageRole::tool,"Actual right result"};right.tool_call_id="call-right";request.messages.push_back(right);
    ModelMessage left{MessageRole::tool,R"({"error":{"code":"permission_denied"}})"};left.tool_call_id="call-left";request.messages.push_back(left);
    const auto wire=Json::parse(serialize_anthropic_request(config,request));
    require(wire["model"]==config.model&&wire["stream"]==true&&wire["max_tokens"]==2048,"Explicit Claude configuration must survive");
    require(wire["system"]==Json::array({{{"type","text"},{"text","Backend instructions 🌍"}}}),"System instructions belong to the top-level field");
    require(!wire.contains("n")&&!wire.contains("stream_options")&&!wire.contains("reasoning_effort"),"OpenAI-only fields must not enter Claude requests");
    require(wire["tools"][0]==Json{{"name","read_file"},{"description","Read repository files"},{"input_schema",Json::parse(request.tools[0].input_schema_json)}},"Native schema and tool identity must survive");
    require(wire["messages"].size()==3&&wire["messages"][1]["role"]=="assistant"&&wire["messages"][1]["content"][0]==Json{{"type","tool_use"},{"id","call-left"},{"name","read_file"},{"input",{{"path","left.txt"}}}},"Assistant tool blocks must preserve call identity and typed input");
    require(wire["messages"][2]["role"]=="user"&&wire["messages"][2]["content"]==Json::array({{{"type","tool_result"},{"tool_use_id","call-right"},{"content","Actual right result"}},{{"type","tool_result"},{"tool_use_id","call-left"},{"content",left.content}}}),"Parallel tool results retain exact order, ownership and opaque content");
    auto missing=request;missing.max_output_tokens.reset();rejects([&]{serialize_anthropic_request(config,missing);});
    auto limit=config;limit.output_limit=Capability::unknown;rejects([&]{serialize_anthropic_request(limit,request);});
    auto tools=config;tools.tools=Capability::unsupported;rejects([&]{serialize_anthropic_request(tools,request);});
    auto reasoning=config;reasoning.reasoning_effort=ReasoningEffort::medium;rejects([&]{serialize_anthropic_request(reasoning,request);});
    auto invalid=request;invalid.messages.pop_back();rejects([&]{serialize_anthropic_request(config,invalid);});
    invalid=request;invalid.messages.back().tool_call_id="foreign-call";rejects([&]{serialize_anthropic_request(config,invalid);});
    invalid=request;invalid.messages[0].role=MessageRole::developer;rejects([&]{serialize_anthropic_request(config,invalid);});
    invalid=request;invalid.messages.push_back({MessageRole::system,"Late policy"});rejects([&]{serialize_anthropic_request(config,invalid);});
    invalid=request;invalid.messages[2].provider_items_json=R"([{"type":"reasoning","encrypted_content":"opaque"}])";rejects([&]{serialize_anthropic_request(config,invalid);});
    invalid=request;invalid.messages[2].refusal="Refusal metadata";rejects([&]{serialize_anthropic_request(config,invalid);});
    invalid=request;invalid.messages[2].tool_calls[0].arguments_json=R"({"path":"first","path":"second"})";rejects([&]{serialize_anthropic_request(config,invalid);});
    invalid=request;invalid.tools[0].input_schema_json=R"({"type":"object","type":"string"})";rejects([&]{serialize_anthropic_request(config,invalid);});
    ModelRequest plain;plain.max_output_tokens=1024;plain.messages={{MessageRole::user,"Exact caller text"},{MessageRole::user,"Second caller text"}};
    const auto joined=Json::parse(serialize_anthropic_request(config,plain));require(joined["messages"].size()==1&&joined["messages"][0]["content"].size()==2&&!joined.contains("system")&&!joined.contains("tools"),"Consecutive caller text retains separate ordered blocks without invented options");
    plain.messages={{MessageRole::system,"Only policy"}};rejects([&]{serialize_anthropic_request(config,plain);});
    plain.messages={{MessageRole::user,""}};rejects([&]{serialize_anthropic_request(config,plain);});
    std::cout<<"Native Claude request component passed explicit limits/capabilities, ordered text and parallel tool identity, schema preservation, duplicate-key rejection and incompatible-history refusal. No transport, provider enrollment or live inference was executed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
