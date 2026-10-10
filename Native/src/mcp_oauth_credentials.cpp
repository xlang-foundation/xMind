#include "agentflow/mcp_oauth_credentials.hpp"
#include "agentflow/mcp_http_transport.hpp"
#include "agentflow/mcp_wire.hpp"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <limits>
#include <set>

namespace agentflow {
namespace {
constexpr std::size_t bound=128*1024;
constexpr std::string_view magic="XMGOAUTH";
[[noreturn]]void invalid(){throw McpProtocolError("Invalid private MCP OAuth grant");}
std::span<const std::uint8_t> bytes(std::string_view value){return {reinterpret_cast<const std::uint8_t*>(value.data()),value.size()};}
void token(std::span<const std::uint8_t> value,bool refresh=false){if(value.empty()||value.size()>32768)invalid();for(auto c:value)if(c<(refresh?0x20:0x21)||c>0x7e)invalid();}
void scopes(const std::vector<std::string>& values){if(values.size()>64)invalid();std::set<std::string> seen;std::size_t total=0;for(const auto& value:values){if(value.empty()||!seen.insert(value).second)invalid();for(unsigned char c:value)if(c<0x21||c>0x7e||c=='"'||c=='\\')invalid();total+=value.size()+1;if(total>8192)invalid();}}
void endpoint(const std::string& value){try{validate_mcp_http_endpoint(value);}catch(const std::invalid_argument&){invalid();}auto scheme=value.substr(0,8);for(auto& c:scheme)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');if(scheme!="https://")invalid();}
std::optional<std::int64_t> expiry(std::int64_t acquired,std::optional<std::uint32_t> seconds){
    if(acquired<=0)invalid();if(!seconds)return {};if(!*seconds||acquired>std::numeric_limits<std::int64_t>::max()-static_cast<std::int64_t>(*seconds)*1000)invalid();return acquired+static_cast<std::int64_t>(*seconds)*1000;
}
void setting(const McpServerSetting& value){if(value.transport!="http"||value.revision<1||!value.executable.empty()||!value.working_directory.empty()||!value.arguments.empty()||!value.credentials.empty())throw std::invalid_argument("OAuth grant requires configured HTTP MCP transport");(void)mcp_credential_purpose(value,"OAUTH");}
struct Writer {
    std::vector<std::uint8_t> value;
    Writer(){value.reserve(bound);}
    ~Writer(){if(!value.empty())SecureZeroMemory(value.data(),value.size());}
    void append(std::span<const std::uint8_t> input){if(input.size()>bound-value.size())invalid();value.insert(value.end(),input.begin(),input.end());}
    void number(std::uint64_t input,unsigned width){for(unsigned n=0;n<width;++n){const std::uint8_t byte=static_cast<std::uint8_t>(input>>(n*8));append({&byte,1});}}
    void field(std::span<const std::uint8_t> input){number(input.size(),4);append(input);}
};
struct Reader {
    std::span<const std::uint8_t> source;std::size_t cursor=0;
    std::span<const std::uint8_t> take(std::size_t count){if(count>source.size()-cursor)invalid();auto result=source.subspan(cursor,count);cursor+=count;return result;}
    std::uint64_t number(unsigned width){std::uint64_t value=0;const auto input=take(width);for(unsigned n=0;n<width;++n)value|=static_cast<std::uint64_t>(input[n])<<(n*8);return value;}
    std::span<const std::uint8_t> field(std::size_t limit){const auto size=number(4);if(size>limit)invalid();return take(static_cast<std::size_t>(size));}
    std::string text(std::size_t limit){const auto input=field(limit);return {reinterpret_cast<const char*>(input.data()),input.size()};}
};
SecretBytes encode_grant(const std::string& target,const McpOAuthTokens& tokens,std::int64_t acquired){
    endpoint(target);token(tokens.access_token.view());if(tokens.refresh_token)token(tokens.refresh_token->view(),true);scopes(tokens.scopes);(void)expiry(acquired,tokens.expires_in);
    Writer out;out.append(bytes(magic));out.number(static_cast<std::uint64_t>(acquired),8);out.number(tokens.expires_in.value_or(0),4);out.field(bytes(target));out.number(tokens.scopes.size(),4);for(const auto& scope:tokens.scopes)out.field(bytes(scope));out.field(tokens.access_token.view());out.field(tokens.refresh_token?tokens.refresh_token->view():std::span<const std::uint8_t>{});return SecretBytes(out.value);
}
McpOAuthGrant decode_grant(ResolvedCredential resolved){
    Reader input{resolved.secret.view()};if(input.source.size()>bound||!std::ranges::equal(input.take(magic.size()),bytes(magic)))invalid();const auto timestamp=input.number(8);if(!timestamp||timestamp>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))invalid();const auto acquired=static_cast<std::int64_t>(timestamp);const auto seconds=static_cast<std::uint32_t>(input.number(4));std::optional<std::uint32_t> duration;if(seconds)duration=seconds;
    auto target=input.text(8192);endpoint(target);const auto count=input.number(4);if(count>64)invalid();std::vector<std::string> granted;for(std::uint64_t n=0;n<count;++n)granted.push_back(input.text(8192));scopes(granted);
    const auto access=input.field(32768);token(access);McpOAuthTokens tokens{SecretBytes(access),{},duration,std::move(granted)};const auto refresh=input.field(32768);if(!refresh.empty()){token(refresh,true);tokens.refresh_token=SecretBytes(refresh);}if(input.cursor!=input.source.size())invalid();
    return {resolved.metadata.revision,acquired,expiry(acquired,duration),std::move(target),std::move(tokens)};
}
McpOAuthRefreshSpec refresh_binding(const McpServerSetting& config,const std::string& id,std::int64_t revision,const std::string& target){return {id,config.id,config.oauth->scope,config.oauth->id,mcp_credential_purpose(config,"OAUTH"),mcp_server_setting_json(config),target,config.revision,revision};}
}
bool McpOAuthGrant::usable_at(std::int64_t now) const noexcept {return now>=acquired_unix_ms&&(!expires_unix_ms||now<*expires_unix_ms);}
CredentialMetadata McpOAuthCredentialStore::save(const McpServerSetting& config,std::string token_endpoint,McpOAuthTokens tokens,std::int64_t acquired,std::int64_t expected_revision){
    setting(config);auto encoded=encode_grant(token_endpoint,tokens,acquired);
    return store_.put_credential(config.oauth->scope,config.oauth->id,mcp_credential_purpose(config,"OAUTH"),"MCP OAuth authorization",std::move(encoded),expected_revision).get();
}
McpOAuthGrant McpOAuthCredentialStore::load(const McpServerSetting& config){
    setting(config);return decode_grant(store_.resolve_credential_snapshot(config.oauth->scope,config.oauth->id,mcp_credential_purpose(config,"OAUTH")).get());
}
void McpOAuthCredentialStore::remove(const McpServerSetting& config,std::int64_t revision){setting(config);auto snapshot=store_.resolve_credential_snapshot(config.oauth->scope,config.oauth->id,mcp_credential_purpose(config,"OAUTH")).get();if(snapshot.metadata.revision!=revision)throw Conflict("MCP OAuth grant revision changed");store_.delete_credential(config.oauth->scope,config.oauth->id,revision).get();}
McpOAuthStoredRefresh McpOAuthCredentialStore::prepare_refresh(const McpServerSetting& config,std::string id,std::int64_t revision){
    setting(config);std::optional<McpOAuthRefreshSpec> binding;
    try{const auto previous=store_.mcp_oauth_refresh(id).get();binding=refresh_binding(config,id,revision,previous.binding.token_endpoint);}
    catch(const NotFound&){auto current=load(config);if(current.revision!=revision)throw Conflict("MCP refresh credential revision changed");if(!current.tokens.refresh_token)invalid();binding=refresh_binding(config,id,revision,current.token_endpoint);}
    auto claim=store_.claim_mcp_oauth_refresh(std::move(*binding)).get();if(!claim.grant)return {std::move(claim.record),{}};
    try{auto grant=decode_grant(std::move(*claim.grant));if(!grant.tokens.refresh_token||grant.token_endpoint!=claim.record.binding.token_endpoint)invalid();return {std::move(claim.record),std::move(grant)};}
    catch(...){try{store_.abandon_mcp_oauth_refresh(claim.record.binding.request_id,claim.record.generation).get();}catch(...){}throw;}
}
McpOAuthRefreshRecord McpOAuthCredentialStore::dispatch_refresh(const McpOAuthRefreshRecord& r){return store_.dispatch_mcp_oauth_refresh(r.binding.request_id,r.generation).get();}
McpOAuthRefreshRecord McpOAuthCredentialStore::abandon_refresh(const McpOAuthRefreshRecord& r){return store_.abandon_mcp_oauth_refresh(r.binding.request_id,r.generation).get();}
McpOAuthRefreshRecord McpOAuthCredentialStore::publish_refresh(const McpServerSetting& config,const McpOAuthRefreshRecord& r,McpOAuthTokens tokens,std::int64_t acquired){
    setting(config);if(refresh_binding(config,r.binding.request_id,r.binding.credential_revision,r.binding.token_endpoint)!=r.binding)throw Conflict("MCP refresh publication binding differs");
    auto previous=load(config);if(previous.revision!=r.binding.credential_revision||previous.token_endpoint!=r.binding.token_endpoint)throw Conflict("MCP refresh stored grant changed");
    for(const auto& scope:tokens.scopes)if(std::find(previous.tokens.scopes.begin(),previous.tokens.scopes.end(),scope)==previous.tokens.scopes.end())invalid();
    if(!tokens.refresh_token)tokens.refresh_token=std::move(previous.tokens.refresh_token);if(!tokens.refresh_token)invalid();auto encoded=encode_grant(r.binding.token_endpoint,tokens,acquired);
    return store_.publish_mcp_oauth_refresh(r.binding.request_id,r.generation,std::move(encoded)).get();
}
}
