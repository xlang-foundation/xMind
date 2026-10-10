#include "agentflow/mcp_oauth_credentials.hpp"
#include "agentflow/mcp_client_factory.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>
#include <system_error>

using namespace agentflow;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected native OAuth storage rejection did not occur");}
SecretBytes secret(std::string_view value){return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
bool same(const SecretBytes& value,std::string_view expected){return std::ranges::equal(value.view(),std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(expected.data()),expected.size()));}
struct Directory {
    std::filesystem::path parent=std::filesystem::temp_directory_path(),path=parent/("xmind-oauth-grant-"+std::to_string(std::random_device{}()));
    Directory(){require(std::filesystem::create_directory(path),"Fixture directory must be newly created");}
    ~Directory(){if(path.parent_path()==parent&&path.filename().string().starts_with("xmind-oauth-grant-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
McpOAuthTokens tokens(std::string_view access="synthetic-oauth-access-not-live",std::string_view refresh="synthetic-oauth-refresh-not-live",std::optional<std::uint32_t> expiry=3600){return {secret(access),refresh.empty()?std::optional<SecretBytes>{}:std::optional<SecretBytes>{secret(refresh)},expiry,{"files:read","files:write"}};}
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
    Directory directory;const auto database=(directory.path/"state.sqlite").string();const std::vector<std::string> imports{argv[1],argv[2]};
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string issuer="https://issuer.example.test/tenant",endpoint="https://issuer.example.test/token";
    Json server{{"id","oauth-peer"},{"transport","http"},{"endpoint","https://resource.example.test/mcp"},{"oauth",{{"scope","server"},{"id","oauth-grant"},{"issuer",issuer},{"client_id","synthetic-public-client"}}}};
    const auto desired=[&]{return Json{{"servers",Json::array({server})}}.dump();};McpServerSetting config;
    {
        PersistenceService store(database,imports);McpConfigurationStore configurations(store);config=configurations.apply(desired()).at(0);McpOAuthCredentialStore grants(store);
        require(config.revision==1&&config.oauth&&config.oauth->id=="oauth-grant"&&!config.bearer,"OAuth configuration must contain references, not a static bearer");
        {auto defaults=server;defaults["oauth"]["callback"]={{"path","/oauth/callback"},{"port",0}};const auto same_config=configurations.apply(Json{{"servers",Json::array({defaults})}}.dump());require(same_config.at(0).revision==1&&mcp_credential_purpose(same_config.at(0),"OAUTH")==mcp_credential_purpose(config,"OAUTH"),"Explicit default callback settings must keep the same configuration and authority");}
        const auto metadata=grants.save(config,endpoint,tokens(),now,0);require(metadata.revision==1&&metadata.purpose==mcp_credential_purpose(config,"OAUTH"),"Grant save must bind native credential purpose");
        auto grant=grants.load(config);require(grant.revision==1&&grant.acquired_unix_ms==now&&grant.expires_unix_ms==now+3600000&&grant.token_endpoint==endpoint&&grant.tokens.expires_in==3600&&grant.tokens.scopes==std::vector<std::string>{"files:read","files:write"}&&same(grant.tokens.access_token,"synthetic-oauth-access-not-live")&&grant.tokens.refresh_token&&same(*grant.tokens.refresh_token,"synthetic-oauth-refresh-not-live"),"Encrypted whole grant must round trip exact tokens/endpoint/scopes/timestamps and revision");
        require(grant.usable_at(now)&&grant.usable_at(now+3599999)&&!grant.usable_at(now+3600000)&&!grant.usable_at(now-1),"Expiry boundary and clock rollback must reject access");
        const auto public_record=store.information("native-mcp","servers").get();require(public_record.find("synthetic-oauth-access")==std::string::npos&&public_record.find("synthetic-oauth-refresh")==std::string::npos,"Public configuration must not retain either token");
        XlangSqlite inspect(database,imports);const auto row=inspect.execute("SELECT ciphertext FROM credentials WHERE id='oauth-grant'").rows.at(0);const auto encrypted=std::get<SqlBytes>(row.at(0));
        const std::string access="synthetic-oauth-access-not-live",refresh="synthetic-oauth-refresh-not-live";
        require(std::search(encrypted.begin(),encrypted.end(),access.begin(),access.end())==encrypted.end()&&std::search(encrypted.begin(),encrypted.end(),refresh.begin(),refresh.end())==encrypted.end(),"Actual SQLite blob must not contain either plaintext token");
        auto snapshot=store.resolve_credential_snapshot("server","oauth-grant",metadata.purpose).get();require(snapshot.metadata.revision==1&&snapshot.metadata.label=="MCP OAuth authorization"&&!snapshot.secret.view().empty(),"Metadata and plaintext must come from one native database snapshot");
        for(const auto* change:{"resource","issuer","client","server"}){auto other=config;if(std::string(change)=="resource")other.endpoint+="/other";if(std::string(change)=="issuer")other.oauth->issuer+="/other";if(std::string(change)=="client")other.oauth->client_id+="-other";if(std::string(change)=="server")other.id+="-other";rejects<Conflict>([&]{grants.load(other);});}
        const auto unchanged=store.information("native-mcp","servers").get();
        for(const auto* field:{"scope","issuer","client_id"}){auto invalid=server;invalid["oauth"][field]=std::string(field)=="scope"?"another-team":std::string(field)=="issuer"?"http://issuer.example.test":"client\r\ninjected";rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});}
        {auto invalid=server;invalid["credential"]={{"scope","server"},{"id","ambiguous-static-key"}};rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});invalid=server;invalid["oauth"]["access_token"]="must-not-be-public";rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});}
        for(const auto& port:Json::array({-1,65536,1.5,true,"43210",nullptr})){auto invalid=server;invalid["oauth"]["callback"]={{"port",port}};rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});}
        for(const auto* path:{"relative","/bad?query","/regex.*","/bad%2Fpath","/fragment#","/bad\r\npath"}){auto invalid=server;invalid["oauth"]["callback"]={{"path",path}};rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});}
        {auto invalid=server;invalid["oauth"]["callback"]={{"host","0.0.0.0"}};rejects<std::invalid_argument>([&]{configurations.apply(Json{{"servers",Json::array({invalid})}}.dump());});}
        require(store.information("native-mcp","servers").get()==unchanged,"Rejected OAuth configuration must preserve the exact public record");
        rejects<Conflict>([&]{grants.save(config,endpoint,tokens(),now,0);});
        grants.save(config,endpoint,tokens("synthetic-rotated-access","synthetic-rotated-refresh",1800),now,1);
        require(snapshot.metadata.revision==1,"Resolved snapshot metadata cannot change after rotation");
        auto rotated=grants.load(config);require(rotated.revision==2&&same(rotated.tokens.access_token,"synthetic-rotated-access")&&rotated.tokens.refresh_token&&same(*rotated.tokens.refresh_token,"synthetic-rotated-refresh"),"Both tokens and expiry must rotate in one revision");
        rejects<Conflict>([&]{grants.save(config,endpoint,tokens(),now,1);});rejects<Conflict>([&]{grants.remove(config,1);});
        inspect.execute("CREATE TRIGGER reject_oauth_grant BEFORE UPDATE ON credentials BEGIN SELECT RAISE(ABORT,'fixture-storage-fault'); END");
        rejects<DatabaseError>([&]{grants.save(config,endpoint,tokens(),now,2);});require(grants.load(config).revision==2&&same(grants.load(config).tokens.access_token,"synthetic-rotated-access"),"Storage failure must retain the complete previous grant and revision");inspect.execute("DROP TRIGGER reject_oauth_grant");
        for(const auto* target:{"http://issuer.example.test/token","https://user:password@issuer.example.test/token","https://issuer.example.test/token#fragment"})rejects<McpProtocolError>([&]{grants.save(config,target,tokens(),now,2);});
        rejects<McpProtocolError>([&]{grants.save(config,endpoint,tokens("bad\r\ntoken"),now,2);});rejects<McpProtocolError>([&]{grants.save(config,endpoint,tokens("access","refresh",0),now,2);});rejects<McpProtocolError>([&]{grants.save(config,endpoint,tokens(),-1,2);});
        auto copied=config;copied.oauth->id="copied-grant";grants.save(copied,endpoint,mcp_oauth_token_response(secret(R"({"access_token":"copy-access","token_type":"Bearer","refresh_token":"synthetic refresh +&="})"),{}),now,0);require(same(*grants.load(copied).tokens.refresh_token,"synthetic refresh +&="),"Parsed opaque refresh tokens must preserve valid spaces and form punctuation through encrypted SQLite");inspect.execute("UPDATE credentials SET ciphertext=? WHERE id='copied-grant'",{encrypted});rejects<std::system_error>([&]{grants.load(copied);});
        auto malformed=config;malformed.oauth->id="malformed-grant";store.put_credential("server",malformed.oauth->id,mcp_credential_purpose(malformed,"OAUTH"),"Malformed synthetic grant",secret("XMGOAUTHtruncated"),0).get();rejects<McpProtocolError>([&]{grants.load(malformed);});
        auto expired=config;expired.oauth->id="expired-grant";grants.save(expired,endpoint,tokens("expired-access","expired-refresh",1),now-2000,0);rejects<McpOAuthAuthorizationRequired>([&]{connect_mcp_client(expired,store,std::chrono::steady_clock::now()+std::chrono::seconds(3));});
        std::stop_source stop;stop.request_stop();rejects<McpTransportCancelled>([&]{connect_mcp_client(config,store,std::chrono::steady_clock::now()+std::chrono::seconds(3),stop.get_token());});store.close();
    }
    {
        PersistenceService store(database,imports);McpOAuthCredentialStore grants(store);const auto restored=McpConfigurationStore(store).load().at(0);auto grant=grants.load(restored);require(grant.revision==2&&grant.tokens.refresh_token&&same(*grant.tokens.refresh_token,"synthetic-rotated-refresh")&&grant.expires_unix_ms==now+1800000,"Encrypted complete grant must survive actual xlang3 SQLite reopen");
        auto disabled=restored;disabled.enabled=false;require(grants.load(disabled).revision==2,"Backend credential administration must remain available for a disabled connector");rejects<std::invalid_argument>([&]{connect_mcp_client(disabled,store,std::chrono::steady_clock::now()+std::chrono::seconds(3));});
        grants.remove(disabled,2);rejects<NotFound>([&]{grants.load(restored);});rejects<McpOAuthAuthorizationRequired>([&]{connect_mcp_client(restored,store,std::chrono::steady_clock::now()+std::chrono::seconds(3));});rejects<Conflict>([&]{grants.save(restored,endpoint,tokens(),now,0);});
        server["oauth"]["id"]="registered-grant";server["oauth"]["callback"]={{"path","/oauth2redirect/registered-client"},{"port",43210}};
        const auto registered=McpConfigurationStore(store).apply(desired()).at(0);require(registered.revision==2&&registered.oauth->callback.path=="/oauth2redirect/registered-client"&&registered.oauth->callback.port==43210,"Registered callback must persist as a new exact backend configuration");
        grants.save(registered,endpoint,tokens(),now,0);
        for(const auto* changed:{"path","port"}){auto other=registered;if(std::string(changed)=="path")other.oauth->callback.path+="-other";else ++other.oauth->callback.port;require(mcp_credential_purpose(other,"OAUTH")!=mcp_credential_purpose(registered,"OAUTH"),"Registered callback change must alter native credential authority");rejects<Conflict>([&]{grants.load(other);});}
        auto ephemeral=registered;ephemeral.oauth->callback.port=0;rejects<Conflict>([&]{grants.load(ephemeral);});store.close();
    }
    {
        PersistenceService store(database,imports);const auto restored=McpConfigurationStore(store).load().at(0);require(restored.revision==2&&restored.oauth->callback.path=="/oauth2redirect/registered-client"&&restored.oauth->callback.port==43210&&McpOAuthCredentialStore(store).load(restored).revision==1,"Registered callback and its exact encrypted grant authority must survive actual SQLite reopen");store.close();
    }
    std::cout<<"Native OAuth grant storage passed actual xlang3 SQLite encryption/reopen, atomic snapshot/rotation/rollback, resource/issuer/client/server binding, expiry/clock-rollback and factory pre-connect rejection, malformed/ciphertext relocation rejection and retired identity checks. Synthetic tokens; no trusted HTTPS login, refresh/revocation request or UI setup verified\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
