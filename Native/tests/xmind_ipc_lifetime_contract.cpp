#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "agentflow/persistence_service.hpp"
#include "xlang3/xlang3.h"
#include <filesystem>
#include <future>
#include <iostream>
#include <random>
using namespace agentflow;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(int argc,char** argv){if(argc!=3)return 2;std::filesystem::path folder;try{
    folder=std::filesystem::temp_directory_path()/("xmind-api-lifetime-"+std::to_string(std::random_device{}()));check(std::filesystem::create_directory(folder),"Fresh API fixture directory required");
    auto store=std::make_unique<PersistenceService>((folder/"state.sqlite").string(),std::vector<std::string>{argv[1],argv[2]});
    auto first=std::async(std::launch::async,[]{ensure_xmind_ipc_api();}),second=std::async(std::launch::async,[]{ensure_xmind_ipc_api();});first.get();second.get();
    const auto endpoint="lrpc:"+std::to_string(xmind_ipc_port());X::Runtime client;auto root=client.ImportRemote("xmind",endpoint.c_str());
    auto text=[&](const std::string& value){return X::Value(client,value);};
    auto call=[&](const X::Value& method,std::vector<X::Value> args){X::Value out;check(method.Call(args,out),"Native API method failed");return out;};
    auto refused=[&](const X::Value& method,std::vector<X::Value> args){X::Value out;check(!method.Call(args,out),"Closed or unauthorized service unexpectedly accepted a call");};
    auto host=call(root["Host"],{});check(call(host["ApiVersion"],{}).ToInt64()==1,"Process root must exist before public service binding");
    const std::string token_one(64,'a'),token_two(64,'b');refused(root["Sessions"],{text(token_one)});X::Value stale,stale_connections,captured,bound_sessions,bound_runs,bound_events,cursor;std::string server_identity;
    {
        XMindIpcServer owner(*store,nullptr,token_one);check(owner.endpoint()==endpoint,"Public binding must reuse the registered process listener");
        stale_connections=call(root["Connections"],{text(token_one)});check(call(stale_connections["List"],{})["active_id"].ToString()=="local","Connection catalog must bind the actual backend");
        stale=call(root["Sessions"],{text(token_one)});auto created=call(stale["Create"],{text("persisted"),text("Scoped native API")});check(created["id"].ToString()=="persisted","Native session creation must use the bound persistence owner");
        captured=call(stale_connections["Open"],{text("local"),X::Value(client,1)});auto identity=call(captured["Identity"],{});server_identity=identity["server_id"].ToString();bound_sessions=call(captured["Sessions"],{});auto bound=call(bound_sessions["Create"],{text("captured-session"),text("Captured session")});check(bound["session"]["id"].ToString()=="captured-session","Captured session creation failed");
        store->create_run("captured-run","captured-session").get();store->append_event("captured-run","fixture.event","{}").get();bound_runs=call(captured["Runs"],{});check(call(bound_runs["Open"],{text("captured-run")})["run"]["session_id"].ToString()=="captured-session","Captured run read failed");
        bound_events=call(captured["Events"],{text("captured-session"),text("captured-run")});cursor=call(bound_events["Cursor"],{});check(call(bound_events["Read"],{cursor})["events"].Size()>0,"Captured events must read actual persisted records");cursor.Set("session_id",text("persisted"));refused(bound_events["Read"],{cursor});cursor.Set("session_id",text("captured-session"));
        auto desired=client.List(),local=client.Dict();local.Set("id",text("local"));local.Set("name",text("Changed Local"));local.Set("kind",text("local"));desired.Append(local);call(stale_connections["Save"],{desired,X::Value(client,1)});
        refused(bound_sessions["Create"],{text("stale-admission"),text("must not exist")});refused(stale["Open"],{text("stale-admission")});check(call(bound_sessions["Open"],{text("captured-session")})["session"]["title"].ToString()=="Captured session","Profile change must preserve existing captured reads");check(call(captured["Identity"],{})["profile_revision"].ToInt64()==1,"Captured identity must remain immutable");check(call(bound_events["Read"],{cursor})["events"].Size()>0,"Profile change must not reinterpret admitted event ownership");
        bool conflict=false;try{XMindIpcServer duplicate(*store,nullptr,token_two);}catch(const Conflict&){conflict=true;}check(conflict,"Concurrent public binding must not replace the existing owner");
    }
    refused(captured["Identity"],{});refused(bound_sessions["Open"],{text("captured-session")});refused(bound_runs["Open"],{text("captured-run")});refused(bound_events["Read"],{cursor});refused(stale_connections["List"],{});refused(stale["Open"],{text("persisted")});refused(root["Sessions"],{text(token_one)});X::Value replacement;
    {
        XMindIpcServer owner(*store,nullptr,token_two);check(owner.endpoint()==endpoint,"Replacement binding must preserve the original root and listener");refused(root["Sessions"],{text(token_one)});refused(stale["Open"],{text("persisted")});
        auto rebound_catalog=call(root["Connections"],{text(token_two)});auto rebound=call(rebound_catalog["Open"],{text("local"),X::Value(client,2)});check(call(rebound["Identity"],{})["server_id"].ToString()==server_identity,"Rebinding must retain persisted server identity");refused(captured["Identity"],{});
        replacement=call(root["Sessions"],{text(token_two)});check(call(replacement["Open"],{text("persisted")})["title"].ToString()=="Scoped native API","Replacement owner must retain actual persisted session data");
    }
    store->close();store.reset();refused(stale_connections["List"],{});refused(stale["Open"],{text("persisted")});refused(replacement["Open"],{text("persisted")});
    {
        PersistenceService other((folder/"other.sqlite").string(),std::vector<std::string>{argv[1],argv[2]});XMindIpcServer owner(other,nullptr,token_two);auto catalog=call(root["Connections"],{text(token_two)});call(catalog["List"],{});auto binding=call(catalog["Open"],{text("local"),X::Value(client,1)});check(call(binding["Identity"],{})["server_id"].ToString()!=server_identity,"Different databases require distinct server identities");other.create_session("captured-session","Other server").get();other.create_run("captured-run","captured-session").get();auto events=call(binding["Events"],{text("captured-session"),text("captured-run")});refused(events["Read"],{cursor});
    }
    check(std::filesystem::weakly_canonical(folder).parent_path()==std::filesystem::weakly_canonical(std::filesystem::temp_directory_path())&&folder.filename().string().starts_with("xmind-api-lifetime-"),"Refusing fixture cleanup outside its known temporary directory");std::filesystem::remove_all(folder);
    std::cout<<"Native process root registration passed concurrent initialization, unbound service refusal, one listener, exclusive public ownership, stale object/token refusal, rebinding without re-registration, persisted data and refusal after backend destruction. Same-process SDK lifecycle fixture; separate xmind.exe calls are covered independently."<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"; retained fixture "<<folder.string()<<std::endl;return 1;}}
