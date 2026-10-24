#include "agentflow/remote_connection_client.hpp"
#include "agentflow/connection_profiles.hpp"
#include "agentflow/http_stream_transport.hpp"
#include <charconv>

namespace agentflow {
namespace {
void endpoint_origin(const std::string& value,RemoteTransportPolicy policy){
    if(value.starts_with("https://")){validate_remote_connection_origin(value);return;}
    constexpr std::string_view prefix="http://127.0.0.1:";
    if(policy!=RemoteTransportPolicy::isolated_loopback_test||!value.starts_with(prefix))throw std::invalid_argument("Remote negotiation requires verified HTTPS");
    const auto port=std::string_view(value).substr(prefix.size());unsigned parsed=0;const auto result=std::from_chars(port.data(),port.data()+port.size(),parsed);
    if(result.ec!=std::errc{}||result.ptr!=port.data()+port.size()||parsed==0||parsed>65535)throw std::invalid_argument("Invalid isolated remote test origin");
}
std::int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
}
RemoteNegotiatedGrant negotiate_remote_connection(const std::string& origin,const SecretBytes& credential,RemoteNegotiationRequirements requirements,std::chrono::milliseconds timeout,RemoteTransportPolicy policy,std::stop_token stop){
    endpoint_origin(origin,policy);
    if(timeout.count()<=0||timeout>std::chrono::minutes(2))throw std::invalid_argument("Invalid remote negotiation timeout");
    if(credential.view().empty()||credential.view().size()>8192)throw std::invalid_argument("Invalid remote credential");
    for(const auto byte:credential.view())if(byte<33||byte>126)throw std::invalid_argument("Invalid remote credential");
    if(requirements.supported_versions!=std::vector<std::int64_t>{1})throw std::invalid_argument("Remote connector supports only protocol v1");
    requirements.now_unix_seconds=now();HttpStreamRequest request;request.url=origin+"/xmind/v1/negotiate";request.body=encode_remote_negotiation_request(requirements);request.deadline=timeout;request.idle_timeout=timeout;
    if(stop.stop_requested())throw TransportCancelled("Remote negotiation cancelled before send");
    const auto response=post_json(request,&credential,65536,stop);
    requirements.now_unix_seconds=now();return validate_remote_negotiation(response,requirements);
}
}
