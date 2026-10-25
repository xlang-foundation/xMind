#pragma once
#include "agentflow/remote_connection_client.hpp"
#include "agentflow/remote_connection_credentials.hpp"
#include <functional>

namespace agentflow {
// Native connector boundary. An injected exchange is trusted backend code,
// never a caller-selected SDK callback or a bypass supplied by a UI.
using RemoteNegotiationExchange=std::function<RemoteNegotiatedGrant(const std::string&,const SecretBytes&,RemoteNegotiationRequirements,std::chrono::milliseconds,std::stop_token)>;
using RemoteSessionReadExchange=std::function<RemoteObservedSession(const std::string&,const SecretBytes&,const RemoteNegotiatedGrant&,const std::string&,const std::string&,std::chrono::milliseconds,std::stop_token)>;
// Backend-only captured state; SDK objects expose a separate public identity.
using RemoteSessionCommandExchange=std::function<RemoteCommandObservation(const std::string&,const SecretBytes&,const RemoteNegotiatedGrant&,const std::string&,const std::string&,const std::string&,std::chrono::milliseconds,std::stop_token)>;
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
    RemoteConnectionAuthentication(PersistenceService&,ConnectionProfiles&,RemoteNegotiationExchange,RemoteSessionReadExchange = {},RemoteSessionCommandExchange create = {},RemoteSessionCommandExchange receipt = {});
    RemoteAuthenticationReceipt authenticate(const std::string& profile_id,std::int64_t expected_profile_revision,
        std::int64_t expected_credential_revision,const SecretBytes& credential,std::chrono::milliseconds timeout,std::stop_token stop={});
    // Local encrypted-record observation only; no live revocation check,
    // mutation retry authority or attribution to a particular request.
    RemoteAuthenticationStatus inspect(const std::string& profile_id,std::int64_t expected_profile_revision);
    RemoteCapturedScope open_scope(const std::string& profile_id,std::int64_t expected_profile_revision,const std::string& scope_id);
    void validate_scope(const RemoteCapturedScope&);
    RemoteObservedSession read_session(const RemoteCapturedScope&,const std::string& session_id,std::chrono::milliseconds timeout,std::stop_token stop={});
    RemoteCommandRecord create_session(const RemoteCapturedScope&,const std::string& request_id,const std::string& title,std::chrono::milliseconds,std::stop_token={});
    RemoteCommandRecord reconcile_session(const RemoteCapturedScope&,const std::string& request_id,std::chrono::milliseconds,std::stop_token={});
private:
    RemoteStoredCredential load_scope(const RemoteCapturedScope&);
    ConnectionProfiles& profiles_;RemoteConnectionCredentials credentials_;RemoteNegotiationExchange exchange_;RemoteSessionReadExchange session_read_;RemoteCommandJournal commands_;RemoteSessionCommandExchange create_,receipt_;
};
}
