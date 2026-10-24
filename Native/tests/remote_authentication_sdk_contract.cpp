#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/connection_profiles.hpp"
#include "agentflow/remote_connection_credentials.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "../src/agent_worker_sdk_values.hpp"
#include "xlang3/xlang3.h"
#include <filesystem>
#include <algorithm>
#include <iostream>
using namespace agentflow;
namespace {
constexpr const char* owner_token="synthetic-remote-sdk-owner-not-live";
constexpr const char* credential="synthetic-remote-sdk-credential-not-live";
void check(bool value){if(!value)throw std::runtime_error("Remote authentication SDK fixture invariant failed");}
int client(const char* endpoint,bool positive){
    X::Runtime runtime;const auto root=runtime.ImportRemote("xmind",endpoint);const auto text=[&](const std::string& value){return X::Value(runtime,value);};
    const auto call=[&](const X::Value& method,std::vector<X::Value> args){X::Value result;check(method.Call(args,result));return result;};
    const auto refused=[&](const X::Value& method,std::vector<X::Value> args){X::Value result;check(!method.Call(args,result));};
    refused(root["Connections"],{text("wrong-synthetic-owner-token")});const auto connections=call(root["Connections"],{text(owner_token)});check(call(connections["Capabilities"],{})["remote_authentication"].ToInt64()==1);
    const auto request=[&](const char* secret){return sdk_values::to_value(runtime.host(),nlohmann::json{{"credential",secret},{"expected_credential_revision",0},{"timeout_ms",5000}});};
    check(call(connections["Capabilities"],{})["remote_authentication_status"].ToInt64()==1);const auto absent=call(connections["Authentication"],{text("remote-sdk"),X::Value(1)});check(absent.Size()==8&&absent["state"].ToString()=="absent"&&absent["credential_revision"].ToInt64()==0&&absent["server_id"].IsNone());
    if(positive)refused(connections["Authenticate"],{text("remote-sdk"),X::Value(1),request("wrong-synthetic-remote-credential")});
    if(positive){const auto receipt=call(connections["Authenticate"],{text("remote-sdk"),X::Value(1),request(credential)});check(receipt.Size()==7&&receipt["server_id"].ToString()=="remote-sdk-server"&&receipt["connection_id"].ToString()=="remote-sdk"&&receipt["credential_revision"].ToInt64()==1&&!receipt["command_routing"].ToInt64());refused(connections["Authenticate"],{text("remote-sdk"),X::Value(1),request(credential)});}
    else {refused(connections["Authenticate"],{text("remote-sdk"),X::Value(1),request(credential)});const bool transport_refusal=runtime.LastError().find("remote_unavailable")!=std::string::npos;std::cout<<"sdk-transport-refusal:"<<(transport_refusal?"yes":"no")<<std::endl;check(transport_refusal);}
    const auto observed=call(connections["Authentication"],{text("remote-sdk"),X::Value(1)});check(observed["state"].ToString()==(positive?"stored":"absent")&&observed["credential_revision"].ToInt64()==(positive?1:0)&&!observed["command_routing"].ToInt64());refused(connections["Authentication"],{text("remote-sdk"),X::Value(2)});
    const auto catalog=call(connections["List"],{});check(catalog["active_id"].ToString()=="local");refused(connections["Open"],{text("remote-sdk"),X::Value(1)});std::cout<<(positive?"separate-sdk-auth-positive":"separate-sdk-auth-refused")<<std::endl;return 0;
}
void verify(PersistenceService& store,bool positive){
    ConnectionProfiles profiles(store);const auto catalog=profiles.load();check(catalog.active_id=="local");if(!positive){check(store.credentials("server").get().empty());return;}
    const auto profile=std::find_if(catalog.profiles.begin(),catalog.profiles.end(),[](const auto& value){return value.id=="remote-sdk";});check(profile!=catalog.profiles.end());const auto saved=RemoteConnectionCredentials(store,profiles).load(*profile);const auto bytes=saved.credential.view();check(saved.revision==1&&saved.grant.server_id=="remote-sdk-server"&&saved.grant.authorized_scopes.size()==1&&std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())==credential);
}
}
int main(int argc,char** argv){try{
    if(argc==4&&std::string(argv[1])=="client")return client(argv[2],std::string(argv[3])=="positive");if(argc!=5)return 2;const auto database=(std::filesystem::path(argv[1])/"remote-sdk.sqlite").string();check(!std::filesystem::exists(database));bool positive=false;
    {PersistenceService store(database,{argv[2],argv[3]});ConnectionProfiles profiles(store);profiles.load_or_initialize();profiles.apply(nlohmann::json{{"profiles",nlohmann::json::array({{{"id","local"},{"name","Local"},{"kind","local"}},{{"id","remote-sdk"},{"name","Controlled remote SDK"},{"kind","nexus"},{"origin",argv[4]}}})}}.dump(),1);
        {XMindIpcServer server(store,nullptr,owner_token);std::cout<<nlohmann::json{{"endpoint",server.endpoint()}}.dump()<<std::endl;std::string instruction;check(bool(std::getline(std::cin,instruction)));check(instruction=="verify-positive"||instruction=="verify-refusal");positive=instruction=="verify-positive";verify(store,positive);}
        store.close();}
    {PersistenceService store(database,{argv[2],argv[3]});verify(store,positive);store.close();}
    std::cout<<"remote-sdk-database-reopened"<<std::endl;return 0;
}catch(const std::exception&){std::cerr<<"Native remote SDK fixture failed; no private material reflected\n";return 1;}}
