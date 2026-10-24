#include "agentflow/remote_connection_authentication.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <random>
using namespace agentflow;
using Json=nlohmann::json;
using namespace std::chrono_literals;
namespace {
void require(bool value){if(!value)throw std::runtime_error("Remote authentication contract failed");}
template<class E,class F>void rejects(F action){try{action();}catch(const E&){return;}throw std::runtime_error("Expected authentication refusal missing");}
SecretBytes secret(){const std::string value="synthetic-auth-service-credential";return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
std::string source(std::string origin,std::string name="Remote"){return Json{{"profiles",Json::array({{{"id","local"},{"name","Local"},{"kind","local"}},{{"id","remote-a"},{"name",name},{"kind","nexus"},{"origin",origin}}})}}.dump();}
}
int main(int argc,char** argv){if(argc!=3)return 2;std::filesystem::path file;try{
    file=std::filesystem::temp_directory_path()/("xmind-remote-auth-"+std::to_string(std::random_device{}())+".sqlite");require(!std::filesystem::exists(file));ConnectionProfile final_profile;
    {
        PersistenceService store(file.string(),{argv[1],argv[2]});ConnectionProfiles profiles(store);profiles.load_or_initialize();auto catalog=profiles.apply(source("https://remote.example.invalid"),1);auto profile=catalog.profiles.at(1);RemoteConnectionCredentials credentials(store,profiles);unsigned calls=0;int mode=0;std::stop_source stop;
        RemoteConnectionAuthentication authentication(store,profiles,[&](const auto& origin,const auto& credential,auto requirements,auto timeout,auto){
            ++calls;require(!credential.view().empty()&&timeout==2s&&requirements.required_capabilities==std::map<std::string,std::int64_t>{{"sessions",1},{"runs",1},{"events",1},{"receipts",1}});
            if(calls==1)require(!requirements.expected_server_id);else if(origin=="https://remote.example.invalid")require(requirements.expected_server_id=="server-a");else require(!requirements.expected_server_id);
            if(mode==1)throw TransportError("Synthetic failed exchange");
            if(mode==4)stop.request_stop();
            if(mode==5){const auto current=profiles.load();profiles.apply(source(origin,"Changed during negotiation"),current.revision);}
            RemoteNegotiatedGrant result;result.server_id=mode==2?"wrong-server":origin=="https://remote.example.invalid"?"server-a":"server-b";result.grant_id="private-grant";result.protocol_version=1;result.grant_revision=1;result.expires_at=requirements.now_unix_seconds+60;result.authorized_scopes={RemoteAuthorizedScope{"workspace-a",{"runs.submit"}}};result.capabilities=requirements.required_capabilities;if(mode==3)result.capabilities.erase("receipts");return result;
        });
        const auto absent=authentication.inspect(profile.id,profile.revision);require(absent.state=="absent"&&absent.credential_revision==0&&!absent.server_id&&calls==0);
        const auto accepted=authentication.authenticate(profile.id,profile.revision,0,secret(),2s);require(accepted.credential_revision==1&&accepted.server_id=="server-a"&&profiles.load().active_id=="local");const auto stored_status=authentication.inspect(profile.id,profile.revision);require(stored_status.state=="stored"&&stored_status.credential_revision==1&&stored_status.server_id=="server-a"&&calls==1);
        for(const auto failure:{1,2,3,4}){mode=failure;if(mode==1)rejects<TransportError>([&]{authentication.authenticate(profile.id,profile.revision,1,secret(),2s);});if(mode==2||mode==3)rejects<RemoteNegotiationError>([&]{authentication.authenticate(profile.id,profile.revision,1,secret(),2s);});if(mode==4)rejects<TransportCancelled>([&]{authentication.authenticate(profile.id,profile.revision,1,secret(),2s,stop.get_token());});require(credentials.load(profile).revision==1&&credentials.load(profile).grant.server_id=="server-a");}
        mode=0;require(authentication.authenticate(profile.id,profile.revision,1,secret(),2s).credential_revision==2);const auto before=calls;
        rejects<Conflict>([&]{authentication.authenticate(profile.id,profile.revision,1,secret(),2s);});rejects<Conflict>([&]{authentication.authenticate(profile.id,profile.revision+1,2,secret(),2s);});rejects<Conflict>([&]{authentication.authenticate("local",1,0,secret(),2s);});require(calls==before);
        mode=5;rejects<Conflict>([&]{authentication.authenticate(profile.id,profile.revision,2,secret(),2s);});const auto old_profile=profile;catalog=profiles.load();profile=catalog.profiles.at(1);require(credentials.previous(profile).revision==2);rejects<Conflict>([&]{credentials.load(profile);});const auto stale=authentication.inspect(profile.id,profile.revision);require(stale.state=="stale_profile"&&stale.credential_revision==2&&!stale.server_id&&!stale.expires_at);rejects<Conflict>([&]{authentication.inspect(old_profile.id,old_profile.revision);});
        mode=0;require(authentication.authenticate(profile.id,profile.revision,2,secret(),2s).credential_revision==3);
        catalog=profiles.apply(source("https://replacement.example.invalid"),catalog.revision);profile=catalog.profiles.at(1);const auto changed=authentication.authenticate(profile.id,profile.revision,3,secret(),2s);require(changed.credential_revision==4&&changed.server_id=="server-b");final_profile=profile;
        require(calls==9&&profiles.load().active_id=="local");rejects<Conflict>([&]{profiles.select(profile.id,catalog.revision);});store.close();
    }
    {
        PersistenceService store(file.string(),{argv[1],argv[2]});ConnectionProfiles profiles(store);RemoteConnectionCredentials credentials(store,profiles);const auto stored=credentials.load(final_profile);require(stored.revision==4&&stored.grant.server_id=="server-b"&&std::ranges::equal(stored.credential.view(),secret().view())&&profiles.load().active_id=="local");RemoteConnectionAuthentication observer(store,profiles,[](const auto&,const auto&,auto,auto,auto)->RemoteNegotiatedGrant{throw std::runtime_error("Observation cannot negotiate");});const auto observed=observer.inspect(final_profile.id,final_profile.revision);require(observed.state=="stored"&&observed.credential_revision==4&&observed.server_id=="server-b");auto expired=stored.grant;expired.expires_at=2;credentials.save(final_profile,expired,stored.credential,4,1);const auto expired_status=observer.inspect(final_profile.id,final_profile.revision);require(expired_status.state=="expired"&&expired_status.credential_revision==5&&expired_status.expires_at==2&&credentials.load(final_profile).revision==5);rejects<Conflict>([&]{observer.inspect("local",profiles.load().profiles.front().revision);});rejects<NotFound>([&]{observer.inspect("absent",1);});store.close();
    }
    std::cout<<"Native authentication service integrates nine controlled injected negotiation outcomes with real xlang3/SQLite encrypted publication. Read-only observation covers absent/stored/stale-profile/expired states, redacted stale identity, retained revision after database reopen and no exchange. No actual TLS/network exchange in this service fixture; standalone HTTP peer coverage is separate. Remote selection remains disabled.\n";
    for(const auto* suffix:{"","-wal","-shm"}){auto candidate=file;candidate+=suffix;std::error_code ignored;std::filesystem::remove(candidate,ignored);}return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"; retained fixture "<<file.string()<<'\n';return 1;}}
