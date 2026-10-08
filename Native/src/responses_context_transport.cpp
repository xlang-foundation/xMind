#include "agentflow/responses_context.hpp"
#include "agentflow/model_provider.hpp"
#include "agentflow/http_stream_transport.hpp"

namespace agentflow {
namespace {
std::string endpoint(const ChatProviderConfig& config,const char* suffix){
    const auto& base=config.endpoint;
    // Endpoint routing is backend-owned. Do not accept another Responses route,
    // query/fragment injection, credentials, or encoded path interpretation.
    if(config.wire!=ProviderWire::responses||base.size()>16384||base.find_first_of("?#\\%")!=std::string::npos||!base.ends_with("/responses"))throw ResponsesContextError(ResponsesContextErrorCode::invalid_endpoint);
    const auto scheme=base.find("://");if(scheme==std::string::npos||(base.substr(0,scheme)!="https"&&base.substr(0,scheme)!="http"))throw ResponsesContextError(ResponsesContextErrorCode::invalid_endpoint);
    const auto begin=scheme+3,slash=base.find('/',begin);if(slash==std::string::npos||slash==begin||base.substr(begin,slash-begin).find('@')!=std::string::npos)throw ResponsesContextError(ResponsesContextErrorCode::invalid_endpoint);
    for(const auto c:base)if(static_cast<unsigned char>(c)<=0x20||static_cast<unsigned char>(c)>0x7e)throw ResponsesContextError(ResponsesContextErrorCode::invalid_endpoint);
    return base+suffix;
}
HttpStreamRequest transport_request(const ChatProviderConfig& config,std::string url,std::string body){return {std::move(url),std::move(body),config.deadline,config.idle_timeout,CredentialHeader::bearer,ProviderHttpProtocol::generic};}
}
ResponsesCompactionResult compact_responses_context(const ChatProviderConfig& config,const ModelRequest& request,const SecretBytes* bearer,std::stop_token cancel){
    const auto url=endpoint(config,"/compact");const auto body=serialize_responses_compaction_request(config,request);
    auto result=parse_responses_compaction_response(post_json(transport_request(config,url,body),bearer,responses_context_response_limit,cancel));
    validate_responses_compaction_result(config,request,result);return result;
}
ResponsesTokenCount count_responses_context(const ChatProviderConfig& config,const ModelRequest& request,const SecretBytes* bearer,std::stop_token cancel){
    const auto url=endpoint(config,"/input_tokens");const auto body=serialize_responses_input_tokens_request(config,request);
    return parse_responses_input_tokens_response(post_json(transport_request(config,url,body),bearer,65536,cancel));
}
}
