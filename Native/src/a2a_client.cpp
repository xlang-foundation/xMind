#include "agentflow/a2a_client.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <climits>
#include <mutex>
#include <random>
#include <sstream>

namespace agentflow {
namespace {
using Json=nlohmann::json;
struct UrlOrigin {std::string value,scheme,authority;};
UrlOrigin origin(std::string_view url){
    if(url.empty()||url.size()>8192)throw std::invalid_argument("Invalid A2A URL");
    for(unsigned char c:url)if(c<=32||c==127||c=='#'||c=='?'||c=='\\')throw std::invalid_argument("Invalid A2A URL");
    const auto colon=url.find("://");if(colon==std::string_view::npos)throw std::invalid_argument("A2A URL must be absolute HTTP(S)");
    UrlOrigin result;result.scheme=std::string(url.substr(0,colon));std::transform(result.scheme.begin(),result.scheme.end(),result.scheme.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(result.scheme!="https"&&result.scheme!="http")throw std::invalid_argument("A2A URL must use HTTP(S)");
    const auto begin=colon+3,end=url.find_first_of("/?",begin);const auto authority=url.substr(begin,end==std::string_view::npos?url.size()-begin:end-begin);
    if(authority.empty()||authority.find('@')!=std::string_view::npos||authority.find('%')!=std::string_view::npos)throw std::invalid_argument("A2A URL authority is invalid");
    result.authority=std::string(authority);std::transform(result.authority.begin(),result.authority.end(),result.authority.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    for(unsigned char c:result.authority)if(!(std::isalnum(c)||c=='.'||c=='-'||c==':'||c=='['||c==']'))throw std::invalid_argument("A2A URL authority is invalid");
    std::string host,port;bool ipv6=result.authority.front()=='[';
    if(ipv6){const auto close=result.authority.find(']');if(close==std::string::npos||close==1)throw std::invalid_argument("A2A IPv6 URL is invalid");host=result.authority.substr(0,close+1);if(close+1<result.authority.size()){if(result.authority[close+1]!=':')throw std::invalid_argument("A2A URL authority is invalid");port=result.authority.substr(close+2);if(port.empty())throw std::invalid_argument("A2A URL port is invalid");}}
    else {const auto first=result.authority.find(':'),last=result.authority.rfind(':');if(first!=std::string::npos&&first!=last)throw std::invalid_argument("A2A IPv6 URLs must use brackets");host=first==std::string::npos?result.authority:result.authority.substr(0,first);if(first!=std::string::npos){port=result.authority.substr(first+1);if(port.empty())throw std::invalid_argument("A2A URL port is invalid");}}
    if(host.empty())throw std::invalid_argument("A2A URL host is empty");
    unsigned value=result.scheme=="https"?443:80;if(!port.empty()){if(!std::all_of(port.begin(),port.end(),[](unsigned char c){return c>='0'&&c<='9';}))throw std::invalid_argument("A2A URL port is invalid");try{value=static_cast<unsigned>(std::stoul(port));}catch(...){throw std::invalid_argument("A2A URL port is invalid");}if(value==0||value>65535)throw std::invalid_argument("A2A URL port is invalid");}
    result.value=result.scheme+"://"+host+":"+std::to_string(value);return result;
}
std::string card_path(std::string_view url){
    const auto parsed=origin(url);const auto scheme=url.find("://");const auto path=url.find('/',scheme+3);
    if(path==std::string_view::npos)return parsed.value+"/.well-known/agent-card.json";
    const auto configured=url.substr(path);if(configured=="/")return parsed.value+"/.well-known/agent-card.json";
    return std::string(url);
}
std::string random_id(){std::random_device source;std::ostringstream out;out<<std::hex;for(int i=0;i<4;++i){if(i)out<<'-';out<<static_cast<std::uint32_t>(source());}return out.str();}
std::string bounded_string(const Json& object,const char* key,std::size_t limit,bool required=false){
    if(!object.contains(key)){if(required)throw std::invalid_argument("AgentCard is missing required metadata");return {};}
    if(!object[key].is_string())throw std::invalid_argument("AgentCard metadata has an invalid type");auto value=object[key].get<std::string>();
    if(value.size()>limit||value.find('\0')!=std::string::npos||(required&&value.empty()))throw std::invalid_argument("AgentCard metadata exceeds its limits");return value;
}
bool bearer_required(const Json& card,bool legacy){
    const char* requirements_key=legacy?"security":"securityRequirements";
    if(!card.contains(requirements_key))return false;
    const auto& requirements=card[requirements_key];if(!requirements.is_array())throw std::invalid_argument("Invalid AgentCard security requirements");
    if(requirements.empty())return false;
    const auto& schemes=card.value("securitySchemes",Json::object());if(!schemes.is_object())throw std::invalid_argument("Invalid AgentCard security schemes");
    bool no_auth_alternative=false,bearer_alternative=false;
    for(const auto& requirement:requirements){
        const Json* selected=&requirement;
        if(!legacy){if(!requirement.is_object()||!requirement.contains("schemes")||!requirement["schemes"].is_object())throw std::invalid_argument("Invalid AgentCard security requirement");selected=&requirement["schemes"];}
        else if(!requirement.is_object())throw std::invalid_argument("Invalid legacy AgentCard security requirement");
        if(selected->empty()){no_auth_alternative=true;continue;}
        bool supported=true;
        for(auto item=selected->begin();item!=selected->end();++item){
            if(legacy){if(!item.value().is_array()||!std::all_of(item.value().begin(),item.value().end(),[](const auto& scope){return scope.is_string();}))throw std::invalid_argument("Invalid legacy AgentCard security scope");}
            else {const auto& scopes=item.value();if(!scopes.is_object()||!scopes.contains("list")||!scopes["list"].is_array()||!std::all_of(scopes["list"].begin(),scopes["list"].end(),[](const auto& scope){return scope.is_string();}))throw std::invalid_argument("Invalid AgentCard security scope");}
            if(!schemes.contains(item.key())){supported=false;continue;}
            const auto& scheme=schemes[item.key()];bool bearer=false;
            if(legacy){bearer=scheme.is_object()&&scheme.value("type",std::string{})=="http"&&scheme.value("scheme",std::string{})=="bearer";}
            else if(scheme.is_object()&&scheme.contains("httpAuthSecurityScheme")&&scheme["httpAuthSecurityScheme"].is_object()){
                const auto& http=scheme["httpAuthSecurityScheme"];if(http.contains("scheme")&&http["scheme"].is_string()){auto value=http["scheme"].get<std::string>();std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});bearer=value=="bearer";}
            }
            if(!bearer)supported=false;
        }
        if(supported)bearer_alternative=true;
    }
    if(no_auth_alternative)return false;
    if(bearer_alternative)return true;
    throw std::invalid_argument("AgentCard requires an unsupported authentication scheme");
}
}
struct A2aJsonRpcClient::Impl {
    std::string card_url;
    std::optional<SecretBytes> bearer;
    std::optional<A2aRemoteAgentInfo> selected;
    bool send_bearer=false;
    std::uint64_t sequence=0;
    mutable std::mutex mutex;
    Impl(std::string url,std::optional<SecretBytes> credential):card_url(std::move(url)),bearer(std::move(credential)){(void)origin(card_url);}
    HttpStreamRequest request(std::string url,std::chrono::milliseconds timeout,std::string version={})const{
        if(timeout.count()<=0||timeout.count()>600000)throw std::invalid_argument("A2A request timeout is out of range");
        HttpStreamRequest value;value.url=std::move(url);value.deadline=timeout;value.idle_timeout=timeout;value.protocol=ProviderHttpProtocol::a2a_jsonrpc;value.a2a_version=std::move(version);value.observe_response=[expected=value.a2a_version](int status,const std::string& media,const std::string& received){if(status!=200||media!="application/json")throw TransportError("A2A JSON-RPC peer returned an invalid HTTP response");if(!received.empty()&&!expected.empty()&&received!=expected)throw TransportError("A2A peer response version does not match the request");};return value;
    }
    std::string call(std::string method,std::string_view params,std::chrono::milliseconds timeout,std::stop_token cancel,std::function<void()> on_sending={}){
        std::lock_guard lock(mutex);if(!selected)throw std::logic_error("A2A client must discover its peer before use");
        if(params.empty()||params.size()>256*1024)throw std::invalid_argument("A2A request parameters exceed limits");Json parsed=Json::parse(params);if(!parsed.is_object())throw std::invalid_argument("A2A request parameters must be an object");
        if(method=="send")method=selected->protocol_version=="1.0"?"SendMessage":"message/send";
        else if(method=="get")method=selected->protocol_version=="1.0"?"GetTask":"tasks/get";
        else if(method=="cancel")method=selected->protocol_version=="1.0"?"CancelTask":"tasks/cancel";
        else throw std::logic_error("Unsupported native A2A operation");
        const auto id="xmind-"+std::to_string(++sequence)+"-"+random_id();Json body={{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",std::move(parsed)}};
        auto transport=request(selected->endpoint,timeout,selected->protocol_version=="1.0"?"1.0":"0.3");transport.body=body.dump();transport.on_sending=std::move(on_sending);
        const auto bytes=post_json(transport,send_bearer&&bearer?&*bearer:nullptr,1024*1024,cancel);Json response;
        try{response=Json::parse(bytes);}catch(const Json::exception&){throw TransportError("A2A peer returned invalid JSON-RPC data");}
        if(!response.is_object()||response.value("jsonrpc",std::string{})!="2.0"||!response.contains("id")||response["id"]!=id||response.contains("result")==response.contains("error"))throw TransportError("A2A peer returned a mismatched JSON-RPC response");
        if(response.contains("error")){const auto& error=response["error"];if(!error.is_object()||!error.contains("code")||!error["code"].is_number_integer())throw TransportError("A2A peer returned an invalid JSON-RPC error");int code=0;try{if(error["code"].is_number_unsigned()){const auto value=error["code"].get<std::uint64_t>();if(value>static_cast<std::uint64_t>(INT_MAX))throw TransportError("A2A peer returned an invalid JSON-RPC error");code=static_cast<int>(value);}else{const auto value=error["code"].get<std::int64_t>();if(value<INT_MIN||value>INT_MAX)throw TransportError("A2A peer returned an invalid JSON-RPC error");code=static_cast<int>(value);}}catch(const TransportError&){throw;}catch(const std::exception&){throw TransportError("A2A peer returned an invalid JSON-RPC error");}throw A2aRemoteError(code);}
        return response["result"].dump();
    }
};
A2aJsonRpcClient::A2aJsonRpcClient(std::string card_url,std::optional<SecretBytes> bearer):impl_(std::make_unique<Impl>(std::move(card_url),std::move(bearer))){}
A2aJsonRpcClient::~A2aJsonRpcClient()=default;
A2aRemoteAgentInfo A2aJsonRpcClient::discover(std::chrono::milliseconds timeout,std::stop_token cancel){
    auto& state=*impl_;std::lock_guard lock(state.mutex);state.selected.reset();state.send_bearer=false;
    const auto root=origin(state.card_url).value;std::string bytes;bool requested_v1=true;
    auto modern=state.request(card_path(state.card_url),timeout,"1.0");
    try{bytes=get_json(modern,state.bearer?&*state.bearer:nullptr,cancel);}
    catch(const ProviderHttpError& error){const bool legacy_supported=error.json_rpc_code==-32009||error.a2a_supports_legacy_03;if(error.status!=400||!error.a2a_version_not_supported||!legacy_supported)throw;requested_v1=false;auto legacy=state.request(card_path(state.card_url),timeout,"0.3");bytes=get_json(legacy,state.bearer?&*state.bearer:nullptr,cancel);}
    Json card;try{card=Json::parse(bytes);}catch(const Json::exception&){throw TransportError("A2A peer returned an invalid AgentCard");}
    if(!card.is_object())throw TransportError("A2A peer returned an invalid AgentCard");A2aRemoteAgentInfo info;
    try{
        state.send_bearer=bearer_required(card,!requested_v1);
        if(state.send_bearer&&!state.bearer)throw TransportError("A2A peer requires bearer credentials");
        info.name=bounded_string(card,"name",256,true);info.description=bounded_string(card,"description",8192,true);info.agent_version=bounded_string(card,"version",128,true);
        if(card.contains("skills")){if(!card["skills"].is_array()||card["skills"].size()>256)throw std::invalid_argument("Invalid AgentCard skills");info.skills_json=card["skills"].dump();}else info.skills_json="[]";
        if(card.contains("supportedInterfaces")){
            const auto& interfaces=card["supportedInterfaces"];if(!interfaces.is_array()||interfaces.empty()||interfaces.size()>64)throw std::invalid_argument("Invalid AgentCard interfaces");
            std::optional<std::pair<std::string,std::string>> selected;
            const std::string requested=requested_v1?"1.0":"0.3";
            for(const auto& item:interfaces){if(!item.is_object()||!item.contains("protocolBinding")||!item["protocolBinding"].is_string()||!item.contains("protocolVersion")||!item["protocolVersion"].is_string()||!item.contains("url")||!item["url"].is_string())throw std::invalid_argument("Invalid AgentCard interface");const auto binding=item["protocolBinding"].get<std::string>(),version=item["protocolVersion"].get<std::string>(),url=item["url"].get<std::string>();if(binding!="JSONRPC"||version!=requested)continue;if(origin(url).value!=root)continue;if(!selected)selected=std::pair{version,url};}
            if(!selected)throw std::invalid_argument("AgentCard has no supported same-origin JSON-RPC interface");info.protocol_version=selected->first;info.endpoint=selected->second;
        }else{
            if(!card.contains("protocolVersion")||!card["protocolVersion"].is_string()||card["protocolVersion"]!="0.3.0"||!card.contains("preferredTransport")||!card["preferredTransport"].is_string()||card["preferredTransport"]!="JSONRPC"||!card.contains("url")||!card["url"].is_string())throw std::invalid_argument("AgentCard does not advertise a supported A2A interface");
            info.protocol_version="0.3";info.endpoint=card["url"].get<std::string>();if(origin(info.endpoint).value!=root)throw std::invalid_argument("AgentCard interface changed the configured origin");
        }
        // A versioned discovery request cannot downgrade to an interface that
        // the peer did not advertise as a supported interface.
        if(requested_v1&&info.protocol_version!="1.0"&&!card.contains("supportedInterfaces"))throw std::invalid_argument("A2A peer did not return a version-1 AgentCard");
    }catch(const TransportError&){throw;}catch(const std::exception&){throw TransportError("A2A peer returned an invalid or unsupported AgentCard");}
    state.selected=info;state.sequence=0;return info;
}
std::string A2aJsonRpcClient::send_message(std::string_view params,std::chrono::milliseconds timeout,std::stop_token cancel,std::function<void()> on_sending){return impl_->call("send",params,timeout,cancel,std::move(on_sending));}
std::string A2aJsonRpcClient::get_task(std::string_view params,std::chrono::milliseconds timeout,std::stop_token cancel){return impl_->call("get",params,timeout,cancel);}
std::string A2aJsonRpcClient::cancel_task(std::string_view params,std::chrono::milliseconds timeout,std::stop_token cancel){return impl_->call("cancel",params,timeout,cancel);}
bool A2aJsonRpcClient::ready()const{std::lock_guard lock(impl_->mutex);return impl_->selected.has_value();}
A2aRemoteAgentInfo A2aJsonRpcClient::remote()const{std::lock_guard lock(impl_->mutex);if(!impl_->selected)throw std::logic_error("A2A client has not discovered a peer");return *impl_->selected;}
}
