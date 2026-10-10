#pragma once
#include "agentflow/mcp_configuration.hpp"
#include "agentflow/mcp_oauth.hpp"

namespace agentflow {
struct McpOAuthGrant {
    std::int64_t revision,acquired_unix_ms;
    std::optional<std::int64_t> expires_unix_ms;
    std::string token_endpoint;
    McpOAuthTokens tokens;
    bool usable_at(std::int64_t now_unix_ms) const noexcept;
};
struct McpOAuthStoredRefresh {McpOAuthRefreshRecord record;std::optional<McpOAuthGrant> grant;};
// Native connector-owned encrypted grant storage. One ciphertext/revision holds
// both tokens, scopes, receipt time and token endpoint. SQLite I/O stays on the
// embedded-xlang3 persistence thread. No grant is a tool approval or retry right.
class McpOAuthCredentialStore {
public:
    explicit McpOAuthCredentialStore(PersistenceService& store):store_(store){}
    CredentialMetadata save(const McpServerSetting& setting,std::string token_endpoint,
        McpOAuthTokens tokens,std::int64_t acquired_unix_ms,std::int64_t expected_revision);
    McpOAuthGrant load(const McpServerSetting& setting);
    void remove(const McpServerSetting& setting,std::int64_t expected_revision);
    McpOAuthStoredRefresh prepare_refresh(const McpServerSetting&,std::string request_id,std::int64_t expected_revision);
    McpOAuthRefreshRecord dispatch_refresh(const McpOAuthRefreshRecord&);
    McpOAuthRefreshRecord abandon_refresh(const McpOAuthRefreshRecord&);
    McpOAuthRefreshRecord publish_refresh(const McpServerSetting&,const McpOAuthRefreshRecord&,McpOAuthTokens,std::int64_t acquired_unix_ms);
private:
    PersistenceService& store_;
};
}
