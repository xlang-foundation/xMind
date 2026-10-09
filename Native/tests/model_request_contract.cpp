#include "agentflow/model_provider.hpp"
#include "agentflow/provider_model_policy.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <initializer_list>
#include <utility>

using namespace agentflow;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Function> void rejects(Function action) {
    try {action();} catch(const std::invalid_argument&) {return;} throw std::runtime_error("Expected request rejection did not occur");
}
}
int main() {
    try {
        for(const auto wire:{ProviderWire::responses,ProviderWire::chat_completions}){
            const auto policy=documented_openai_model_policy(wire);validate_provider_model_policy(policy);
            ChatProviderConfig selected;selected.endpoint="https://fixture.example.invalid/owned";selected.wire=wire;
            const auto bound=bind_provider_model_policy(policy,selected,"gpt-6-sol",true);
            require(bound.endpoint==selected.endpoint&&bound.wire==wire&&bound.model=="gpt-6-sol"&&bound.tools==Capability::supported,"Policy must preserve the owned route and bind exact native capabilities");
            require(bound.reasoning_effort==(wire==ProviderWire::chat_completions?std::optional{ReasoningEffort::none}:std::nullopt),"Only the documented Chat tool route requires explicit none reasoning");
            ModelRequest probe{{{MessageRole::user,"Synthetic declared model request"}},{{"inspect","Synthetic inspection",R"({"type":"object"})"}}};
            probe.include_usage=true;probe.max_output_tokens=64;
            const auto payload=Json::parse(wire==ProviderWire::responses?serialize_responses_request(bound,probe):serialize_chat_request(bound,probe));
            require(payload.at("model")=="gpt-6-sol"&&payload.at("tools").size()==1,"Bound model must serialize actual native tool requests");
            if(wire==ProviderWire::chat_completions)require(payload.at("reasoning_effort")=="none","Chat tool requirement must reach the actual request serializer");
            else require(!payload.contains("reasoning")&&!payload.contains("reasoning_effort"),"Responses reasoning remains omitted unless configured");
            for(const auto* id:{"gpt-6-astra","gpt-6.1-sol"}){
                const auto plain=bind_provider_model_policy(policy,selected,id,false);
                require(plain.tools==(wire==ProviderWire::responses?Capability::supported:Capability::unsupported),"Responses-only tools cannot become Chat tools");
                if(wire==ProviderWire::chat_completions)rejects([&]{bind_provider_model_policy(policy,selected,id,true);});
                else require(bind_provider_model_policy(policy,selected,id,true).tools==Capability::supported,"Current flagship Responses tools stay available");
                auto none=selected;none.reasoning_effort=ReasoningEffort::none;rejects([&]{bind_provider_model_policy(policy,none,id,false);});
            }
            auto high=selected;high.reasoning_effort=ReasoningEffort::high;
            if(wire==ProviderWire::chat_completions)rejects([&]{bind_provider_model_policy(policy,high,"gpt-6-sol",true);});
            else require(bind_provider_model_policy(policy,high,"gpt-6-sol",true).reasoning_effort==ReasoningEffort::high,"Responses must preserve explicit supported reasoning");
            rejects([&]{bind_provider_model_policy(policy,high,"gpt-4.1",false);});
            for(const auto* id:{"gpt-6-sol-audio","gpt-6.2-future","gpt-image-1","text-embedding-3-small","ft:gpt-4.1:unknown","gpt-4.1-2099-01-01"})
                rejects([&]{bind_provider_model_policy(policy,selected,id,false);});
            auto wrong=selected;wrong.wire=wire==ProviderWire::responses?ProviderWire::chat_completions:ProviderWire::responses;rejects([&]{bind_provider_model_policy(policy,wrong,"gpt-4.1",false);});
            auto invalid=policy;invalid.models.at("gpt-4.1").tools=static_cast<Capability>(99);rejects([&]{validate_provider_model_policy(invalid);});
            invalid=policy;invalid.models.at("gpt-4.1").tool_reasoning_effort=ReasoningEffort::max;rejects([&]{validate_provider_model_policy(invalid);});
            auto declared=policy;declared.models.emplace("fixture-explicit-deployment",declared.models.at("gpt-4.1"));
            validate_provider_model_policy(declared);require(bind_provider_model_policy(declared,selected,"fixture-explicit-deployment",true).model=="fixture-explicit-deployment","Trusted backend declarations support custom identities without guessing from names");
        }
        ChatProviderConfig config{"https://provider.example.invalid/v1/chat/completions","configured-deployment"};
        ModelRequest request{{{MessageRole::user,"A real caller's text"}}};
        auto plain=Json::parse(serialize_chat_request(config,request));
        require(plain["stream"]==true && plain["n"]==1 && plain["model"]==config.model,"Wire request configuration");
        require(plain["messages"][0]["content"]==request.messages[0].content,"Caller content must survive");
        require(!plain.contains("tools") && !plain.contains("stream_options"),"No unrequested feature options");
        require(!plain.contains("reasoning_effort"),"Omitted reasoning policy must stay omitted");
        config.reasoning_effort=ReasoningEffort::medium;rejects([&]{serialize_chat_request(config,request);});
        config.reasoning=Capability::unsupported;rejects([&]{serialize_chat_request(config,request);});
        config.reasoning=Capability::supported;
        for(const auto& [effort,name]:std::initializer_list<std::pair<ReasoningEffort,const char*>>{{ReasoningEffort::none,"none"},{ReasoningEffort::minimal,"minimal"},{ReasoningEffort::low,"low"},{ReasoningEffort::medium,"medium"},{ReasoningEffort::high,"high"},{ReasoningEffort::xhigh,"xhigh"},{ReasoningEffort::max,"max"}}){config.reasoning_effort=effort;require(Json::parse(serialize_chat_request(config,request))["reasoning_effort"]==name,"Explicit effort must survive native serialization");}
        config.reasoning_effort=static_cast<ReasoningEffort>(999);rejects([&]{serialize_chat_request(config,request);});config.reasoning_effort.reset();
        request.tools={{"read_file","Read permitted files",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"}};
        rejects([&]{serialize_chat_request(config,request);});
        config.tools=Capability::supported;
        auto tools=Json::parse(serialize_chat_request(config,request));
        require(tools["tools"][0]["function"]["parameters"]["required"][0]=="path","Tool schema must remain intact");
        request.include_usage=true;rejects([&]{serialize_chat_request(config,request);});
        config.stream_usage=Capability::supported;
        request.max_output_tokens=2048;rejects([&]{serialize_chat_request(config,request);});
        config.output_limit=Capability::supported;
        require(Json::parse(serialize_chat_request(config,request))["max_completion_tokens"]==2048,"Explicit output token limit");
        request.messages.push_back({MessageRole::assistant,"",{{"call-a","read_file",R"({"path":"README"})"},{"call-b","read_file",R"({"path":"LICENSE"})"}}});
        rejects([&]{serialize_chat_request(config,request);});
        request.messages.push_back({MessageRole::tool,"LICENSE content",{},"call-b"});
        rejects([&]{serialize_chat_request(config,request);});
        request.messages.push_back({MessageRole::tool,"README content",{},"call-a"});
        auto continued=Json::parse(serialize_chat_request(config,request));
        require(continued["messages"][2]["tool_call_id"]=="call-b" && continued["messages"][3]["tool_call_id"]=="call-a","Parallel results must keep their call identities");
        for(int condition=0;condition<9;++condition) {
            auto invalid=request;
            switch(condition) {
            case 0:invalid.messages[2].tool_call_id="unknown";break;
            case 1:invalid.messages[2].role=MessageRole::user;invalid.messages[2].tool_call_id.clear();break;
            case 2:invalid.messages[1].tool_calls[1].id="call-a";break;
            case 3:invalid.messages[1].role=MessageRole::user;break;
            case 4:invalid.tools[0].input_schema_json="malformed-private-marker";break;
            case 5:invalid.tools.push_back(invalid.tools[0]);break;
            case 6:invalid.max_output_tokens=0;break;
            case 7:invalid.messages[0].content=std::string("\xff",1);break;
            case 8:invalid.messages[1].tool_calls[0].arguments_json="[]";break;
            }
            rejects([&]{serialize_chat_request(config,invalid);});
        }
        auto unsupported=config;unsupported.tools=Capability::unsupported;
        rejects([&]{serialize_chat_request(unsupported,request);});
        auto refused=request;refused.messages.push_back({MessageRole::assistant,"",{}, {},"Actual refusal text"});
        require(Json::parse(serialize_chat_request(config,refused))["messages"].back()["refusal"]=="Actual refusal text","Refusal provenance must be preserved");
        refused.messages.back().role=MessageRole::user;
        rejects([&]{serialize_chat_request(config,refused);});
        auto oversized=request;
        oversized.messages.clear();
        for(int i=0;i<3;++i) oversized.messages.push_back({MessageRole::user,std::string(4*1024*1024,'x')});
        rejects([&]{serialize_chat_request(config,oversized);});
        std::cout<<"Native model request contracts passed; no live provider was called\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
