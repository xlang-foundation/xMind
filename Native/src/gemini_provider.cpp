#include "agentflow/gemini_provider.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
std::string gemini_stream_endpoint(const GeminiProviderConfig& config){
    auto base=config.endpoint_base;auto model=std::string_view(config.model);
    if(model.starts_with("models/"))model.remove_prefix(7);
    if(model.empty()||model.size()>128||model=="."||model==".."||model.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string_view::npos)throw std::invalid_argument("Invalid Gemini model resource");
    if(base.empty()||base.size()>4096||base.find_first_of("?#@")!=std::string::npos)throw std::invalid_argument("Invalid Gemini endpoint base");
    for(unsigned char byte:base)if(byte<=32||byte==127)throw std::invalid_argument("Invalid Gemini endpoint base");
    if(!base.starts_with("https://")&&!base.starts_with("http://"))throw std::invalid_argument("Invalid Gemini endpoint scheme");
    while(base.ends_with('/'))base.pop_back();
    if(base=="http:"||base=="https:")throw std::invalid_argument("Missing Gemini endpoint host");
    return base+"/models/"+std::string(model)+":streamGenerateContent?alt=sse";
}
GeminiCompletion complete_gemini(const GeminiProviderConfig& config,const GeminiRequest& request,
    const SecretBytes* api_key,ChatCompletionStream::Sink sink,std::stop_token cancel){
    if(!api_key||api_key->view().empty())throw std::invalid_argument("Gemini requires a backend credential");
    if(!sink)throw std::invalid_argument("Model event sink is required");
    const auto endpoint=gemini_stream_endpoint(config);const auto body=serialize_gemini_request(request);
    GeminiStream stream([&](const ModelEvent& event){if(event.kind!="model.done"&&event.kind!="model.finish")sink(event);});
    post_event_stream({endpoint,body,config.deadline,config.idle_timeout,CredentialHeader::x_goog_api_key},api_key,[&](std::string_view bytes){stream.feed(bytes);},cancel);
    auto result=stream.finish();std::set<std::string> offered;for(const auto& tool:request.tools)offered.insert(tool.name);
    for(const auto& call:result.function_calls)if(!offered.contains(call.name))throw ModelProtocolError("Provider requested a tool outside this request");
    sink({"model.finish",nlohmann::json{{"reason",result.finish_reason},{"provider_reason",result.provider_finish_reason}}.dump()});
    sink({"model.done","{}"});return result;
}
}
