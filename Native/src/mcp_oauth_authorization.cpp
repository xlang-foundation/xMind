#include "agentflow/mcp_oauth.hpp"
#include "agentflow/mcp_http_transport.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
[[noreturn]] void invalid(){throw McpProtocolError("Invalid MCP OAuth authorization response or binding");}
struct PrivateText {std::string value;~PrivateText(){if(!value.empty())SecureZeroMemory(value.data(),value.size());}};
struct WipeJson {
    Json& value;
    static void wipe(Json& input) noexcept {if(input.is_string()){auto& text=input.get_ref<std::string&>();if(!text.empty())SecureZeroMemory(text.data(),text.size());}else if(input.is_structured())for(auto& item:input)wipe(item);}
    ~WipeJson(){wipe(value);}
};
std::string lower(std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return text;}
void bounded(std::string_view text,std::size_t limit){
    if(text.empty()||text.size()>limit)invalid();
    for(std::size_t i=0;i<text.size();++i){const auto c=static_cast<unsigned char>(text[i]);if(c<0x20||c==0x7f)invalid();if(c<0x80)continue;unsigned scalar=0,count=0,minimum=0;
        if(c>=0xc2&&c<=0xdf){scalar=c&31;count=1;minimum=0x80;}else if(c>=0xe0&&c<=0xef){scalar=c&15;count=2;minimum=0x800;}else if(c>=0xf0&&c<=0xf4){scalar=c&7;count=3;minimum=0x10000;}else invalid();
        if(count>text.size()-i-1)invalid();for(unsigned n=0;n<count;++n){const auto tail=static_cast<unsigned char>(text[++i]);if((tail&0xc0)!=0x80)invalid();scalar=(scalar<<6)|(tail&63);}if(scalar<minimum||scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff))invalid();
    }
}
void https(std::string_view url){try{validate_mcp_http_endpoint(url);}catch(const std::invalid_argument&){invalid();}if(url.size()<8||lower(std::string(url.substr(0,8)))!="https://")invalid();}
void redirect(std::string_view url){
    try{validate_mcp_http_endpoint(url);}catch(const std::invalid_argument&){invalid();}
    if(url.find('?')!=std::string_view::npos)invalid();
    if(url.size()>=8&&lower(std::string(url.substr(0,8)))=="https://")return;
    const auto text=lower(std::string(url));std::size_t start=0;
    for(const auto prefix:{"http://127.0.0.1:","http://localhost:","http://[::1]:"})if(text.starts_with(prefix)){start=std::string_view(prefix).size();break;}
    if(!start)invalid();const auto end=text.find('/',start);if(end==std::string::npos)invalid();unsigned port=0;const auto parsed=std::from_chars(text.data()+start,text.data()+end,port);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+end||!port||port>65535)invalid();
}
bool unreserved(unsigned char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='.'||c=='_'||c=='~';}
void encode(std::string& result,std::span<const std::uint8_t> bytes,std::size_t bound){
    constexpr char hex[]="0123456789ABCDEF";
    for(auto c:bytes){if((unreserved(c)?1:3)>bound-result.size())invalid();if(unreserved(c))result+=static_cast<char>(c);else{result+='%';result+=hex[c>>4];result+=hex[c&15];}}
}
std::span<const std::uint8_t> bytes(std::string_view text){return {reinterpret_cast<const std::uint8_t*>(text.data()),text.size()};}
void parameter(std::string& result,std::string_view name,std::span<const std::uint8_t> value,std::size_t bound){if(name.size()+2>bound-result.size())invalid();if(!result.empty()&&result.back()!='?'&&result.back()!='&')result+='&';result+=name;result+='=';encode(result,value,bound);}
std::string base64url(std::span<const std::uint8_t> source){
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;result.reserve((source.size()*8+5)/6);unsigned bits=0,count=0;
    for(auto byte:source){bits=(bits<<8)|byte;count+=8;while(count>=6){count-=6;result+=alphabet[(bits>>count)&63];}}if(count)result+=alphabet[(bits<<(6-count))&63];return result;
}
SecretBytes random_secret(){std::array<std::uint8_t,32> value{};struct Wipe{std::array<std::uint8_t,32>& value;~Wipe(){SecureZeroMemory(value.data(),value.size());}} wipe{value};if(BCryptGenRandom(nullptr,value.data(),static_cast<ULONG>(value.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw McpTransportError("Native OAuth random generation failed");PrivateText encoded;encoded.value=base64url(value);return SecretBytes(bytes(encoded.value));}
int digit(char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;invalid();}
void decode(std::string_view source,std::string& result){result.reserve(source.size());for(std::size_t i=0;i<source.size();++i){unsigned char c=source[i];if(c=='%'){if(i+2>=source.size())invalid();c=static_cast<unsigned char>((digit(source[i+1])<<4)|digit(source[i+2]));i+=2;}else if(c=='+')c=' ';if(c<0x20||c==0x7f)invalid();result+=static_cast<char>(c);}if(!result.empty())bounded(result,8192);}
using Parameters=std::map<std::string,PrivateText>;
Parameters query(std::string_view source){
    if(source.size()>32768)invalid();Parameters result;std::size_t begin=0;
    while(begin<source.size()){const auto end=source.find('&',begin);const auto item=source.substr(begin,end==std::string_view::npos?source.size()-begin:end-begin);const auto equal=item.find('=');if(item.empty()||equal==std::string_view::npos)invalid();PrivateText key;decode(item.substr(0,equal),key.value);bounded(key.value,128);if(result.size()>=64||result.contains(key.value))invalid();decode(item.substr(equal+1),result[key.value].value);if(end==std::string_view::npos)break;begin=end+1;if(begin==source.size())invalid();}
    return result;
}
bool equals(std::span<const std::uint8_t> expected,std::string_view actual){std::size_t diff=expected.size()^actual.size();for(std::size_t i=0;i<expected.size();++i)diff|=expected[i]^(i<actual.size()?static_cast<unsigned char>(actual[i]):0);return diff==0;}
void scopes_valid(const std::vector<std::string>& scopes){if(scopes.size()>64)invalid();std::set<std::string> seen;std::size_t total=0;for(const auto& scope:scopes){if(scope.empty()||!seen.insert(scope).second)invalid();for(unsigned char c:scope)if(c<0x21||c>0x7e||c=='"'||c=='\\')invalid();total+=scope.size()+1;if(total>8192)invalid();}}
std::vector<std::string> scope_list(std::string_view source){std::vector<std::string> result;std::set<std::string> seen;if(source.empty())invalid();std::size_t start=0;while(start<source.size()){auto end=source.find(' ',start);auto scope=std::string(source.substr(start,end==std::string_view::npos?source.size()-start:end-start));if(scope.empty())invalid();if(seen.insert(scope).second)result.push_back(std::move(scope));if(end==std::string_view::npos)break;start=end+1;if(start==source.size())invalid();}scopes_valid(result);return result;}
SecretBytes secret(const Json& value,const char* key,bool refresh=false){if(!value.contains(key)||!value[key].is_string())invalid();const auto& text=value[key].get_ref<const std::string&>();bounded(text,32768);for(unsigned char c:text)if(c<(refresh?0x20:0x21)||c>0x7e)invalid();return SecretBytes(bytes(text));}
}
std::string mcp_oauth_pkce_challenge(std::span<const std::uint8_t> verifier){
    if(verifier.size()<43||verifier.size()>128)invalid();for(auto c:verifier)if(!unreserved(c))invalid();
    BCRYPT_ALG_HANDLE raw=nullptr;if(BCryptOpenAlgorithmProvider(&raw,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw McpTransportError("Native OAuth SHA-256 is unavailable");struct Close{BCRYPT_ALG_HANDLE value;~Close(){BCryptCloseAlgorithmProvider(value,0);}} close{raw};
    std::array<std::uint8_t,32> hash{};if(BCryptHash(raw,nullptr,0,const_cast<PUCHAR>(verifier.data()),static_cast<ULONG>(verifier.size()),hash.data(),static_cast<ULONG>(hash.size()))<0)throw McpTransportError("Native OAuth SHA-256 failed");return base64url(hash);
}
McpOAuthTokens mcp_oauth_token_response(SecretBytes source,const std::vector<std::string>& requested){
    scopes_valid(requested);const auto raw=source.view();if(raw.empty()||raw.size()>1024*1024)invalid();Json value;WipeJson wipe{value};std::vector<std::set<std::string>> keys;
    try{value=Json::parse(raw.begin(),raw.end(),[&](int depth,Json::parse_event_t event,Json& parsed){if(depth>16)invalid();if(event==Json::parse_event_t::object_start)keys.emplace_back();else if(event==Json::parse_event_t::object_end)keys.pop_back();else if(event==Json::parse_event_t::key&&!keys.back().insert(parsed.get<std::string>()).second)invalid();return true;});}catch(const Json::exception&){invalid();}
    if(!value.is_object()||value.contains("error")||!value.contains("token_type")||!value["token_type"].is_string()||lower(value["token_type"].get<std::string>())!="bearer")invalid();
    McpOAuthTokens result{secret(value,"access_token"),{}, {},requested};if(value.contains("refresh_token"))result.refresh_token=secret(value,"refresh_token",true);
    if(value.contains("expires_in")){const auto& expiry=value["expires_in"];if(!expiry.is_number_integer()||(!expiry.is_number_unsigned()&&expiry.get<std::int64_t>()<=0))invalid();const auto seconds=expiry.get<std::uint64_t>();if(!seconds||seconds>UINT32_MAX)invalid();result.expires_in=static_cast<std::uint32_t>(seconds);}
    if(value.contains("scope")){if(!value["scope"].is_string())invalid();result.scopes=scope_list(value["scope"].get_ref<const std::string&>());if(!requested.empty())for(const auto& scope:result.scopes)if(std::find(requested.begin(),requested.end(),scope)==requested.end())invalid();}
    return result;
}
SecretBytes mcp_oauth_code_grant_form(const McpOAuthPublicClient& client,std::string_view resource,const SecretBytes& code,const SecretBytes& verifier){
    bounded(client.client_id,2048);https(client.issuer);redirect(client.redirect_uri);https(resource);if(code.view().empty())invalid();bounded(std::string_view(reinterpret_cast<const char*>(code.view().data()),code.view().size()),4096);(void)mcp_oauth_pkce_challenge(verifier.view());
    PrivateText form;form.value.reserve(65536);parameter(form.value,"grant_type",bytes("authorization_code"),65536);parameter(form.value,"client_id",bytes(client.client_id),65536);parameter(form.value,"redirect_uri",bytes(client.redirect_uri),65536);parameter(form.value,"resource",bytes(resource),65536);parameter(form.value,"code",code.view(),65536);parameter(form.value,"code_verifier",verifier.view(),65536);return SecretBytes(bytes(form.value));
}
struct McpOAuthAuthorizationAttempt::Impl {
    McpOAuthDiscovery discovery;McpOAuthPublicClient client;std::vector<std::string> scopes;McpDeadline expires;
    SecretBytes state,verifier;std::optional<SecretBytes> code;enum class Phase{callback,exchange,retired};Phase phase=Phase::callback;
    Impl(McpOAuthDiscovery value,McpOAuthPublicClient registration,std::vector<std::string> wanted,McpDeadline limit):discovery(std::move(value)),client(std::move(registration)),scopes(std::move(wanted)),expires(limit),state(random_secret()),verifier(random_secret()){}
    void retire() noexcept {phase=Phase::retired;state.clear();verifier.clear();code.reset();}
    void check(){if(phase==Phase::retired)invalid();if(std::chrono::steady_clock::now()>=expires){retire();throw McpTransportTimeout("MCP OAuth authorization expired");}}
};
McpOAuthAuthorizationAttempt::McpOAuthAuthorizationAttempt(McpOAuthDiscovery discovery,McpOAuthPublicClient client,std::vector<std::string> scopes,McpDeadline expires){
    scopes_valid(scopes);bounded(client.client_id,2048);redirect(client.redirect_uri);https(discovery.resource.resource);const auto& metadata=discovery.authorization;https(metadata.issuer);https(metadata.authorization_endpoint);https(metadata.token_endpoint);
    if(metadata.issuer.find('?')!=std::string::npos||client.issuer!=metadata.issuer||std::find(discovery.resource.authorization_servers.begin(),discovery.resource.authorization_servers.end(),metadata.issuer)==discovery.resource.authorization_servers.end()||!metadata.pkce_s256||!metadata.authorization_code||std::find(metadata.token_auth_methods.begin(),metadata.token_auth_methods.end(),"none")==metadata.token_auth_methods.end())invalid();
    const auto existing=metadata.authorization_endpoint.find('?');if(existing!=std::string::npos){const auto fields=query(std::string_view(metadata.authorization_endpoint).substr(existing+1));for(const auto* field:{"response_type","client_id","redirect_uri","scope","state","code_challenge","code_challenge_method","resource"})if(fields.contains(field))invalid();}
    const auto now=std::chrono::steady_clock::now();if(expires<=now||expires>now+std::chrono::minutes(10))throw McpTransportTimeout("Invalid MCP OAuth authorization lifetime");impl_=std::make_unique<Impl>(std::move(discovery),std::move(client),std::move(scopes),expires);
}
McpOAuthAuthorizationAttempt::~McpOAuthAuthorizationAttempt()=default;
McpOAuthAuthorizationAttempt::McpOAuthAuthorizationAttempt(McpOAuthAuthorizationAttempt&&) noexcept=default;
McpOAuthAuthorizationAttempt& McpOAuthAuthorizationAttempt::operator=(McpOAuthAuthorizationAttempt&&) noexcept=default;
std::string McpOAuthAuthorizationAttempt::redirect_uri() const {if(!impl_)invalid();return impl_->client.redirect_uri;}
McpDeadline McpOAuthAuthorizationAttempt::expires_at() const {if(!impl_)invalid();return impl_->expires;}
std::string McpOAuthAuthorizationAttempt::authorization_url() const {
    if(!impl_||impl_->phase!=Impl::Phase::callback)invalid();if(std::chrono::steady_clock::now()>=impl_->expires){impl_->retire();throw McpTransportTimeout("MCP OAuth authorization expired");}const auto& owner=*impl_;std::string result=owner.discovery.authorization.authorization_endpoint;result.reserve(32768);if(result.find('?')==std::string::npos)result+='?';
    parameter(result,"response_type",bytes("code"),32768);parameter(result,"client_id",bytes(owner.client.client_id),32768);parameter(result,"redirect_uri",bytes(owner.client.redirect_uri),32768);parameter(result,"resource",bytes(owner.discovery.resource.resource),32768);parameter(result,"state",owner.state.view(),32768);parameter(result,"code_challenge",bytes(mcp_oauth_pkce_challenge(owner.verifier.view())),32768);parameter(result,"code_challenge_method",bytes("S256"),32768);
    if(!owner.scopes.empty()){std::string scopes;for(const auto& scope:owner.scopes){if(!scopes.empty())scopes+=' ';scopes+=scope;}parameter(result,"scope",bytes(scopes),32768);}return result;
}
void McpOAuthAuthorizationAttempt::accept_callback(std::string_view redirect_uri,std::string_view raw_query){
    if(!impl_)invalid();auto& owner=*impl_;try{owner.check();if(owner.phase!=Impl::Phase::callback||redirect_uri!=owner.client.redirect_uri||raw_query.size()>8192)invalid();auto fields=query(raw_query);if(!fields.contains("state")||!equals(owner.state.view(),fields.at("state").value))invalid();
        const auto issuer=fields.find("iss");if((issuer==fields.end()&&owner.discovery.authorization.response_issuer_required)||(issuer!=fields.end()&&issuer->second.value!=owner.discovery.authorization.issuer))invalid();
        if(fields.contains("error")){if(fields.contains("code"))invalid();const auto& error=fields.at("error").value;std::string reason="authorization_failed";for(const auto* allowed:{"access_denied","interaction_required","login_required","consent_required","temporarily_unavailable","server_error","invalid_request","unauthorized_client","unsupported_response_type","invalid_scope"})if(error==allowed){reason=allowed;break;}throw McpOAuthAuthorizationDenied(std::move(reason));}
        if(!fields.contains("code"))invalid();bounded(fields.at("code").value,4096);owner.code=SecretBytes(bytes(fields.at("code").value));owner.state.clear();owner.phase=Impl::Phase::exchange;
    }catch(...){owner.retire();throw;}
}
bool McpOAuthAuthorizationAttempt::ready()const noexcept{if(!impl_)return false;if(std::chrono::steady_clock::now()>=impl_->expires){impl_->retire();return false;}return impl_->phase==Impl::Phase::exchange;}
void McpOAuthAuthorizationAttempt::cancel()noexcept{if(impl_)impl_->retire();}
McpOAuthTokens McpOAuthAuthorizationAttempt::exchange(McpDeadline deadline,std::stop_token cancel){
    if(!impl_)invalid();auto& owner=*impl_;try{owner.check();if(owner.phase!=Impl::Phase::exchange||!owner.code)invalid();owner.phase=Impl::Phase::retired;if(cancel.stop_requested())throw McpTransportCancelled("MCP OAuth exchange cancelled");const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(std::min(deadline,owner.expires)-std::chrono::steady_clock::now());if(remaining.count()<=0)throw McpTransportTimeout("MCP OAuth exchange deadline exceeded");
        auto body=mcp_oauth_code_grant_form(owner.client,owner.discovery.resource.resource,*owner.code,owner.verifier);HttpStreamRequest request;request.url=owner.discovery.authorization.token_endpoint;request.deadline=std::min(remaining,std::chrono::milliseconds(600000));request.idle_timeout=request.deadline;auto response=post_form_json(request,body,1024*1024,cancel);auto tokens=mcp_oauth_token_response(std::move(response),owner.scopes);owner.retire();return tokens;
    }catch(const TransportCancelled&){owner.retire();throw McpTransportCancelled("MCP OAuth exchange cancelled");}catch(const TransportTimeout&){owner.retire();throw McpTransportTimeout("MCP OAuth exchange deadline exceeded");}catch(const ProviderHttpError& error){owner.retire();throw McpTransportError("MCP OAuth token endpoint returned HTTP "+std::to_string(error.status));}catch(const TransportError&){owner.retire();throw McpTransportError("Native MCP OAuth exchange transport failed");}catch(...){owner.retire();throw;}
}
}
