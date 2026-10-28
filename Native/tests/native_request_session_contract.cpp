#include "native_request_channel.hpp"
#include <windows.h>
#include <atomic>
#include <future>
#include <iostream>
#include <filesystem>
#include <fstream>

using agentflow::LocalProfileConnection;
using agentflow::NativeRequestChannel;
using Json=nlohmann::json;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

// Real SDK IPC with a synthetic native service. No database, model or user
// backend is involved. Protocol 2 also preserves the legacy snapshot contract.
struct Fixture {
    X::Value module;
    std::atomic<int> factories=0,requests=0,capabilities=0;
    X::Value WebRequests(std::string){++factories;return module;}
    std::int64_t ReplyProtocol(){++capabilities;return 2;}
    X::Value RequestSnapshot(X::Value input){
        const auto request=agentflow::sdk_values::from_value(input);const int count=++requests;
        if(request.at("target")=="/v1/fail")throw std::runtime_error("Synthetic admitted request failure");
        return agentflow::sdk_values::to_value(Host(),Json{{"status",200},{"headers",Json::object()},
            {"body",Json{{"requests",count},{"factories",factories.load()}}.dump()},{"streaming",false}});
    }
    BEGIN_PACKAGE(Fixture)
        APISET().AddFunc<1>("WebRequests",&Fixture::WebRequests);
        APISET().AddFunc<0>("ReplyProtocol",&Fixture::ReplyProtocol);
        APISET().AddFunc<1>("RequestSnapshot",&Fixture::RequestSnapshot);
    END_PACKAGE
};
static LocalProfileConnection connection(const std::string& endpoint,const std::string& secret="fixture-owner"){
    return LocalProfileConnection{"fixture","",1,GetCurrentProcessId(),false,
        agentflow::SecretBytes({reinterpret_cast<const std::uint8_t*>(secret.data()),secret.size()}),true,"thread",endpoint,{}};
}
static Json read(LocalProfileConnection& target){const auto value=NativeRequestChannel(target).Get("/v1/health",{});require(value->status==200,"SDK reply failed");return Json::parse(value->body);}
static void complete_ready_records(){
    namespace fs=std::filesystem;
    const auto parent=fs::canonical(fs::current_path());
    const auto name="request-session-ready-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
    const auto root=parent/name,workspace=root/"workspace",records=root/"records";
    fs::create_directories(workspace); // The native publisher creates its private records directory.
    const auto text=[](const fs::path& path){const auto value=path.u8string();return std::string(reinterpret_cast<const char*>(value.data()),value.size());};
    const auto payload=Json{{"payload",std::string(15000,'x')}}.dump();std::atomic<bool> done=false,valid=true;std::atomic<int> observed=0;std::string observation_failure;
    std::thread observer([&]{try{for(int i=0;i<32;i++){
        const auto file=records/(std::to_string(i)+".json");
        while(!fs::exists(file)){if(done.load()){if(!fs::exists(file)){observation_failure="Missing completed readiness record "+std::to_string(i);valid=false;return;}break;}std::this_thread::yield();}
        std::ifstream input;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        do{input.open(file,std::ios::binary);if(input.is_open())break;input.clear();std::this_thread::yield();}while(std::chrono::steady_clock::now()<deadline);
        const std::string value((std::istreambuf_iterator<char>(input)),{});
        if(value!=payload){observation_failure="Unreadable readiness record "+std::to_string(i)+" bytes="+std::to_string(value.size())+" open="+std::to_string(input.is_open());valid=false;return;}++observed;
    }}catch(const std::exception& error){observation_failure=error.what();valid=false;}});
    try{for(int i=0;i<32;i++)agentflow::publish_local_view_ready(text(records/(std::to_string(i)+".json")),payload,text(workspace));}
    catch(...){done=true;observer.join();throw;}
    done=true;observer.join();require(valid&&observed==32,observation_failure.empty()?"Readiness observations incomplete":observation_failure.c_str());
    bool refused=false;try{agentflow::publish_local_view_ready(text(records/"0.json"),"{}",text(workspace));}catch(...){refused=true;}
    require(refused,"Publishing readiness replaced an existing file");
    const auto actual=fs::canonical(root);require(actual.parent_path()==parent&&actual.filename()==name,"Fixture cleanup escaped its own directory");fs::remove_all(actual);
}
int main(){
    try{
        complete_ready_records();
        Fixture fixture;
        {
            X::Runtime server;fixture.module=server.RegisterPackage("request_session_fixture",fixture);
            const auto port=20000LL+GetCurrentProcessId()%30000;const auto endpoint="lrpc:"+std::to_string(port);
            server.RegisterRemoteObject("xmind",fixture.module);server.LrpcListen(port);
            {
                auto target=connection(endpoint);read(target);auto initial=target.request_session->session;
                read(target);require(fixture.factories==1&&fixture.capabilities==1,"Each request reimported the service");
                std::vector<std::future<Json>> concurrent;
                for(int i=0;i<8;i++)concurrent.push_back(std::async(std::launch::async,[&]{return read(target);}));
                for(auto& result:concurrent)result.get();
                require(fixture.requests==10&&fixture.factories==1,"Concurrent replies lost connection ownership");
                // Exercise generation mismatch without replacing any live user
                // owner. The subsequent probe must invalidate this cached root.
                {std::lock_guard lock(initial->mutex);initial->generation^=1;}
                read(target);require(fixture.factories==2&&target.request_session->session!=initial,"Peer generation did not invalidate the import");
                const std::string replacement="fixture-replacement-owner";
                target.auth=agentflow::SecretBytes({reinterpret_cast<const std::uint8_t*>(replacement.data()),replacement.size()});
                read(target);require(fixture.factories==3,"Credential rotation reused the previous owner service");
                const auto admitted=fixture.requests.load();target.process_id++;
                bool refused=false;try{read(target);}catch(...){refused=true;}
                require(refused&&fixture.requests==admitted&&!target.request_session->session,"A different PID admitted a request or retained the cache");
                target.process_id=GetCurrentProcessId();read(target);require(fixture.factories==4,"Valid connection was not freshly imported after identity failure");
                bool failed=false;try{NativeRequestChannel(target).Get("/v1/fail",{});}catch(...){failed=true;}
                require(failed&&fixture.requests==admitted+2,"An admitted failure was replayed");
                read(target);require(fixture.factories==4,"Request failure unnecessarily reimported the backend");
                auto separate=connection(endpoint);read(separate);require(fixture.factories==5,"Separate connections shared an owner cache");
                auto retained=target.request_session->session;auto moved=std::move(target);read(moved);
                require(moved.request_session->session==retained&&fixture.factories==5,"Moving the connection lost its imported service");
                auto direct=connection("");direct.bound_requests=[](auto&,auto& response){response.status=200;response.body="{}";};read(direct);
                require(!direct.request_session->session&&fixture.factories==5,"In-process request created an IPC client");
            }
            fixture.module=X::Value{};
        }
        std::cout<<"persistent SDK session: reuse, concurrent calls, generation/credential/PID invalidation, no replay, move and direct dispatch passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
