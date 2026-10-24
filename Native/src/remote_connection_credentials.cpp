#include "agentflow/remote_connection_credentials.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <algorithm>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t limit=80000;
std::string key(const ConnectionProfile& profile){return "remote-connection-"+profile.id;}
std::string purpose(const ConnectionProfile& profile){return "xmind.remote.v1:"+profile.id;}
std::span<const std::uint8_t> bytes(const std::string& value){return {reinterpret_cast<const std::uint8_t*>(value.data()),value.size()};}
void token(std::span<const std::uint8_t> value){if(value.empty()||value.size()>8192)throw std::invalid_argument("Invalid remote credential");for(const auto c:value)if(c<33||c>126)throw std::invalid_argument("Invalid remote credential");}
struct WipingBuffer {std::vector<std::uint8_t> value;~WipingBuffer(){if(!value.empty())SecureZeroMemory(value.data(),value.size());}};
RemoteStoredCredential decode(ResolvedCredential snapshot){
    const auto input=snapshot.secret.view();if(input.size()<5||input.size()>limit)throw DatabaseError("Invalid stored remote credential");
    std::uint32_t length=0;for(unsigned i=0;i<4;++i)length|=static_cast<std::uint32_t>(input[i])<<(8*i);
    if(length>65536||length>input.size()-4)throw DatabaseError("Invalid stored remote credential");
    try{
        const auto value=Json::parse(input.begin()+4,input.begin()+4+length);if(!value.is_object()||value.size()!=5||value.at("version")!=1||!value.at("profile_id").is_string()||!value.at("origin").is_string()||!value.at("profile_revision").is_number_integer())throw DatabaseError("Invalid stored remote credential");
        ConnectionProfile profile;profile.id=value.at("profile_id");profile.origin=value.at("origin");profile.revision=value.at("profile_revision");profile.kind=ConnectionKind::nexus;
        RemoteNegotiationRequirements requirements;requirements.now_unix_seconds=1;auto grant=validate_remote_negotiation(value.at("grant").dump(),requirements);const auto secret=input.subspan(4+length);token(secret);
        return {snapshot.metadata.revision,std::move(profile),std::move(grant),SecretBytes(secret)};
    }catch(const Json::exception&){throw DatabaseError("Invalid stored remote credential");}
}
}
CredentialMetadata RemoteConnectionCredentials::save(const ConnectionProfile& profile,const RemoteNegotiatedGrant& grant,const SecretBytes& credential,std::int64_t revision,std::int64_t now){
    auto admission=profiles_.admit_remote(profile);token(credential.view());RemoteNegotiationRequirements requirements;requirements.now_unix_seconds=now;requirements.expected_server_id=grant.server_id;
    const auto encoded_grant=Json::parse(encode_remote_negotiated_grant(grant));(void)validate_remote_negotiation(encoded_grant.dump(),requirements);
    if(revision>0){const auto previous=decode(store_.resolve_credential_snapshot("server",key(profile),purpose(profile)).get());if(previous.revision!=revision)throw Conflict("Remote credential revision changed");if(previous.profile.origin==profile.origin&&previous.grant.server_id!=grant.server_id)throw Conflict("Remote server identity changed; explicit new trust enrollment required");}
    const auto metadata=Json{{"version",1},{"profile_id",profile.id},{"profile_revision",profile.revision},{"origin",profile.origin},{"grant",encoded_grant}}.dump();
    if(metadata.size()>65536||4+metadata.size()+credential.view().size()>limit)throw std::invalid_argument("Remote credential record exceeds limits");WipingBuffer buffer;buffer.value.reserve(4+metadata.size()+credential.view().size());for(unsigned i=0;i<4;++i)buffer.value.push_back(static_cast<std::uint8_t>(metadata.size()>>(8*i)));buffer.value.insert(buffer.value.end(),bytes(metadata).begin(),bytes(metadata).end());buffer.value.insert(buffer.value.end(),credential.view().begin(),credential.view().end());
    return store_.put_credential("server",key(profile),purpose(profile),"Remote connection authorization",SecretBytes(buffer.value),revision).get();
}
RemoteStoredCredential RemoteConnectionCredentials::load(const ConnectionProfile& profile){
    auto admission=profiles_.admit_remote(profile);auto result=decode(store_.resolve_credential_snapshot("server",key(profile),purpose(profile)).get());
    if(result.profile.id!=profile.id||result.profile.origin!=profile.origin||result.profile.revision!=profile.revision)throw Conflict("Stored remote authorization belongs to another profile revision");return result;
}
RemoteStoredCredential RemoteConnectionCredentials::previous(const ConnectionProfile& profile){
    auto admission=profiles_.admit_remote(profile);auto result=decode(store_.resolve_credential_snapshot("server",key(profile),purpose(profile)).get());if(result.profile.id!=profile.id)throw Conflict("Stored remote credential identity differs");return result;
}
}
