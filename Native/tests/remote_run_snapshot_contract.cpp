#include "agentflow/remote_run_snapshot.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <iostream>
using namespace agentflow;
using Json=nlohmann::json;
void check(bool v){if(!v)throw std::runtime_error("Snapshot contract failed");}
int main(){try{
 RemoteNegotiatedGrant grant{"server-a","grant-a",1,2,2000,{{"scope-a",{"snapshots.read","runs.read","events.read"}}},{{"snapshots",1},{"runs",1},{"events",1}}};
 RemoteRunEventCursor resource{"connection-a","server-a","scope-a","session-a","run-a","old-epoch",15};
 Json good={{"binding",{{"server_id","server-a"},{"scope_id","scope-a"},{"protocol_version",1},{"grant_id","grant-a"},{"grant_revision",2}}},{"snapshot",{{"id","snapshot-a"},{"server_id","server-a"},{"scope_id","scope-a"},{"session_id","session-a"},{"run_id","run-a"},{"epoch","new-epoch"},{"sequence",30},{"total_records",2},{"offset",0}}},{"records",Json::array({{{"ordinal",0},{"kind","run"},{"id","run-a"},{"revision",3},{"payload",{{"state","running"}}}}})},{"complete",false}};
 const auto page=validate_remote_run_snapshot_page(good.dump(),grant,resource,1000);check(page.cursor.epoch=="new-epoch"&&page.cursor.sequence==30&&!page.complete&&page.records.size()==1&&page.records[0].id=="run-a");
 auto next=good;next["snapshot"]["offset"]=1;next["records"][0]={{"ordinal",1},{"kind","message"},{"id","message-a"},{"revision",1},{"payload",{{"role","user"},{"content","observed"}}}};next["complete"]=true;
 auto continuation=page.position;continuation.offset=1;const auto end=validate_remote_run_snapshot_page(next.dump(),grant,resource,1000,continuation);check(end.complete&&end.records[0].kind=="message");
 unsigned refused=0;const auto reject=[&](const Json& value,const std::optional<RemoteSnapshotPosition>& cursor=std::nullopt){try{validate_remote_run_snapshot_page(value.dump(),grant,resource,1000,cursor);}catch(const std::exception& e){check(std::string(e.what()).find("private-marker")==std::string::npos);++refused;return;}throw std::runtime_error("Missing snapshot refusal");};
 auto changed=good;changed["binding"]["grant_revision"]=true;reject(changed);changed=good;changed["binding"]["scope_id"]="other";reject(changed);changed=good;changed["snapshot"]["run_id"]="other";reject(changed);changed=good;changed["snapshot"]["offset"]=1;reject(changed);changed=good;changed["snapshot"]["sequence"]=true;reject(changed);changed=good;changed["snapshot"]["total_records"]=4097;reject(changed);changed=good;changed["complete"]=true;reject(changed);changed=good;changed["complete"]=0;reject(changed);changed=good;changed["records"]=Json::array();reject(changed);changed=good;changed["records"][0]["ordinal"]=1;reject(changed);changed=good;changed["records"][0]["kind"]="unknown";reject(changed);changed=good;changed["records"][0]["id"]="foreign-run";reject(changed);changed=good;changed["records"][0]["payload"]="private-marker";reject(changed);changed=good;changed["snapshot"]["extra"]=1;reject(changed);
 changed=next;changed["snapshot"]["epoch"]="other";reject(changed,continuation);changed=next;changed["snapshot"]["sequence"]=31;reject(changed,continuation);changed=next;changed["snapshot"]["id"]="other";reject(changed,continuation);changed=next;changed["snapshot"]["total_records"]=3;reject(changed,continuation);reject(next);
 changed=good;changed["snapshot"]["epoch"]="old-epoch";changed["snapshot"]["sequence"]=14;reject(changed);
 changed=good;changed["records"][0]["revision"]=true;reject(changed);
 changed=good;changed["records"][0]["kind"]="message";reject(changed);
 changed=next;changed["records"][0]["kind"]="run";changed["records"][0]["id"]="run-a";reject(changed,continuation);
 changed=good;changed["snapshot"]["total_records"]=3;changed["records"].push_back({{"ordinal",1},{"kind","message"},{"id","same"},{"revision",1},{"payload",Json::object()}});changed["records"].push_back({{"ordinal",2},{"kind","message"},{"id","same"},{"revision",1},{"payload",Json::object()}});changed["complete"]=true;reject(changed);
 auto duplicate=good.dump();duplicate.insert(1,"\"complete\":true,");try{validate_remote_run_snapshot_page(duplicate,grant,resource,1000);throw std::logic_error("Duplicate accepted");}catch(const TransportError&){++refused;}
 for(const auto* action:{"snapshots.read","runs.read","events.read"}){auto denied=grant;auto& a=denied.authorized_scopes[0].actions;a.erase(std::remove(a.begin(),a.end(),action),a.end());try{validate_remote_run_snapshot_page(good.dump(),denied,resource,1000);throw std::logic_error("Unauthorized accepted");}catch(const TransportError&){++refused;}}
 auto expired=grant;expired.expires_at=999;try{validate_remote_run_snapshot_page(good.dump(),expired,resource,1000);throw std::logic_error("Expired accepted");}catch(const RemoteNegotiationError&){++refused;}
 try{validate_remote_run_snapshot_page(std::string(65537,' '),grant,resource,1000);throw std::logic_error("Oversize accepted");}catch(const TransportError&){++refused;}
 check(resource.epoch=="old-epoch"&&resource.sequence==15);std::cout<<"Native snapshot page validation: initial/continuation, replacement epoch, stable watermark, resource/grant identity and "<<refused<<" refusals. No fetch, persistence, cursor reset or routing claimed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
