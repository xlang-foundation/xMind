#include "agentflow/a2a_agent_registry.hpp"
#include "agentflow/http_server.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class E,class F>void rejects(F&& action,const char* message){try{action();}catch(const E&){return;}throw std::runtime_error(message);}
agentflow::SecretBytes secret(const std::string& value){return agentflow::SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
bool equals(const agentflow::SecretBytes& value,const std::string& expected){const auto bytes=value.view();return bytes.size()==expected.size()&&std::equal(bytes.begin(),bytes.end(),reinterpret_cast<const std::uint8_t*>(expected.data()));}
struct Directory {std::filesystem::path path;Directory(){std::random_device random;path=std::filesystem::temp_directory_path()/std::filesystem::path("xmind-a2a-registry-"+std::to_string(random())+"-"+std::to_string(random()));if(!std::filesystem::create_directory(path))throw std::runtime_error("Cannot create A2A registry fixture directory");}~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}};
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try{
        Directory directory;const auto database=(directory.path/"registry.sqlite").string();const std::vector<std::string> imports{argv[1],argv[2]};const std::string first="fixture-a2a-bearer-initial",rotated="fixture-a2a-bearer-rotated";
        {
            agentflow::PersistenceService persistence(database,imports);agentflow::A2aRemoteAgentRegistry registry(persistence);
            require(registry.list().revision==0&&registry.list().entries.empty(),"Empty registry must have a stable initial snapshot");
            rejects<std::invalid_argument>([&]{registry.register_agent("research.insecure","http://agents.example.test/card.json",true,0);},"Remote cleartext A2A endpoints must be rejected at registration");
            const auto loopback=registry.register_agent("research.loopback","http://127.0.0.1:6550/card.json",false,0);require(loopback.revision==1,"Local loopback test peers may use HTTP");registry.remove(loopback.id,1);
            const auto added=registry.register_agent("research.alpha","https://agents.example.test/cards/alpha.json",true,0,secret(first));require(added.revision==1&&added.enabled&&added.has_bearer,"Owner registration must assign the first stable revision without exposing its credential");
            auto list=registry.list();require(list.revision==3&&list.entries.size()==1&&list.entries[0].id==added.id&&list.entries[0].has_bearer,"Remote agent catalogue must expose registered safe metadata");
            const auto stored=persistence.information("native-a2a-agents","catalog").get();require(stored.find(first)==std::string::npos&&stored.find("credential_id")!=std::string::npos,"The public registry record must contain only an opaque credential reference");
            auto credentials=persistence.credentials("a2a-remote-agents").get();require(credentials.size()==1&&credentials[0].purpose=="a2a.remote-agent.research.alpha","Bearer values must be stored through the scoped encrypted credential repository");
            {auto resolved=registry.resolve(added.id);require(resolved.bearer&&equals(*resolved.bearer,first)&&resolved.descriptor.revision==1,"Resolution must return the registered peer key only to the native owner");}
            rejects<agentflow::Conflict>([&]{registry.register_agent("research.alpha","https://other.example.test/card.json",true,0);},"Stale owner revision must not replace a registration");
            const auto disabled=registry.set_enabled(added.id,false,1);require(disabled.revision==2&&!disabled.enabled&&disabled.has_bearer,"Disabling a registration must preserve but prevent use of its scoped credential");rejects<std::invalid_argument>([&]{(void)registry.resolve(added.id);},"Disabled peers must not resolve for dispatch");
            const auto enabled=registry.set_enabled(added.id,true,2);require(enabled.revision==3&&enabled.enabled,"Re-enabling a peer must advance its owner revision");{auto resolved=registry.resolve(added.id);require(resolved.bearer&&equals(*resolved.bearer,first),"Enable changes must preserve the currently registered key");}
            const auto changed=registry.register_agent(added.id,"https://agents.example.test/cards/beta.json",true,3,secret(rotated));require(changed.revision==4&&changed.card_url.ends_with("beta.json"),"Endpoint and key rotation must publish together at one new registration revision");credentials=persistence.credentials("a2a-remote-agents").get();require(credentials.size()==1,"Replaced remote keys must be removed after new metadata is published");{auto resolved=registry.resolve(added.id);require(resolved.bearer&&equals(*resolved.bearer,rotated),"Registry resolution must match the committed key rotation");}
            const auto public_peer=registry.register_agent("research.public","https://agents.example.test/public.json",true,0);require(!public_peer.has_bearer,"Public peers must not acquire credentials by default");const auto public_resolved=registry.resolve(public_peer.id);require(!public_resolved.bearer,"Public peer resolution must remain credential-free");
            const auto cleared=registry.register_agent(added.id,changed.card_url,true,4,{},true);require(cleared.revision==5&&!cleared.has_bearer&&persistence.credentials("a2a-remote-agents").get().empty(),"Credential removal must sever and retire its encrypted value");
            rejects<agentflow::Conflict>([&]{registry.remove(added.id,4);},"Removal must reject stale revisions");registry.remove(added.id,5);require(registry.list().entries.size()==1,"Removal must retain the other registered remote agent");rejects<agentflow::Conflict>([&]{registry.register_agent(added.id,changed.card_url,true,0);},"Retired remote agent IDs must not be reused");
            persistence.close();
        }
        {
            agentflow::PersistenceService persistence(database,imports);agentflow::A2aRemoteAgentRegistry reopened(persistence);const auto snapshot=reopened.list();require(snapshot.revision==9&&snapshot.entries.size()==1&&snapshot.entries[0].id=="research.public","Owner registrations must survive embedded-xlang3 SQLite reopen");require(!reopened.resolve("research.public").bearer,"Public registration must remain secret-free after reopen");persistence.close();
        }
        {
            agentflow::PersistenceService persistence(database,imports);const std::string token="synthetic-a2a-registry-owner-token-32-bytes";agentflow::HttpServer server(persistence,token);const auto port=server.bind(0);require(port>0,"A2A registry API failed to bind");std::thread worker([&]{server.listen();});
            try{
                httplib::Client client("127.0.0.1",port);client.set_connection_timeout(2,0);client.set_read_timeout(5,0);const httplib::Headers headers{{"Authorization","Bearer "+token}};
                std::shared_ptr<httplib::Response> response;for(int attempt=0;attempt<30;++attempt){response=client.Get("/v1/a2a/agents",headers);if(response)break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}require(response&&response->status==200,"Authenticated A2A registry listing failed");auto listing=Json::parse(response->body);require(listing["revision"]==9&&listing["agents"].size()==1&&listing["agents"][0]["id"]=="research.public","HTTP listing must expose the persisted registry snapshot");
                const std::string view_origin="http://127.0.0.1:54321";auto enrolled=client.Post("/v1/view-sessions",headers,Json{{"origin",view_origin}}.dump(),"application/json");require(enrolled&&enrolled->status==200,"Local browser session enrollment failed");const auto view_credential=Json::parse(enrolled->body).at("credential").get<std::string>();const httplib::Headers view_headers{{"Authorization","View "+view_credential},{"X-XMind-View-Origin",view_origin}};auto view_listing=client.Get("/v1/a2a/agents",view_headers);require(view_listing&&view_listing->status==200,"Same-origin local view session must be able to use the A2A registry API");
                auto unauthorized=client.Get("/v1/a2a/agents");require(unauthorized&&unauthorized->status==401,"A2A owner registry must require owner authentication");
                auto post=[&](const std::string& path,const Json& input){return client.Post(path,headers,input.dump(),"application/json");};
                auto created=post("/v1/a2a/agents/register",Json{{"id","research.http"},{"card_url","https://agents.example.test/http.json"},{"enabled",true},{"expected_agent_revision",0},{"bearer_token","fixture-http-only-secret"}});require(created&&created->status==200,"A2A registration HTTP endpoint failed");require(created->body.find("fixture-http-only-secret")==std::string::npos,"A2A registration response leaked its bearer value");auto safe=Json::parse(created->body)["agent"];require(safe["has_bearer"]==true&&safe["enabled"]==true,"A2A registration must return safe credential-presence metadata");
                auto catalog=client.Get("/v1/a2a/agents",headers);require(catalog&&catalog->status==200&&catalog->body.find("fixture-http-only-secret")==std::string::npos,"A2A catalog must never return bearer values");
                auto stale=post("/v1/a2a/agents/register",Json{{"id","research.http"},{"card_url","https://agents.example.test/other.json"},{"enabled",true},{"expected_agent_revision",0}});require(stale&&stale->status==409,"A2A HTTP registration must enforce expected revisions");
                auto disabled=post("/v1/a2a/agents/research.http/enabled",Json{{"enabled",false},{"expected_agent_revision",1}});require(disabled&&disabled->status==200&&Json::parse(disabled->body)["agent"]["enabled"]==false,"A2A HTTP enablement must update with optimistic concurrency");
                auto removed=post("/v1/a2a/agents/research.http/remove",Json{{"expected_agent_revision",2}});require(removed&&removed->status==204,"A2A HTTP removal must honor the current registration revision");
                auto malformed=client.Post("/v1/a2a/agents/register?next=1",headers,"{}","application/json");require(malformed&&malformed->status==400,"Malformed A2A query must not be accepted");
            }catch(...){server.stop();worker.join();throw;}server.stop();worker.join();persistence.close();
        }
        std::cout<<"Native A2A registry passed xlang3 SQLite persistence, authenticated owner HTTP registration/list/enable/remove, revision conflicts, encrypted bearer references, no-secret responses, key rotation/clearing, disable/remove semantics, retired IDs and restart resolution. Remote agent dispatch and cross-client APIs remain open.\n";
    }catch(const std::exception& error){std::cerr<<"Native A2A registry contract failed: "<<error.what()<<"\n";return 1;}
    return 0;
}
