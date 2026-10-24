#pragma once
#include "agentflow/connection_profiles.hpp"
#include "agentflow/remote_negotiation.hpp"

namespace agentflow {
struct RemoteStoredCredential {
    std::int64_t revision;
    ConnectionProfile profile;
    RemoteNegotiatedGrant grant;
    SecretBytes credential;
};
// Backend-private storage, not an SDK value or authentication proof. Grant and
// credential share one encrypted record/revision. No profile selection occurs.
class RemoteConnectionCredentials {
public:
    RemoteConnectionCredentials(PersistenceService& store,ConnectionProfiles& profiles):store_(store),profiles_(profiles){}
    CredentialMetadata save(const ConnectionProfile&,const RemoteNegotiatedGrant&,const SecretBytes&,std::int64_t expected_credential_revision,std::int64_t now_unix_seconds);
    RemoteStoredCredential load(const ConnectionProfile&);
    // Authentication may inspect the previous pin after an explicit profile
    // edit, but cannot treat that old grant as admitted to the new profile.
    RemoteStoredCredential previous(const ConnectionProfile&);
private:
    PersistenceService& store_;ConnectionProfiles& profiles_;
};
}
