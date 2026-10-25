#include "agentflow/remote_run_snapshot.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <atomic>
#include <iostream>
#include <thread>
using namespace agentflow;
using Json=nlohmann::json;
using namespace std::chrono_literals;
void check(bool v){if(!v)throw std::runtime_error("Snapshot transport contract failed");}
struct Peer {
 httplib::Server server;std::thread thread;std::string origin;
 std::atomic<unsigned> requests=0,redirects=0;std::atomic<int> mode=0;std::atomic_bool valid=true;
 Peer(){server.Get("/xmind/v1/scopes/scope:a/sessions/session:a/runs/run:a/snapshot",[&](const auto& request,auto& response){++requests;
  const bool next=request.has_param("snapshot_id");const std::string path="/xmind/v1/scopes/scope%3Aa/sessions/session%3Aa/runs/run%3Aa/snapshot";
  if(request.target!=(next?path+"?snapshot_id=snapshot%3Aa&offset=1":path)||!request.body.empty()||request.get_header_value("Authorization")!="Bearer snapshot-test-key"||request.get_header_value("X-XMind-Server-ID")!="server-a"||request.get_header_value("X-XMind-Scope-ID")!="scope:a"||request.get_header_value("X-XMind-Protocol-Version")!="1"||request.get_header_value("X-XMind-Grant-ID")!="grant-a"||request.get_header_value("X-XMind-Grant-Revision")!="2")valid=false;
  Json value={{"binding",{{"server_id","server-a"},{"scope_id","scope:a"},{"protocol_version",1},{"grant_id","grant-a"},{"grant_revision",2}}},{"snapshot",{{"id","snapshot:a"},{"server_id","server-a"},{"scope_id","scope:a"},{"session_id","session:a"},{"run_id","run:a"},{"epoch","epoch-a"},{"sequence",25},{"total_records",2},{"offset",next?1:0}}},{"records",Json::array({{{"ordinal",next?1:0},{"kind",next?"message":"run"},{"id",next?"message-a":"run:a"},{"revision",3},{"payload",Json::object()}}})},{"complete",next}};
  switch(mode.load()){case 1:response.status=307;response.set_header("Location",origin+"/redirect-target");return;case 2:response.status=403;response.set_content("private-marker","text/plain");return;case 3:response.set_content(std::string(65537,'x'),"application/json");return;case 4:value["binding"]["grant_revision"]=3;break;case 5:value["snapshot"]["sequence"]=26;break;case 6:response.set_content("{private-marker","application/json");return;}
  response.set_content(value.dump(),"application/json");
 });server.Get("/redirect-target",[&](const auto&,auto& response){++redirects;response.status=500;});const auto port=server.bind_to_any_port("127.0.0.1");check(port>0);origin="http://127.0.0.1:"+std::to_string(port);thread=std::thread([&]{server.listen_after_bind();});server.wait_until_ready();}
 ~Peer(){server.stop();if(thread.joinable())thread.join();}
};
int main(){try{
 Peer peer;const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();RemoteNegotiatedGrant grant{"server-a","grant-a",1,2,now+60,{{"scope:a",{"snapshots.read","runs.read","events.read"}}},{{"snapshots",1},{"runs",1},{"events",1}}};RemoteRunEventCursor resource{"connection-a","server-a","scope:a","session:a","run:a","",0};const std::string key="snapshot-test-key";SecretBytes credential({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()});
 auto read=[&](const std::optional<RemoteSnapshotPosition>& position=std::nullopt,std::stop_token stop={}){return read_remote_run_snapshot_page(peer.origin,credential,grant,resource,position,2s,RemoteTransportPolicy::isolated_loopback_test,stop);};
 const auto first=read();check(!first.complete&&first.records.size()==1);auto continuation=first.position;continuation.offset=1;check(read(continuation).complete);check(peer.requests==2&&peer.valid);
 unsigned refused=0;auto reject=[&](auto operation){try{operation();}catch(const std::exception& e){check(std::string(e.what()).find("private-marker")==std::string::npos);++refused;return;}throw std::runtime_error("Snapshot refusal missing");};
 for(int mode=1;mode<=6;++mode){peer.mode=mode;const auto count=peer.requests.load();reject([&]{read(continuation);});check(peer.requests==count+1);}check(peer.redirects==0);peer.mode=0;
 const auto count=peer.requests.load();reject([&]{read_remote_run_snapshot_page(peer.origin,credential,grant,resource,std::nullopt,2s);});
 auto denied=grant;denied.authorized_scopes[0].actions={"runs.read","events.read"};reject([&]{read_remote_run_snapshot_page(peer.origin,credential,denied,resource,std::nullopt,2s,RemoteTransportPolicy::isolated_loopback_test);});
 auto wrong=resource;wrong.run_id="../other";reject([&]{read_remote_run_snapshot_page(peer.origin,credential,grant,wrong,std::nullopt,2s,RemoteTransportPolicy::isolated_loopback_test);});auto invalid=continuation;invalid.offset=0;reject([&]{read(invalid);});reject([&]{read_remote_run_snapshot_page(peer.origin,credential,grant,resource,std::nullopt,0ms,RemoteTransportPolicy::isolated_loopback_test);});
 std::stop_source cancelled;cancelled.request_stop();reject([&]{read(std::nullopt,cancelled.get_token());});check(peer.requests==count&&peer.valid&&resource.sequence==0&&resource.epoch.empty());
 std::cout<<"Native snapshot transport: two exact encoded GET pages, "<<refused<<" refusals, no redirects/retries or cursor mutation. Controlled loopback only; native trusted TLS remains separate.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
