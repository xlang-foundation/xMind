#include "agentflow/model_provider.hpp"
#include "agentflow/responses_stream.hpp"
#include <set>

namespace agentflow {
ModelCompletion complete_chat(const ChatProviderConfig& config,const ModelRequest& request,
    const SecretBytes* bearer,ChatCompletionStream::Sink sink,std::stop_token cancel) {
    const auto body=serialize_chat_request(config,request);
    if(!sink) throw std::invalid_argument("Model event sink is required");
    ChatCompletionStream stream([&](const ModelEvent& event){if(event.kind!="model.done") sink(event);});
    post_event_stream({config.endpoint,body,config.deadline,config.idle_timeout},bearer,
        [&](std::string_view bytes){stream.feed(bytes);},cancel);
    auto result=stream.finish();
    std::set<std::string> names;for(const auto& tool:request.tools) names.insert(tool.name);
    for(const auto& call:result.tool_calls) if(!names.contains(call.name)) throw ModelProtocolError("Provider requested a tool outside this request");
    sink({"model.done","{}"});
    // The agent/tool layer must validate each argument against its schema and
    // authorize execution. This provider adapter never invokes a tool.
    return result;
}
ModelCompletion complete_model(const ChatProviderConfig& config,const ModelRequest& request,const SecretBytes* bearer,ChatCompletionStream::Sink sink,std::stop_token cancel){
    if(config.wire==ProviderWire::chat_completions)return complete_chat(config,request,bearer,std::move(sink),cancel);
    if(config.wire!=ProviderWire::responses)throw std::invalid_argument("Unsupported provider wire");
    const auto body=serialize_responses_request(config,request);if(!sink)throw std::invalid_argument("Model event sink is required");
    ResponsesStream stream([&](const ModelEvent& event){if(event.kind!="model.done")sink(event);});
    post_event_stream({config.endpoint,body,config.deadline,config.idle_timeout},bearer,[&](std::string_view bytes){stream.feed(bytes);},cancel);
    auto result=stream.finish();std::set<std::string> names;for(const auto& tool:request.tools)names.insert(tool.name);for(const auto& call:result.tool_calls)if(!names.contains(call.name))throw ModelProtocolError("Provider requested a tool outside this request");sink({"model.done","{}"});return result;
}
}
