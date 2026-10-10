#include "agentflow/mcp_client_factory.hpp"
#include "agentflow/mcp_client.hpp"
#include "agentflow/mcp_http_client.hpp"
#include "agentflow/mcp_oauth_credentials.hpp"
#define NOMINMAX
#include <windows.h>
namespace agentflow {
std::unique_ptr<McpToolClient> connect_mcp_client(const McpServerSetting& setting,
    PersistenceService& store,McpDeadline deadline,std::stop_token cancel,
    std::vector<SecretBytes>* private_catalogue_credentials) {
    const auto check=[&]{if(cancel.stop_requested())throw McpTransportCancelled("MCP connection cancelled");if(std::chrono::steady_clock::now()>=deadline)throw McpTransportTimeout("MCP connection deadline exceeded");};
    check();if(!setting.enabled || setting.id.empty() || setting.revision<1)throw std::invalid_argument("MCP connection requires active native configuration");
    const auto capture=[&](const SecretBytes& secret){if(private_catalogue_credentials)private_catalogue_credentials->emplace_back(secret.view());};
    if(setting.transport=="http") {
        if(!setting.executable.empty() || !setting.working_directory.empty() || !setting.arguments.empty() || !setting.credentials.empty())throw std::invalid_argument("HTTP MCP cannot carry process configuration");
        if(setting.bearer && setting.bearer->scope!="server")throw std::invalid_argument("HTTP MCP credential scope is unavailable");
        if(setting.bearer&&setting.oauth)throw std::invalid_argument("HTTP MCP requires one credential method");
        std::optional<SecretBytes> credential;
        std::optional<McpBearerValidity> validity;
        if(setting.bearer){check();credential=store.resolve_credential(setting.bearer->scope,setting.bearer->id,mcp_credential_purpose(setting,"BEARER")).get();capture(*credential);}
        if(setting.oauth){
            check();auto grant=[&]{try{return McpOAuthCredentialStore(store).load(setting);}catch(const NotFound&){throw McpOAuthAuthorizationRequired({});}}();
            const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            if(!grant.usable_at(now))throw McpOAuthAuthorizationRequired({});
            capture(grant.tokens.access_token);if(grant.tokens.refresh_token)capture(*grant.tokens.refresh_token);
            credential=std::move(grant.tokens.access_token);
            validity=McpBearerValidity{grant.acquired_unix_ms,grant.expires_unix_ms};
        }
        check();auto client=std::make_unique<McpHttpClient>(setting.endpoint,std::move(credential),validity);client->connect(deadline,cancel);return client;
    }
    if(setting.transport!="stdio" || !setting.endpoint.empty() || setting.bearer || setting.oauth)throw std::invalid_argument("Invalid native MCP transport configuration");
    McpStdioConfiguration configuration{setting.executable,setting.working_directory,setting.arguments,{}};
    struct ClearEnvironment {McpStdioConfiguration& config;~ClearEnvironment(){for(auto& entry:config.environment)if(!entry.second.empty())SecureZeroMemory(entry.second.data(),entry.second.size());}} clear{configuration};
    configuration.environment.reserve(setting.credentials.size());
    for(const auto& reference:setting.credentials){check();auto secret=store.resolve_credential(reference.scope,reference.id,mcp_credential_purpose(setting,reference.name)).get();capture(secret);const auto bytes=secret.view();configuration.environment.emplace_back(reference.name,std::string{});configuration.environment.back().second.assign(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    check();auto client=std::make_unique<McpStdioClient>(configuration);client->connect(deadline,cancel);return client;
}
}
