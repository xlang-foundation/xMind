#pragma once
#include "agentflow/remote_negotiation.hpp"
#include "agentflow/secret_protection.hpp"
#include "agentflow/remote_command_journal.hpp"
#include <chrono>
#include <stop_token>

namespace agentflow {
enum class RemoteTransportPolicy {verified_https,isolated_loopback_test};
// Connector primitive only: does not persist credentials or activate profiles.
// No redirect, protocol downgrade or automatic retry; SDK objects stay local.
RemoteNegotiatedGrant negotiate_remote_connection(const std::string& origin,const SecretBytes& credential,
    RemoteNegotiationRequirements requirements,std::chrono::milliseconds timeout,
    RemoteTransportPolicy policy=RemoteTransportPolicy::verified_https,std::stop_token stop={});
struct RemoteCommandObservation {std::string state;std::optional<RemoteCommandResolution> resolution;};
void validate_remote_receipt_scope(const RemoteNegotiatedGrant&,const std::string&,std::int64_t);
void validate_remote_session_create_scope(const RemoteNegotiatedGrant&,const std::string&,std::int64_t);
RemoteCommandObservation create_remote_session(const std::string& origin,const SecretBytes&,const RemoteNegotiatedGrant&,
    const std::string& scope,const std::string& request_id,const std::string& title,std::chrono::milliseconds,
    RemoteTransportPolicy=RemoteTransportPolicy::verified_https,std::stop_token={});
RemoteCommandObservation observe_remote_session_request(const std::string& origin,const SecretBytes&,const RemoteNegotiatedGrant&,
    const std::string& scope,const std::string& request_id,const std::string& title,std::chrono::milliseconds,
    RemoteTransportPolicy=RemoteTransportPolicy::verified_https,std::stop_token={});
struct RemoteObservedSession {std::string id,title;std::int64_t revision=0;};
void validate_remote_session_scope(const RemoteNegotiatedGrant&,const std::string& scope_id,std::int64_t now_unix_seconds);
RemoteObservedSession read_remote_session(const std::string& origin,const SecretBytes& credential,
    const RemoteNegotiatedGrant&,const std::string& scope_id,const std::string& session_id,std::chrono::milliseconds timeout,
    RemoteTransportPolicy policy=RemoteTransportPolicy::verified_https,std::stop_token stop={});
}
