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
void validate_remote_run_submit_scope(const RemoteNegotiatedGrant&,const std::string&,std::int64_t);
void validate_remote_run_receipt_scope(const RemoteNegotiatedGrant&,const std::string&,std::int64_t);
void validate_remote_run_cancel_scope(const RemoteNegotiatedGrant&,const std::string&,std::int64_t);
RemoteCommandObservation cancel_remote_run(const std::string& origin,const SecretBytes&,const RemoteNegotiatedGrant&,
    const std::string& scope,const std::string& request_id,const RemoteRunCancelAction&,std::chrono::milliseconds,
    RemoteTransportPolicy=RemoteTransportPolicy::verified_https,std::stop_token={});
RemoteCommandObservation observe_remote_cancel_request(const std::string& origin,const SecretBytes&,const RemoteNegotiatedGrant&,
    const std::string& scope,const std::string& request_id,const RemoteRunCancelAction&,std::chrono::milliseconds,
    RemoteTransportPolicy=RemoteTransportPolicy::verified_https,std::stop_token={});
RemoteCommandObservation submit_remote_run(const std::string& origin,const SecretBytes&,const RemoteNegotiatedGrant&,
    const std::string& scope,const std::string& request_id,const RemoteRunSubmitAction&,std::chrono::milliseconds,
    RemoteTransportPolicy=RemoteTransportPolicy::verified_https,std::stop_token={});
RemoteCommandObservation observe_remote_run_request(const std::string& origin,const SecretBytes&,const RemoteNegotiatedGrant&,
    const std::string& scope,const std::string& request_id,const RemoteRunSubmitAction&,std::chrono::milliseconds,
    RemoteTransportPolicy=RemoteTransportPolicy::verified_https,std::stop_token={});
RemoteCommandObservation create_remote_session(const std::string& origin,const SecretBytes&,const RemoteNegotiatedGrant&,
    const std::string& scope,const std::string& request_id,const std::string& title,std::chrono::milliseconds,
    RemoteTransportPolicy=RemoteTransportPolicy::verified_https,std::stop_token={});
RemoteCommandObservation observe_remote_session_request(const std::string& origin,const SecretBytes&,const RemoteNegotiatedGrant&,
    const std::string& scope,const std::string& request_id,const std::string& title,std::chrono::milliseconds,
    RemoteTransportPolicy=RemoteTransportPolicy::verified_https,std::stop_token={});
struct RemoteObservedSession {std::string id,title;std::int64_t revision=0;};
struct RemoteObservedRun {std::string id,session_id,root_id,state;std::int64_t revision=0;};
struct RemoteRunEventCursor {
    std::string connection_id,server_id,scope_id,session_id,run_id,epoch;
    std::int64_t sequence=0;
    bool operator==(const RemoteRunEventCursor&)const=default;
};
enum class RemoteEventGapReason {retention_gap,epoch_expired};
class RemoteEventCursorGap:public std::runtime_error {
public:
    explicit RemoteEventCursorGap(RemoteEventGapReason reason):std::runtime_error("Remote event cursor requires an authoritative snapshot and explicit reset"),reason_(reason){}
    RemoteEventGapReason reason()const noexcept{return reason_;}
private:RemoteEventGapReason reason_;
};
struct RemoteObservedRunEvents {RemoteRunEventCursor cursor;std::vector<Event> events;};
void validate_remote_event_scope(const RemoteNegotiatedGrant&,const std::string&,std::int64_t);
// Backend connector only. Does not publish a cursor, reset gaps or replay work.
RemoteObservedRunEvents read_remote_run_events(const std::string& origin,const SecretBytes&,
    const RemoteNegotiatedGrant&,const RemoteRunEventCursor&,std::chrono::milliseconds,
    RemoteTransportPolicy=RemoteTransportPolicy::verified_https,std::stop_token={});
void validate_remote_run_scope(const RemoteNegotiatedGrant&,const std::string& scope_id,std::int64_t now_unix_seconds);
RemoteObservedRun read_remote_run(const std::string& origin,const SecretBytes& credential,
    const RemoteNegotiatedGrant&,const std::string& scope_id,const std::string& session_id,const std::string& run_id,
    std::chrono::milliseconds timeout,RemoteTransportPolicy policy=RemoteTransportPolicy::verified_https,std::stop_token stop={});
void validate_remote_session_scope(const RemoteNegotiatedGrant&,const std::string& scope_id,std::int64_t now_unix_seconds);
RemoteObservedSession read_remote_session(const std::string& origin,const SecretBytes& credential,
    const RemoteNegotiatedGrant&,const std::string& scope_id,const std::string& session_id,std::chrono::milliseconds timeout,
    RemoteTransportPolicy policy=RemoteTransportPolicy::verified_https,std::stop_token stop={});
}
