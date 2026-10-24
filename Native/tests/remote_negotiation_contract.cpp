#include "agentflow/remote_negotiation.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <limits>
using namespace agentflow;
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("Remote negotiation contract failed");}
int main(){try{
    RemoteNegotiationRequirements requirements;requirements.now_unix_seconds=1000;requirements.expected_server_id="server-a";requirements.required_capabilities={{"sessions",1},{"runs",1},{"events",1}};
    const Json good={{"server_id","server-a"},{"protocol_version",1},{"grant_id","grant-a"},{"grant_revision",3},{"expires_at",1100},{"authorized_scopes",Json::array({{{"id","workspace-a"},{"actions",Json::array({"sessions.read","runs.submit"})}}})},{"capabilities",{{"sessions",1},{"runs",1},{"events",1}}}};
    const auto grant=validate_remote_negotiation(good.dump(),requirements);require(grant.server_id=="server-a"&&grant.grant_id=="grant-a"&&grant.grant_revision==3&&grant.expires_at==1100&&grant.authorized_scopes.size()==1&&grant.authorized_scopes[0].actions.size()==2);
    auto first=requirements;first.expected_server_id.reset();require(validate_remote_negotiation(good.dump(),first).server_id=="server-a");
    unsigned rejected=0;const auto rejects=[&](const std::string& body,RemoteNegotiationFailure expected=RemoteNegotiationFailure::malformed){try{validate_remote_negotiation(body,requirements);}catch(const RemoteNegotiationError& e){require(e.reason()==expected&&std::string(e.what()).find("private-test-secret")==std::string::npos);++rejected;return;}throw std::runtime_error("Expected remote refusal missing");};
    auto changed=good;changed["server_id"]="server-b";rejects(changed.dump(),RemoteNegotiationFailure::server_mismatch);
    changed=good;changed["protocol_version"]=2;rejects(changed.dump(),RemoteNegotiationFailure::unsupported_version);
    auto future=requirements;future.supported_versions={1,2};bool future_refused=false;try{validate_remote_negotiation(changed.dump(),future);}catch(const RemoteNegotiationError& e){future_refused=e.reason()==RemoteNegotiationFailure::unsupported_version;}require(future_refused);
    for(const auto expiry:{999,1000}){changed=good;changed["expires_at"]=expiry;rejects(changed.dump(),RemoteNegotiationFailure::expired_grant);}
    for(const auto* name:{"runs","events","sessions"}){changed=good;changed["capabilities"].erase(name);rejects(changed.dump(),RemoteNegotiationFailure::missing_capability);changed=good;changed["capabilities"][name]=2;rejects(changed.dump(),RemoteNegotiationFailure::missing_capability);}
    for(const auto* name:{"server_id","protocol_version","grant_id","grant_revision","expires_at","authorized_scopes","capabilities"}){changed=good;changed.erase(name);rejects(changed.dump());}
    for(const auto* name:{"protocol_version","grant_revision","expires_at"})for(const auto& invalid:Json::array({true,0,-1,1.5,"1",nullptr,std::numeric_limits<std::uint64_t>::max()})){changed=good;changed[name]=invalid;rejects(changed.dump());}
    for(const auto& invalid:Json::array({"","contains space","private-test-secret\n",std::string(257,'a'),"a/b"})){changed=good;changed["grant_id"]=invalid;rejects(changed.dump());}
    changed=good;changed["access_token"]="private-test-secret";rejects(changed.dump());changed=good;changed["authorized_scopes"]=Json::array();rejects(changed.dump());
    changed=good;changed["authorized_scopes"].push_back(changed["authorized_scopes"][0]);rejects(changed.dump());changed=good;changed["authorized_scopes"][0]["actions"].push_back("runs.submit");rejects(changed.dump());
    changed=good;changed["authorized_scopes"][0]["actions"]=Json::array();rejects(changed.dump());changed=good;changed["authorized_scopes"][0]["role"]="admin";rejects(changed.dump());
    changed=good;changed["capabilities"]["runs"]=true;rejects(changed.dump());
    changed=good;for(unsigned i=0;i<129;++i)changed["capabilities"]["extra-"+std::to_string(i)]=1;rejects(changed.dump());
    changed=good;for(unsigned i=0;i<129;++i)changed["authorized_scopes"].push_back({{"id","extra-"+std::to_string(i)},{"actions",Json::array({"sessions.read"})}});rejects(changed.dump());
    changed=good;for(unsigned i=0;i<65;++i)changed["authorized_scopes"][0]["actions"].push_back("extra-"+std::to_string(i));rejects(changed.dump());
    auto duplicate=good.dump();duplicate.insert(1,"\"server_id\":\"private-test-secret\",");rejects(duplicate);
    auto nested_duplicate=good.dump();const auto at=nested_duplicate.find("\"capabilities\":{");require(at!=std::string::npos);nested_duplicate.insert(at+16,"\"runs\":1,");rejects(nested_duplicate);
    rejects("{private-test-secret");rejects(std::string(65537,' '));rejects("[]");
    auto invalid=requirements;invalid.supported_versions={1,1};bool invalid_rejected=false;try{validate_remote_negotiation(good.dump(),invalid);}catch(const std::invalid_argument&){invalid_rejected=true;}require(invalid_rejected);
    std::cout<<"Native proposed remote negotiation validates pinned identity, exact offered versions/capabilities, expiry, authorized scope shape and bounded duplicate-free JSON; "<<rejected<<" malformed/incompatible responses rejected. No transport, authentication, persistence or remote activation claimed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
