#include "agentflow/model_stream.hpp"
#include <iostream>

using namespace agentflow;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Function> void rejects(Function action) {
    try {action();} catch(const ModelProtocolError&) {return;} throw std::runtime_error("Expected protocol rejection did not occur");
}
// Synthetic protocol fixtures. These are not model/provider execution evidence.
const std::string tools=
    "\xef\xbb\xbf: heartbeat\r\n\r\n"
    "data: {\"id\":\"response\",\"model\":\"fixture\",\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"content\":\"Hi \"}}]}\r\n\r\n"
    "data: {\"id\":\"response\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"\xe4\xb8\xad\",\"reasoning_content\":\"extension\",\"tool_calls\":[{\"index\":1,\"id\":\"call-b\",\"type\":\"function\",\"function\":{\"name\":\"read\",\"arguments\":\"{\\\"path\\\":\"}},{\"index\":0,\"id\":\"call-a\",\"type\":\"function\",\"function\":{\"name\":\"search\",\"arguments\":\"{\\\"query\\\":\\\"x\\\"\"}}]}}]}\n\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"}\"}},{\"index\":1,\"function\":{\"arguments\":\"\\\"README\\\"}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\n"
    "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":4,\"completion_tokens\":8,\"total_tokens\":12}}\n\n"
    "data: [DONE]\n\n";
std::string stop_chunk=R"({"choices":[{"index":0,"delta":{"content":"answer"},"finish_reason":"stop"}]})";
std::string framed(const std::string& chunk) {return "data: "+chunk+"\n\ndata: [DONE]\n\n";}
void check_tools(const ModelCompletion& result) {
    require(result.content=="Hi \xe4\xb8\xad","UTF-8 bytes must survive arbitrary boundaries");
    require(result.finish_reason=="tool_calls" && result.tool_calls.size()==2,"Parallel tool calls must remain distinct");
    require(result.tool_calls[0].id=="call-a" && result.tool_calls[0].name=="search" && result.tool_calls[0].arguments_json==R"({"query":"x"})","First indexed tool reconstruction");
    require(result.tool_calls[1].id=="call-b" && result.tool_calls[1].arguments_json==R"({"path":"README"})","Second indexed tool reconstruction");
    require(result.usage_json==R"({"completion_tokens":8,"prompt_tokens":4,"total_tokens":12})","Final usage chunk must survive empty choices");
}
}
int main() {
    try {
        for(std::size_t boundary=0;boundary<=tools.size();++boundary) {
            std::vector<ModelEvent> events;ChatCompletionStream stream([&](const ModelEvent& event){events.push_back(event);});
            stream.feed(std::string_view(tools).substr(0,boundary));stream.feed(std::string_view(tools).substr(boundary));
            check_tools(stream.finish());
            bool extension=false;for(const auto& event:events) if(event.kind=="model.extension") extension=true;
            require(extension,"Provider extensions must be preserved");require(events.back().kind=="model.done","Done event ordering");
        }
        {
            ChatCompletionStream stream([](const ModelEvent&){});
            for(char byte:tools) stream.feed(std::string_view(&byte,1));check_tools(stream.finish());
        }
        for(const auto& malformed:{
            std::string("data: [DONE]\n\n"),
            framed(R"({"error":{"message":"untrusted provider error"}})"),
            framed(R"({"choices":[{"index":1,"delta":{},"finish_reason":"stop"}]})"),
            framed(R"({"choices":[{"index":0,"delta":{},"finish_reason":"tool_calls"}]})"),
            framed(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"c","function":{"name":"f","arguments":"{"}}]},"finish_reason":"tool_calls"}]})"),
            framed(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":64}]},"finish_reason":"tool_calls"}]})"),
            framed(R"({"choices":[{"index":0,"delta":{"content":12},"finish_reason":"stop"}]})"),
            framed(R"({"choices":[{"index":0,"delta":{},"finish_reason":"unknown"}]})"),
            framed(R"({"choices":[{"index":0,"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":-1}})"),
            std::string("data: not json\n\n"),
            framed(stop_chunk)+"data: {}\n\n"}) {
            ChatCompletionStream stream([](const ModelEvent&){});
            rejects([&]{stream.feed(malformed);stream.finish();});
            rejects([&]{stream.finish();});
        }
        {
            ChatCompletionStream stream([](const ModelEvent&){});
            stream.feed("data: "+stop_chunk+"\n\n");rejects([&]{stream.finish();});
        }
        {
            ChatCompletionStream stream([](const ModelEvent&){});
            stream.feed("data: {\n");stream.feed("data: \"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n");
            require(stream.finish().finish_reason=="stop","SSE multiline data must join with newlines");
        }
        {
            ChatCompletionStream stream([](const ModelEvent&){});
            stream.feed(framed(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"c","function":{"name":"f","arguments":"{"}}]},"finish_reason":"length"}]})"));
            require(stream.finish().tool_calls.empty(),"Truncated tool fragments must never be executable calls");
        }
        {
            ChatCompletionStream stream([](const ModelEvent&){});
            rejects([&]{stream.feed(std::string(1024*1024+1,'x'));});
        }
        {
            ChatCompletionStream stream([](const ModelEvent&){throw std::runtime_error("Consumer stopped");});
            bool propagated=false;
            try {stream.feed(framed(stop_chunk));} catch(const std::runtime_error&) {propagated=true;}
            require(propagated,"Consumer errors must abort the stream");rejects([&]{stream.finish();});
        }
        std::cout<<"Synthetic model-stream protocol contracts passed; no live provider was called\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
