#include "agentflow/model_provider.hpp"
#include "agentflow/responses_stream.hpp"
#include "agentflow/anthropic_stream.hpp"
#include "agentflow/gemini_provider.hpp"
#include "agentflow/gemini_history.hpp"
#include "nlohmann/json.hpp"
#include <random>
#include <sstream>
#include <iomanip>
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
    if(config.wire==ProviderWire::gemini_generate_content){
        if(!sink)throw std::invalid_argument("Model event sink is required");
        if(config.reasoning_effort)throw std::invalid_argument("Gemini reasoning controls are not implemented");
        if(request.include_usage&&config.stream_usage!=Capability::supported)throw std::invalid_argument("Gemini usage capability is unknown or unsupported");
        if(request.max_output_tokens&&config.output_limit!=Capability::supported)throw std::invalid_argument("Gemini output capability is unknown or unsupported");
        const auto adapted=gemini_model_request(request,config.tools);
        const auto wire=complete_gemini({config.endpoint,config.model,config.deadline,config.idle_timeout},adapted,bearer,[&](const ModelEvent& event){
            if(event.kind=="model.finish"||event.kind=="model.done")return;
            if(event.kind=="model.usage")sink({event.kind,gemini_model_usage(event.json)});else sink(event);
        },cancel);
        const auto result=gemini_model_completion(wire,[]{std::random_device random;std::ostringstream id;id<<"gm_"<<std::hex<<std::setfill('0');for(int index=0;index<4;++index)id<<std::setw(8)<<random();return id.str();});
        for(std::size_t index=0;index<result.tool_calls.size();++index){const auto& call=result.tool_calls[index];sink({"model.tool_delta",nlohmann::json{{"index",index},{"id",call.id},{"name",call.name},{"arguments",call.arguments_json}}.dump()});}
        sink({"model.finish",nlohmann::json{{"reason",result.finish_reason},{"provider_reason",wire.provider_finish_reason}}.dump()});sink({"model.done","{}"});return result;
    }
    if(config.wire==ProviderWire::anthropic_messages){
        const auto body=serialize_anthropic_request(config,request);
        if(!sink)throw std::invalid_argument("Model event sink is required");
        if(!bearer)throw std::invalid_argument("Claude requires a backend credential");
        AnthropicStream stream([&](const ModelEvent& event){if(event.kind!="model.done")sink(event);});
        post_event_stream({config.endpoint,body,config.deadline,config.idle_timeout,CredentialHeader::x_api_key,ProviderHttpProtocol::anthropic},bearer,[&](std::string_view bytes){stream.feed(bytes);},cancel);
        auto result=stream.finish();
        std::set<std::string> names;for(const auto& tool:request.tools)names.insert(tool.name);
        for(const auto& call:result.tool_calls)if(!names.contains(call.name))throw ModelProtocolError("Provider requested a tool outside this request");
        sink({"model.done","{}"});return result;
    }
    if(config.wire!=ProviderWire::responses)throw std::invalid_argument("Unsupported provider wire");
    const auto body=serialize_responses_request(config,request);if(!sink)throw std::invalid_argument("Model event sink is required");
    ResponsesStream stream([&](const ModelEvent& event){if(event.kind!="model.done")sink(event);});
    post_event_stream({config.endpoint,body,config.deadline,config.idle_timeout},bearer,[&](std::string_view bytes){stream.feed(bytes);},cancel);
    auto result=stream.finish();std::set<std::string> names;for(const auto& tool:request.tools)names.insert(tool.name);for(const auto& call:result.tool_calls)if(!names.contains(call.name))throw ModelProtocolError("Provider requested a tool outside this request");sink({"model.done","{}"});return result;
}
}
