#pragma once
#include "agentflow/remote_connection_client.hpp"
#include "agentflow/remote_connection_credentials.hpp"
#include <functional>

namespace agentflow {
// Native connector boundary. An injected exchange is trusted backend code,
// never a caller-selected SDK callback or a bypass supplied by a UI.
using RemoteNegotiationExchange=std::function<RemoteNegotiatedGrant(const std::string&,const SecretBytes&,RemoteNegotiationRequirements,std::chrono::milliseconds,std::stop_token)>;
struct RemoteAuthenticationReceipt {
    std::string connection_id,server_id;
    std::int64_t profile_revision,credential_revision,protocol_version,expires_at;
};
class RemoteConnectionAuthentication {
public:
    RemoteConnectionAuthentication(PersistenceService&,ConnectionProfiles&);
    RemoteConnectionAuthentication(PersistenceService&,ConnectionProfiles&,RemoteNegotiationExchange);
    RemoteAuthenticationReceipt authenticate(const std::string& profile_id,std::int64_t expected_profile_revision,
        std::int64_t expected_credential_revision,const SecretBytes& credential,std::chrono::milliseconds timeout,std::stop_token stop={});
private:
    ConnectionProfiles& profiles_;RemoteConnectionCredentials credentials_;RemoteNegotiationExchange exchange_;
};
}
