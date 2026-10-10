#pragma once
#include "agentflow/mcp_transport.hpp"
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
struct McpOAuthResourceMetadata {std::string resource;std::vector<std::string> authorization_servers,scopes;};
struct McpOAuthServerMetadata {
    std::string issuer,authorization_endpoint,token_endpoint;
    std::optional<std::string> registration_endpoint;
    std::vector<std::string> scopes,token_auth_methods;
    bool response_issuer_required=false,client_id_metadata_supported=false;
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
}
