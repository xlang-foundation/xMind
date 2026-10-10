#pragma once
#include "agentflow/mcp_transport.hpp"
#include "agentflow/secret_protection.hpp"
#include <memory>
#include <optional>
#include <stop_token>
#include <utility>
#include <string>
#include <string_view>
#include <vector>
namespace agentflow {
struct McpOAuthChallenge {std::optional<std::string> metadata_url;std::vector<std::string> scopes;std::string error;};
std::optional<McpOAuthChallenge> mcp_oauth_bearer_challenge(std::string_view header);
struct McpOAuthAuthorizationRequired : McpTransportError {
    std::optional<McpOAuthChallenge> challenge;
    explicit McpOAuthAuthorizationRequired(std::optional<McpOAuthChallenge> value):McpTransportError("MCP HTTP authorization is required"),challenge(std::move(value)) {}
};
struct McpOAuthAuthorizationDenied : McpTransportError {
    // Exact allowlisted protocol reason only; never descriptions or error URIs.
    std::string reason;
    explicit McpOAuthAuthorizationDenied(std::string value):McpTransportError("MCP OAuth authorization was not granted"),reason(std::move(value)){}
};
struct McpOAuthResourceMetadata {std::string resource;std::vector<std::string> authorization_servers,scopes;};
struct McpOAuthServerMetadata {
    std::string issuer,authorization_endpoint,token_endpoint;
    std::optional<std::string> registration_endpoint;
    std::vector<std::string> scopes,token_auth_methods;
    bool response_issuer_required=false,client_id_metadata_supported=false;
    bool pkce_s256=false,authorization_code=false;
};
struct McpOAuthDiscovery {McpOAuthResourceMetadata resource;McpOAuthServerMetadata authorization;};
// Bounded native metadata discovery only. No registration, user redirect,
// token request, credential forwarding or automatic tool-call replay.
std::vector<std::string> mcp_oauth_resource_metadata_urls(std::string_view resource);
std::vector<std::string> mcp_oauth_authorization_metadata_urls(std::string_view issuer);
McpOAuthResourceMetadata mcp_oauth_resource_metadata(std::string_view json,std::string_view expected_resource);
McpOAuthServerMetadata mcp_oauth_server_metadata(std::string_view json,std::string_view expected_issuer);
McpOAuthDiscovery discover_mcp_oauth(std::string resource,std::optional<std::string> metadata_url,
    McpDeadline deadline,std::stop_token cancel={},std::optional<std::string> preferred_issuer={});
struct McpOAuthPublicClient {std::string issuer,client_id,redirect_uri;};
struct McpOAuthTokens {
    SecretBytes access_token;
    std::optional<SecretBytes> refresh_token;
    std::optional<std::uint32_t> expires_in;
    std::vector<std::string> scopes;
};
std::string mcp_oauth_pkce_challenge(std::span<const std::uint8_t> verifier);
// Backend-only form encoding; this grants no callback, credential or effect
// authority. The authorization owner binds and consumes its code before use.
SecretBytes mcp_oauth_code_grant_form(const McpOAuthPublicClient& client,
    std::string_view resource,const SecretBytes& code,const SecretBytes& verifier);
McpOAuthTokens mcp_oauth_token_response(SecretBytes json,const std::vector<std::string>& requested_scopes);
// Native single-caller owner for a pre-registered public client. Backend code
// owns this attempt, callback routing and returned credentials. This does not
// provide a listener, persistence, registration, refresh or tool-call replay.
class McpOAuthAuthorizationAttempt {
public:
    McpOAuthAuthorizationAttempt(McpOAuthDiscovery discovery,McpOAuthPublicClient client,
        std::vector<std::string> scopes,McpDeadline expires);
    ~McpOAuthAuthorizationAttempt();
    McpOAuthAuthorizationAttempt(const McpOAuthAuthorizationAttempt&)=delete;
    McpOAuthAuthorizationAttempt& operator=(const McpOAuthAuthorizationAttempt&)=delete;
    McpOAuthAuthorizationAttempt(McpOAuthAuthorizationAttempt&&) noexcept;
    McpOAuthAuthorizationAttempt& operator=(McpOAuthAuthorizationAttempt&&) noexcept;
    std::string authorization_url() const;
    void accept_callback(std::string_view redirect_uri,std::string_view raw_query);
    bool ready() const noexcept;
    McpOAuthTokens exchange(McpDeadline deadline,std::stop_token cancel={});
    void cancel() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
