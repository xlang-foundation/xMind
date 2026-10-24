#include "agentflow/remote_connection_authentication.hpp"
#include "agentflow/http_stream_transport.hpp"
#include <algorithm>

namespace agentflow {
namespace {std::int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}}
RemoteConnectionAuthentication::RemoteConnectionAuthentication(PersistenceService& store,ConnectionProfiles& profiles):RemoteConnectionAuthentication(store,profiles,[](const auto& origin,const auto& credential,auto requirements,auto timeout,auto stop){return negotiate_remote_connection(origin,credential,std::move(requirements),timeout,RemoteTransportPolicy::verified_https,stop);}){}
RemoteConnectionAuthentication::RemoteConnectionAuthentication(PersistenceService& store,ConnectionProfiles& profiles,RemoteNegotiationExchange exchange):profiles_(profiles),credentials_(store,profiles),exchange_(std::move(exchange)){if(!exchange_)throw std::invalid_argument("Remote negotiation exchange is required");}
RemoteAuthenticationStatus RemoteConnectionAuthentication::inspect(const std::string& id,std::int64_t revision){
    const auto catalog=profiles_.load();const auto found=std::find_if(catalog.profiles.begin(),catalog.profiles.end(),[&](const auto& profile){return profile.id==id;});if(found==catalog.profiles.end())throw NotFound("Remote connection profile not found");const auto profile=*found;if(profile.revision!=revision)throw Conflict("Remote connection profile revision changed");
    RemoteAuthenticationStatus status;status.connection_id=profile.id;status.profile_revision=profile.revision;std::optional<RemoteStoredCredential> stored;
    try{stored=credentials_.previous(profile);}catch(const NotFound&){}
    auto admission=profiles_.admit_remote(profile);if(!stored)return status;status.credential_revision=stored->revision;
    if(stored->profile.origin!=profile.origin||stored->profile.revision!=profile.revision){status.state="stale_profile";return status;}
    status.state=stored->grant.expires_at<=now()?"expired":"stored";status.server_id=stored->grant.server_id;status.protocol_version=stored->grant.protocol_version;status.expires_at=stored->grant.expires_at;return status;
}
RemoteAuthenticationReceipt RemoteConnectionAuthentication::authenticate(const std::string& id,std::int64_t revision,std::int64_t credential_revision,const SecretBytes& credential,std::chrono::milliseconds timeout,std::stop_token stop){
    if(credential_revision<0||credential_revision==INT64_MAX||timeout.count()<=0||timeout>std::chrono::minutes(2)||credential.view().empty()||credential.view().size()>8192)throw std::invalid_argument("Invalid remote authentication request");for(const auto c:credential.view())if(c<33||c>126)throw std::invalid_argument("Invalid remote credential");
    const auto catalog=profiles_.load();const auto found=std::find_if(catalog.profiles.begin(),catalog.profiles.end(),[&](const auto& profile){return profile.id==id;});if(found==catalog.profiles.end())throw NotFound("Remote connection profile not found");const auto profile=*found;if(profile.revision!=revision)throw Conflict("Remote connection profile revision changed");{auto admission=profiles_.admit_remote(profile);}
    RemoteNegotiationRequirements requirements;requirements.now_unix_seconds=now();requirements.required_capabilities={{"sessions",1},{"runs",1},{"events",1},{"receipts",1}};
    try{auto previous=credentials_.previous(profile);if(previous.revision!=credential_revision)throw Conflict("Remote credential revision changed");if(previous.profile.origin==profile.origin)requirements.expected_server_id=previous.grant.server_id;}
    catch(const NotFound&){if(credential_revision!=0)throw;}
    if(stop.stop_requested())throw TransportCancelled("Remote authentication cancelled before negotiation");
    const auto negotiated=exchange_(profile.origin,credential,requirements,timeout,stop);if(stop.stop_requested())throw TransportCancelled("Remote authentication cancelled before publication");requirements.now_unix_seconds=now();const auto grant=validate_remote_negotiation(encode_remote_negotiated_grant(negotiated),requirements);
    const auto saved=credentials_.save(profile,grant,credential,credential_revision,requirements.now_unix_seconds);return {profile.id,grant.server_id,profile.revision,saved.revision,grant.protocol_version,grant.expires_at};
}
}
