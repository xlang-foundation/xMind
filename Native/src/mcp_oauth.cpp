#include "agentflow/mcp_oauth.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/mcp_http_transport.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>
#include <utility>
namespace agentflow {
namespace {
using Json=nlohmann::json;
[[noreturn]] void invalid(){throw McpProtocolError("Invalid MCP OAuth discovery metadata");}
std::string lower(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;}
struct Uri {std::string origin,path;};
bool token_char(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||std::string_view("!#$%&'*+-.^_`|~").find(c)!=std::string_view::npos;}
void ows(std::string_view source,std::size_t& i){while(i<source.size()&&(source[i]==' '||source[i]=='\t'))++i;}
std::string token(std::string_view source,std::size_t& i){const auto start=i;while(i<source.size()&&token_char(source[i]))++i;if(i==start)invalid();return std::string(source.substr(start,i-start));}
Uri uri(std::string_view input,bool issuer=false) {
    try{validate_mcp_http_endpoint(input);}catch(const std::invalid_argument&){invalid();}
    if(input.size()<8 || lower(std::string(input.substr(0,8)))!="https://")invalid();
    if(issuer && input.find('?')!=std::string_view::npos)invalid();
    const auto start=input.find_first_of("/?",8);const auto query=input.find('?');
    const auto end=std::min(query==std::string_view::npos?input.size():query,input.size());
    Uri value;value.origin=input.substr(0,start==std::string_view::npos?input.size():start);
    if(start!=std::string_view::npos && input[start]=='/')value.path=input.substr(start,end-start);
    return value;
}
Json object(std::string_view source){if(source.size()>256*1024)invalid();return Json::parse(mcp_compact_object(source));}
std::string text(const Json& value,const char* key) {
    if(!value.contains(key)||!value[key].is_string())invalid();const auto result=value[key].get<std::string>();
    if(result.empty()||result.size()>8192||result.find('\0')!=std::string::npos)invalid();return result;
}
std::vector<std::string> list(const Json& value,const char* key,std::size_t limit,bool required=false,bool scope=false) {
    if(!value.contains(key)){if(required)invalid();return {};}
    const auto& raw=value[key];if(!raw.is_array()||raw.size()>limit||(required&&raw.empty()))invalid();
    std::vector<std::string> result;std::set<std::string> seen;
    for(const auto& item:raw){if(!item.is_string())invalid();auto entry=item.get<std::string>();if(entry.empty()||entry.size()>8192||entry.find('\0')!=std::string::npos||!seen.insert(entry).second)invalid();
        if(scope)for(unsigned char c:entry)if(c<0x21||c>0x7e||c=='"'||c=='\\')invalid();result.push_back(std::move(entry));}
    return result;
}
bool flag(const Json& value,const char* key){if(!value.contains(key))return false;if(!value[key].is_boolean())invalid();return value[key].get<bool>();}
std::string fetch(const std::string& url,McpDeadline deadline,std::stop_token cancel) {
    (void)uri(url);if(cancel.stop_requested())throw McpTransportCancelled("MCP OAuth discovery cancelled");
    const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());if(remaining.count()<=0)throw McpTransportTimeout("MCP OAuth discovery deadline exceeded");
    HttpStreamRequest request;request.url=url;request.deadline=std::min(remaining,std::chrono::milliseconds(600000));request.idle_timeout=request.deadline;
    try{return get_json(request,nullptr,cancel);}
    catch(const TransportCancelled&){throw McpTransportCancelled("MCP OAuth discovery cancelled");}
    catch(const TransportTimeout&){throw McpTransportTimeout("MCP OAuth discovery deadline exceeded");}
}
template<class Parse> auto discover(const std::vector<std::string>& urls,McpDeadline deadline,std::stop_token cancel,Parse parse) {
    for(const auto& url:urls) {
        try{return parse(fetch(url,deadline,cancel));}
        catch(const ProviderHttpError& error){if(error.status!=404)throw;}
    }
    throw McpTransportError("MCP OAuth metadata endpoints were not found");
}
}
std::optional<McpOAuthChallenge> mcp_oauth_bearer_challenge(std::string_view source) {
    if(source.size()>8192)invalid();for(unsigned char c:source)if(c!='\t'&&(c<0x20||c>0x7e))invalid();
    std::optional<McpOAuthChallenge> result;std::size_t i=0;
    while(i<source.size()) {
        ows(source,i);if(i<source.size()&&source[i]==','){++i;continue;}if(i==source.size())break;
        const auto scheme=lower(token(source,i));if(i<source.size()&&source[i]!=' '&&source[i]!='\t'&&source[i]!=',')invalid();ows(source,i);
        if(scheme!="bearer") {
            bool quoted=false,escaped=false;
            for(;i<source.size();++i){const char c=source[i];if(escaped){escaped=false;continue;}if(quoted&&c=='\\'){escaped=true;continue;}if(c=='"'){quoted=!quoted;continue;}
                if(!quoted&&c==','){auto next=i+1;ows(source,next);if(next==source.size()){i=next;break;}token(source,next);ows(source,next);if(next==source.size()||source[next]!='='){++i;break;}}}
            if(quoted||escaped)invalid();continue;
        }
        if(result)invalid();McpOAuthChallenge challenge;std::set<std::string> names;
        while(i<source.size()&&source[i]!=',') {
            const auto name=lower(token(source,i));if(!names.insert(name).second||names.size()>32)invalid();ows(source,i);if(i==source.size()||source[i++]!='=')invalid();ows(source,i);std::string value;
            if(i<source.size()&&source[i]=='"') {++i;bool closed=false;while(i<source.size()){char c=source[i++];if(c=='"'){closed=true;break;}if(c=='\\'){if(i==source.size())invalid();c=source[i++];}value+=c;}if(!closed)invalid();}
            else value=token(source,i);
            if(name=="resource_metadata"){(void)uri(value);challenge.metadata_url=value;}
            else if(name=="scope") {
                if(value.empty())invalid();std::size_t start=0;std::set<std::string> scopes;
                while(start<value.size()){const auto end=value.find(' ',start);auto scope=value.substr(start,end==std::string::npos?value.size()-start:end-start);if(scope.empty()||scopes.size()>=64)invalid();for(unsigned char c:scope)if(c<0x21||c>0x7e||c=='"'||c=='\\')invalid();if(scopes.insert(scope).second)challenge.scopes.push_back(std::move(scope));if(end==std::string::npos)break;start=end+1;if(start==value.size())invalid();}
            }else if(name=="error") {if(value!="invalid_request"&&value!="invalid_token"&&value!="insufficient_scope")invalid();challenge.error=value;}
            ows(source,i);if(i==source.size())break;if(source[i]!=',')invalid();auto next=i+1;ows(source,next);if(next==source.size()){i=next;break;}auto peek=next;token(source,peek);ows(source,peek);if(peek<source.size()&&source[peek]=='='){i=next;continue;}i=next;break;
        }
        result=std::move(challenge);
    }
    return result;
}
std::vector<std::string> mcp_oauth_resource_metadata_urls(std::string_view resource) {
    const auto value=uri(resource);std::vector<std::string> result;
    if(!value.path.empty()&&value.path!="/")result.push_back(value.origin+"/.well-known/oauth-protected-resource"+value.path);
    result.push_back(value.origin+"/.well-known/oauth-protected-resource");return result;
}
std::vector<std::string> mcp_oauth_authorization_metadata_urls(std::string_view issuer) {
    const auto value=uri(issuer,true);const auto path=value.path=="/"?std::string{}:value.path;
    std::vector<std::string> result{value.origin+"/.well-known/oauth-authorization-server"+path,value.origin+"/.well-known/openid-configuration"+path};
    if(!path.empty()){auto base=std::string(issuer);if(base.ends_with('/'))base.pop_back();const auto appended=base+"/.well-known/openid-configuration";if(std::find(result.begin(),result.end(),appended)==result.end())result.push_back(appended);}
    return result;
}
McpOAuthResourceMetadata mcp_oauth_resource_metadata(std::string_view source,std::string_view expected_resource) {
    (void)uri(expected_resource);const auto value=object(source);McpOAuthResourceMetadata result;
    result.resource=text(value,"resource");if(result.resource!=expected_resource)invalid();
    result.authorization_servers=list(value,"authorization_servers",16,true);
    for(const auto& issuer:result.authorization_servers)(void)uri(issuer,true);
    result.scopes=list(value,"scopes_supported",64,false,true);
    if(value.contains("bearer_methods_supported")){const auto methods=list(value,"bearer_methods_supported",16);if(std::find(methods.begin(),methods.end(),"header")==methods.end())invalid();}
    return result;
}
McpOAuthServerMetadata mcp_oauth_server_metadata(std::string_view source,std::string_view expected_issuer) {
    (void)uri(expected_issuer,true);const auto value=object(source);McpOAuthServerMetadata result;
    result.issuer=text(value,"issuer");if(result.issuer!=expected_issuer)invalid();
    result.authorization_endpoint=text(value,"authorization_endpoint");(void)uri(result.authorization_endpoint);
    result.token_endpoint=text(value,"token_endpoint");(void)uri(result.token_endpoint);
    if(value.contains("registration_endpoint")){result.registration_endpoint=text(value,"registration_endpoint");(void)uri(*result.registration_endpoint);}
    const auto pkce=list(value,"code_challenge_methods_supported",16,true);if(std::find(pkce.begin(),pkce.end(),"S256")==pkce.end())invalid();
    const auto response_types=list(value,"response_types_supported",16,true);if(std::find(response_types.begin(),response_types.end(),"code")==response_types.end())invalid();
    if(value.contains("grant_types_supported")){const auto grants=list(value,"grant_types_supported",16);if(std::find(grants.begin(),grants.end(),"authorization_code")==grants.end())invalid();}
    result.token_auth_methods=value.contains("token_endpoint_auth_methods_supported")?list(value,"token_endpoint_auth_methods_supported",16):std::vector<std::string>{"client_secret_basic"};
    result.scopes=list(value,"scopes_supported",64,false,true);result.response_issuer_required=flag(value,"authorization_response_iss_parameter_supported");result.client_id_metadata_supported=flag(value,"client_id_metadata_document_supported");return result;
}
McpOAuthDiscovery discover_mcp_oauth(std::string resource,std::optional<std::string> metadata_url,McpDeadline deadline,std::stop_token cancel,std::optional<std::string> preferred_issuer) {
    auto urls=mcp_oauth_resource_metadata_urls(resource);if(metadata_url){(void)uri(*metadata_url);urls={*metadata_url};}
    auto discovered=discover(urls,deadline,cancel,[&](const auto& source){return mcp_oauth_resource_metadata(source,resource);});
    const auto issuer=preferred_issuer.value_or(discovered.authorization_servers.front());
    if(std::find(discovered.authorization_servers.begin(),discovered.authorization_servers.end(),issuer)==discovered.authorization_servers.end())invalid();
    auto authorization=discover(mcp_oauth_authorization_metadata_urls(issuer),deadline,cancel,[&](const auto& source){return mcp_oauth_server_metadata(source,issuer);});
    return {std::move(discovered),std::move(authorization)};
}
}
