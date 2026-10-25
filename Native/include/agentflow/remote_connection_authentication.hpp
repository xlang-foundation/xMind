#pragma once
#include "agentflow/remote_event_checkpoint.hpp"
#include "agentflow/remote_connection_client.hpp"
#include "agentflow/remote_connection_credentials.hpp"
#include <functional>

namespace agentflow {
// Native connector boundary. An injected exchange is trusted backend code,
// never a caller-selected SDK callback or a bypass supplied by a UI.
using RemoteNegotiationExchange=std::function<RemoteNegotiatedGrant(const std::string&,const SecretBytes&,RemoteNegotiationRequirements,std::chrono::milliseconds,std::stop_token)>;
using RemoteSessionReadExchange=std::function<RemoteObservedSession(const std::string&,const SecretBytes&,const RemoteNegotiatedGrant&,const std::string&,const std::string&,std::chrono::milliseconds,std::stop_token)>;
using RemoteRunReadExchange=std::function<RemoteObservedRun(const std::string&,const SecretBytes&,const RemoteNegotiatedGrant&,const std::string&,const std::string&,const std::string&,std::chrono::milliseconds,std::stop_token)>;
using RemoteRunEventExchange=std::function<RemoteObservedRunEvents(const std::string&,const SecretBytes&,const RemoteNegotiatedGrant&,const RemoteRunEventCursor&,std::chrono::milliseconds,std::stop_token)>;
// Backend-only captured state; SDK objects expose a separate public identity.
using RemoteSessionCommandExchange=std::function<RemoteCommandObservation(const std::string&,const SecretBytes&,const RemoteNegotiatedGrant&,const std::string&,const std::string&,const std::string&,std::chrono::milliseconds,std::stop_token)>;
using RemoteRunCommandExchange=std::function<RemoteCommandObservation(const std::string&,const SecretBytes&,const RemoteNegotiatedGrant&,const std::string&,const std::string&,const RemoteRunSubmitAction&,std::chrono::milliseconds,std::stop_token)>;
using RemoteRunCancelExchange=std::function<RemoteCommandObservation(const std::string&,const SecretBytes&,const RemoteNegotiatedGrant&,const std::string&,const std::string&,const RemoteRunCancelAction&,std::chrono::milliseconds,std::stop_token)>;
struct RemoteCapturedScope {ConnectionProfile profile;RemoteNegotiatedGrant grant;std::string scope_id;std::int64_t credential_revision=0;};
struct RemoteAuthenticationReceipt {
    std::string connection_id,server_id;
    std::int64_t profile_revision,credential_revision,protocol_version,expires_at;
};
struct RemoteAuthenticationStatus {
    std::string connection_id,state="absent";
    std::int64_t profile_revision=0,credential_revision=0;
    std::optional<std::string> server_id;
    std::optional<std::int64_t> protocol_version,expires_at;
    // Cached, backend-observed grant metadata only. Empty unless the stored
    // grant matches this profile and is unexpired; never execution authority.
    std::vector<RemoteAuthorizedScope> authorized_scopes;
};
class RemoteConnectionAuthentication {
public:
    RemoteConnectionAuthentication(PersistenceService&,ConnectionProfiles&);
    RemoteConnectionAuthentication(PersistenceService&,ConnectionProfiles&,RemoteNegotiationExchange,RemoteSessionReadExchange = {},RemoteSessionCommandExchange create = {},RemoteSessionCommandExchange receipt = {},RemoteRunReadExchange run_read = {},RemoteRunCommandExchange run_submit = {},RemoteRunCommandExchange run_receipt = {},RemoteRunCancelExchange run_cancel = {},RemoteRunCancelExchange cancel_receipt = {},RemoteRunEventExchange event_read = {});
    RemoteAuthenticationReceipt authenticate(const std::string& profile_id,std::int64_t expected_profile_revision,
        std::int64_t expected_credential_revision,const SecretBytes& credential,std::chrono::milliseconds timeout,std::stop_token stop={});
    // Local encrypted-record observation only; no live revocation check,
    // mutation retry authority or attribution to a particular request.
    RemoteAuthenticationStatus inspect(const std::string& profile_id,std::int64_t expected_profile_revision);
    RemoteCapturedScope open_scope(const std::string& profile_id,std::int64_t expected_profile_revision,const std::string& scope_id);
    void validate_scope(const RemoteCapturedScope&);
    RemoteObservedSession read_session(const RemoteCapturedScope&,const std::string& session_id,std::chrono::milliseconds timeout,std::stop_token stop={});
    RemoteObservedRun read_run(const RemoteCapturedScope&,const std::string& session_id,const std::string& run_id,std::chrono::milliseconds timeout,std::stop_token stop={});
    RemoteRunEventCursor run_event_cursor(const RemoteCapturedScope&,const std::string& session_id,const std::string& run_id);
    RemoteObservedRunEvents read_run_events(const RemoteCapturedScope&,const std::string& session_id,const std::string& run_id,const RemoteRunEventCursor&,std::chrono::milliseconds,std::stop_token={});
    RemoteEventCheckpoint open_run_event_consumer(const RemoteCapturedScope&,const std::string& consumer_id,const std::string& session_id,const std::string& run_id);
    RemoteEventCheckpoint read_run_event_consumer(const RemoteCapturedScope&,const std::string& consumer_id,const std::string& session_id,const std::string& run_id,std::int64_t expected_revision,std::chrono::milliseconds,std::stop_token={});
    RemoteEventCheckpoint accept_run_event_consumer(const RemoteCapturedScope&,const std::string& consumer_id,const std::string& session_id,const std::string& run_id,std::int64_t expected_revision,const RemoteRunEventCursor&,std::stop_token={});
    static bool commands_available(const RemoteCapturedScope&);
    RemoteCommandRecord observe_command(const RemoteCapturedScope&,const std::string& request_id);
    RemoteCommandRecord create_session(const RemoteCapturedScope&,const std::string& request_id,const std::string& title,std::chrono::milliseconds,std::stop_token={});
    RemoteCommandRecord submit_run(const RemoteCapturedScope&,const std::string& request_id,RemoteRunSubmitAction,std::chrono::milliseconds,std::stop_token={});
    RemoteCommandRecord cancel_run(const RemoteCapturedScope&,const std::string& request_id,RemoteRunCancelAction,std::chrono::milliseconds,std::stop_token={});
    RemoteCommandRecord reconcile_command(const RemoteCapturedScope&,const std::string& request_id,std::chrono::milliseconds,std::stop_token={});
private:
    RemoteCommandRecord send_command(const RemoteCapturedScope&,const RemoteCommandIntent&,std::chrono::milliseconds,std::stop_token);
    RemoteStoredCredential load_scope(const RemoteCapturedScope&);
    ConnectionProfiles& profiles_;RemoteConnectionCredentials credentials_;RemoteNegotiationExchange exchange_;RemoteSessionReadExchange session_read_;RemoteCommandJournal commands_;RemoteSessionCommandExchange create_,receipt_;RemoteRunReadExchange run_read_;RemoteRunCommandExchange run_submit_,run_receipt_;RemoteRunCancelExchange run_cancel_,cancel_receipt_;RemoteRunEventExchange event_read_;RemoteEventCheckpoints event_checkpoints_;
};
}
