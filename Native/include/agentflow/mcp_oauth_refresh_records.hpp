#pragma once
#include <cstdint>
#include <string>
namespace agentflow {
// Backend-only durable renewal identity. server_json is the exact public stored
// configuration entry, never a token or a caller-selected authorization scope.
struct McpOAuthRefreshSpec {
    std::string request_id,server_id,scope,credential_id,purpose,server_json,token_endpoint;
    std::int64_t configuration_revision=0,credential_revision=0;
    bool operator==(const McpOAuthRefreshSpec&) const = default;
};
struct McpOAuthRefreshRecord {
    McpOAuthRefreshSpec binding;
    std::string generation,state;
    std::int64_t created_unix_ms=0,dispatched_unix_ms=0,settled_unix_ms=0,published_revision=0;
};
}
