#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace agentflow {
enum class RemoteNegotiationFailure {malformed,server_mismatch,unsupported_version,missing_capability,expired_grant};
class RemoteNegotiationError:public std::runtime_error {
public:
    explicit RemoteNegotiationError(RemoteNegotiationFailure reason);
    RemoteNegotiationFailure reason()const noexcept{return reason_;}
private:
    RemoteNegotiationFailure reason_;
};
struct RemoteAuthorizedScope {std::string id;std::vector<std::string> actions;};
struct RemoteNegotiationRequirements {
    std::vector<std::int64_t> supported_versions{1};
    std::map<std::string,std::int64_t> required_capabilities;
    std::optional<std::string> expected_server_id;
    std::int64_t now_unix_seconds=0;
};
// Backend-private negotiation result. Not a public binding identity or proof
// of authentication: transport, credential and profile admission are separate.
struct RemoteNegotiatedGrant {
    std::string server_id,grant_id;
    std::int64_t protocol_version=0,grant_revision=0,expires_at=0;
    std::vector<RemoteAuthorizedScope> authorized_scopes;
    std::map<std::string,std::int64_t> capabilities;
};
RemoteNegotiatedGrant validate_remote_negotiation(std::string_view response,const RemoteNegotiationRequirements& requirements);
std::string encode_remote_negotiation_request(const RemoteNegotiationRequirements& requirements);
std::string encode_remote_negotiated_grant(const RemoteNegotiatedGrant& grant);
}
