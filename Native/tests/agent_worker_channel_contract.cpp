#include "fixture_worker_channel.hpp"
#include "agentflow/agent_worker_channel.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <array>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

using namespace agentflow;
using Json=nlohmann::json;
using namespace std::chrono_literals;
namespace {
using Clock=std::chrono::steady_clock;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class Error,class Function> void rejects(Function action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected channel rejection did not occur");}
AgentWorkerScope scope(){return {"root-fixture","run-fixture","workspace-fixture","generation-fixture",7};}
std::string executable(){std::wstring value(32768,L'\0');const auto size=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));require(size&&size<value.size(),"Fixture image path unavailable");value.resize(size);auto path=std::filesystem::path(value).u8string();return {reinterpret_cast<const char*>(path.data()),path.size()};}
int child(int argc,char** argv){
    if(std::filesystem::path(executable()).stem()=="startup-no-ready"){Sleep(INFINITE);return 1;}
    require(argc==4&&std::string(argv[2])=="--channel-handle","Private child arguments must contain only an inherited handle");std::uintptr_t handle=0;const std::string text=argv[3];const auto parsed=std::from_chars(text.data(),text.data()+text.size(),handle);require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size(),"Invalid fixture channel handle");
    auto channel=AgentWorkerChannel::accept_inherited(handle,Clock::now()+10s);require(channel->scope()==scope(),"Child must receive its captured scope");
    for(;;){const auto request=fixture_worker_channel(*channel).receive(Clock::now()+20s);
        if(request.operation=="test.echo")fixture_worker_channel(*channel).send(request,Clock::now()+5s);
        else if(request.operation=="test.environment"){
            std::array<wchar_t,2> value{};Json result={{"pid",GetCurrentProcessId()},{"private_absent",GetEnvironmentVariableW(L"XMIND_CHANNEL_TEST_PRIVATE",value.data(),static_cast<DWORD>(value.size()))==0},{"auth_absent",GetEnvironmentVariableW(L"XMIND_AUTH_TOKEN",value.data(),static_cast<DWORD>(value.size()))==0},{"path_absent",GetEnvironmentVariableW(L"PATH",value.data(),static_cast<DWORD>(value.size()))==0}};fixture_worker_channel(*channel).send({request.operation,result.dump()},Clock::now()+5s);
        }else if(request.operation=="test.exit"){fixture_worker_channel(*channel).send({"test.exiting","actual-normal-exit"},Clock::now()+5s);return 0;}
        else if(request.operation=="test.crash")TerminateProcess(GetCurrentProcess(),91);
        else if(request.operation=="test.hold"){fixture_worker_channel(*channel).send({"test.holding","actual-child-held"},Clock::now()+5s);Sleep(INFINITE);}
        else if(request.operation=="test.descendant"){
            const auto file=std::filesystem::u8path(executable()).native();auto command=L"\""+file+L"\" channel-fixture-hold";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION created{};require(CreateProcessW(file.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&created),"Cannot create actual owned descendant");CloseHandle(created.hThread);CloseHandle(created.hProcess);fixture_worker_channel(*channel).send({"test.descendant",Json{{"pid",created.dwProcessId}}.dump()},Clock::now()+5s);Sleep(INFINITE);
        }else throw std::runtime_error("Unexpected fixture operation");
    }
}
void process_contract(const std::string& image){
    auto bound=ForegroundProcess::bind_executable(image);require(!bound->identity().empty()&&bound->identity()==ForegroundProcess::executable_identity(image),"Worker must use its exact retained native image");
    SetEnvironmentVariableW(L"XMIND_CHANNEL_TEST_PRIVATE",L"synthetic-private-parent-value");
    auto launch=[&]{return AgentWorkerProcess::launch(bound,scope(),Clock::now()+10s);};
    std::cout<<"SDK IPC phase: initial launch"<<std::endl;auto worker=launch();std::cout<<"SDK IPC phase: environment"<<std::endl;require(worker->pid()!=GetCurrentProcessId()&&worker->creation_time()!=0&&worker->executable_identity()==bound->identity()&&worker->alive(),"Ready identity must name the actual distinct child");
    fixture_worker_channel(worker->channel()).send({"test.environment",""},Clock::now()+5s);const auto metadata=Json::parse(fixture_worker_channel(worker->channel()).receive(Clock::now()+5s).payload);require(metadata.at("pid")==worker->pid()&&metadata.at("private_absent")==true&&metadata.at("auth_absent")==true&&metadata.at("path_absent")==true,"Private worker must not inherit ambient keys or shell environment");
    std::cout<<"SDK IPC phase: payloads"<<std::endl;const std::string data=std::string(256*1024,'x')+std::string("\0",1)+"\xE2\x9C\x93";for(unsigned i=0;i<3;++i){fixture_worker_channel(worker->channel()).send({"test.echo",data+std::to_string(i)},Clock::now()+5s);require(fixture_worker_channel(worker->channel()).receive(Clock::now()+5s).payload==data+std::to_string(i),"Actual xlang3 remote object must retain bounded SDK payloads");}
    fixture_worker_channel(worker->channel()).send({"test.exit",""},Clock::now()+5s);require(fixture_worker_channel(worker->channel()).receive(Clock::now()+5s).operation=="test.exiting"&&worker->wait(Clock::now()+5s)==0&&!worker->alive(),"Actual normal child exit must be observed");worker.reset();
    std::cout<<"SDK IPC phase: multiple workers"<<std::endl;{auto first=launch(),second=launch();require(first->pid()!=second->pid(),"Concurrent workers must have distinct actual identities");fixture_worker_channel(first->channel()).send({"test.echo","first isolated remote object"},Clock::now()+5s);fixture_worker_channel(second->channel()).send({"test.echo","second isolated remote object"},Clock::now()+5s);require(fixture_worker_channel(first->channel()).receive(Clock::now()+5s).payload=="first isolated remote object"&&fixture_worker_channel(second->channel()).receive(Clock::now()+5s).payload=="second isolated remote object","Multiple worker remote objects must retain their separate routes");for(auto* child:{first.get(),second.get()}){fixture_worker_channel(child->channel()).send({"test.exit",""},Clock::now()+5s);fixture_worker_channel(child->channel()).receive(Clock::now()+5s);require(child->wait(Clock::now()+5s)==0,"Each remote worker must drain independently");}}
    std::cout<<"SDK IPC phase: crash"<<std::endl;worker=launch();try{fixture_worker_channel(worker->channel()).send({"test.crash",""},Clock::now()+5s);}catch(const AgentWorkerProtocolError&){}rejects<AgentWorkerProtocolError>([&]{fixture_worker_channel(worker->channel()).receive(Clock::now()+5s);});require(worker->wait(Clock::now()+5s)==91,"Crash must retain its actual exit without restart");worker.reset();
    std::cout<<"SDK IPC phase: hold 1"<<std::endl;worker=launch();fixture_worker_channel(worker->channel()).send({"test.hold",""},Clock::now()+5s);require(fixture_worker_channel(worker->channel()).receive(Clock::now()+5s).operation=="test.holding","Held child must acknowledge actual dispatch");rejects<AgentWorkerChannelTimeout>([&]{worker->wait(Clock::now()+30ms);});require(worker->alive(),"Observation timeout must not retire or replace a live child");rejects<AgentWorkerChannelTimeout>([&]{fixture_worker_channel(worker->channel()).receive(Clock::now()+30ms);});require(worker->alive()&&fixture_worker_channel(worker->channel()).statistics().cancelled_reads==1,"Read timeout must cancel its IPC rendezvous while retaining the owned process");rejects<AgentWorkerProtocolError>([&]{fixture_worker_channel(worker->channel()).send({"test.echo","no-reuse"},Clock::now()+5s);});worker->terminate();require(!worker->alive(),"Explicit retirement must observe the owned child terminal");worker.reset();
    std::cout<<"SDK IPC phase: hold 2"<<std::endl;worker=launch();fixture_worker_channel(worker->channel()).send({"test.hold",""},Clock::now()+5s);fixture_worker_channel(worker->channel()).receive(Clock::now()+5s);std::stop_source cancel;std::jthread timer([&]{Sleep(100);cancel.request_stop();});rejects<AgentWorkerChannelCancelled>([&]{fixture_worker_channel(worker->channel()).send({"test.echo",std::string(1024*1024,'b')},Clock::now()+5s,cancel.get_token());});require(worker->alive()&&fixture_worker_channel(worker->channel()).statistics().cancelled_writes==1,"Blocked remote delivery cancellation must retain the live owned worker");worker->terminate();worker.reset();
    std::cout<<"SDK IPC phase: descendant"<<std::endl;worker=launch();fixture_worker_channel(worker->channel()).send({"test.descendant",""},Clock::now()+5s);const auto descendant=Json::parse(fixture_worker_channel(worker->channel()).receive(Clock::now()+5s).payload).at("pid").get<DWORD>();HANDLE observed=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,descendant);require(observed&&WaitForSingleObject(observed,0)==WAIT_TIMEOUT,"Fixture must create an actual live descendant");worker->terminate();require(!worker->alive()&&WaitForSingleObject(observed,1000)==WAIT_OBJECT_0,"Owned job retirement must include its actual descendant");CloseHandle(observed);worker.reset();
    std::cout<<"SDK IPC phase: hold 3"<<std::endl;worker=launch();fixture_worker_channel(worker->channel()).send({"test.hold",""},Clock::now()+5s);fixture_worker_channel(worker->channel()).receive(Clock::now()+5s);HANDLE retained=OpenProcess(SYNCHRONIZE,FALSE,worker->pid());require(retained!=nullptr,"Fixture must retain its exact worker handle");worker.reset();require(WaitForSingleObject(retained,1000)==WAIT_OBJECT_0,"Owner destruction must retire only its created worker");CloseHandle(retained);
    for(bool oversized:{false,true}){auto bounded=launch();rejects<AgentWorkerProtocolError>([&]{fixture_worker_channel(bounded->channel()).send({oversized?"test.echo":"invalid operation",oversized?std::string(AgentWorkerChannel::max_message_bytes,'x'):""},Clock::now()+5s);});require(bounded->alive(),"Rejected local message must not fabricate process exit");bounded->terminate();}
    std::stop_source cancelled;cancelled.request_stop();rejects<AgentWorkerChannelCancelled>([&]{AgentWorkerProcess::launch(bound,scope(),Clock::now()+5s,cancelled.get_token());});SetEnvironmentVariableW(L"XMIND_CHANNEL_TEST_PRIVATE",nullptr);
}
void failed_startup_contract(){
    const auto source=std::filesystem::u8path(executable());
    const auto root=std::filesystem::temp_directory_path()/(L"xmind-channel-startup-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    require(std::filesystem::create_directory(root),"Fresh failed-startup fixture required");
    try{
        const auto image=root/L"startup-no-ready.exe";
        std::filesystem::copy_file(source,image);
        std::filesystem::copy_file(source.parent_path()/L"xlang3_runtime.dll",root/L"xlang3_runtime.dll");
        const auto text=image.u8string();auto worker=AgentWorkerProcess::prepare(ForegroundProcess::bind_executable({reinterpret_cast<const char*>(text.data()),text.size()}),scope());
        require(worker->pid()==0&&!worker->alive(),"Preparation must create no process");
        rejects<AgentWorkerChannelTimeout>([&]{worker->start(Clock::now()+500ms);});
        require(worker->pid()!=0&&worker->pid()!=GetCurrentProcessId()&&worker->creation_time()!=0&&worker->alive(),"Readiness failure must retain its exact live child");
        rejects<AgentWorkerChannelTimeout>([&]{worker->wait(Clock::now()+1ms);});
        require(worker->alive(),"Observation timeout cannot infer physical exit");
        worker->terminate();require(!worker->alive()&&worker->wait(Clock::now()+1s)==1,"Explicit startup retirement must confirm the same owned job exited");
        rejects<AgentWorkerProtocolError>([&]{worker->start(Clock::now()+1s);});
        worker.reset();std::filesystem::remove_all(root);
        std::cout<<"Failed readiness retained exact child; observation timeout preserved it; explicit job retirement confirmed exit without restart."<<std::endl;
    }catch(...){std::cerr<<"Retained startup fixture: "<<root<<'\n';throw;}
}
void long_image_contract(){
    const auto source=std::filesystem::u8path(executable());
    const auto root=std::filesystem::temp_directory_path()/(L"xmind-channel-path-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    require(std::filesystem::create_directory(root),"Fresh channel fixture directory required");
    try{
        require(root.native().size()<237,"Channel fixture temporary root is too long");
        const auto directory=root/std::wstring(237-root.native().size(),L'p');
        std::filesystem::create_directory(directory);
        const auto image=directory/L"xmind.exe";
        require(image.native().size()==248,"Channel loader regression must use a 248-character image");
        std::filesystem::copy_file(source,image);
        std::filesystem::copy_file(source.parent_path()/L"xlang3_runtime.dll",directory/L"xlang3_runtime.dll");
        const auto text=image.u8string();process_contract({reinterpret_cast<const char*>(text.data()),text.size()});
        std::filesystem::remove_all(root);
    }catch(...){std::cerr<<"Retained long-image channel fixture: "<<root<<'\n';throw;}
}
}
int main(int argc,char** argv){try{initialize_fixture_worker_channel();if(argc>1&&std::string(argv[1])=="agent-channel-worker")return child(argc,argv);if(argc==2&&std::string(argv[1])=="channel-fixture-hold"){Sleep(INFINITE);return 1;}require(argc==1,"Unexpected fixture arguments");process_contract(executable());failed_startup_contract();long_image_contract();std::cout<<"Native SDK worker links, captured scope, distinct PID, cancellation and owned tree retirement passed at short and long image paths.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
