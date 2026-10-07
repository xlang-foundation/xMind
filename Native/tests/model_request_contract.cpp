#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <iostream>

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
        ChatProviderConfig config{"https://provider.example.invalid/v1/chat/completions","configured-deployment"};
        ModelRequest request{{{MessageRole::user,"A real caller's text"}}};
        auto plain=Json::parse(serialize_chat_request(config,request));
        require(plain["stream"]==true && plain["n"]==1 && plain["model"]==config.model,"Wire request configuration");
        require(plain["messages"][0]["content"]==request.messages[0].content,"Caller content must survive");
        require(!plain.contains("tools") && !plain.contains("stream_options"),"No unrequested feature options");
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
