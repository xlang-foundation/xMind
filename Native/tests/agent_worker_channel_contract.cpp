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
void codec_contract(){
    auto key=new_agent_worker_channel_key();AgentWorkerFrameCodec backend(scope(),SecretBytes(key.view()),AgentWorkerSide::backend),worker(scope(),SecretBytes(key.view()),AgentWorkerSide::worker);
    const AgentWorkerMessage request{"journal.run",std::string("owned\0value",11)+"\xE2\x9C\x93"};const auto first=backend.encode(request);const auto actual=worker.decode(first);require(actual.operation==request.operation&&actual.payload==request.payload,"Owned payload bytes must survive authentication");
    const auto answer=backend.decode(worker.encode({"journal.result","actual-result"}));require(answer.payload=="actual-result","Duplex channel must have independent direction sequences");
    rejects<AgentWorkerProtocolError>([&]{worker.decode(first);});rejects<AgentWorkerProtocolError>([&]{worker.decode(backend.encode(request));});
    auto reject_frame=[&](auto mutate){AgentWorkerFrameCodec sender(scope(),SecretBytes(key.view()),AgentWorkerSide::backend),receiver(scope(),SecretBytes(key.view()),AgentWorkerSide::worker);const auto valid=sender.encode(request);rejects<AgentWorkerProtocolError>([&]{receiver.decode(mutate(valid));});rejects<AgentWorkerProtocolError>([&]{receiver.decode(valid);});};
    for(const auto* field:{"payload","operation","direction","mac"})reject_frame([&](const std::string& value){auto frame=Json::parse(value);frame[field]="tampered";return frame.dump();});
    reject_frame([](const std::string& value){auto frame=Json::parse(value);frame["version"]=2;return frame.dump();});
    reject_frame([](const std::string& value){auto frame=Json::parse(value);frame["unexpected"]=true;return frame.dump();});
    reject_frame([](const std::string& value){return std::string("{\"version\":1,")+value.substr(1);});
    reject_frame([](const std::string&){return std::string(AgentWorkerFrameCodec::max_frame_bytes+1,'x');});
    reject_frame([](const std::string&){return std::string("{\"nested\":")+std::string(30,'[')+"0"+std::string(30,']')+"}";});
    for(unsigned changed=0;changed<5;++changed){auto foreign=scope();if(changed==0)foreign.root_id="foreign-root";if(changed==1)foreign.run_id="foreign-run";if(changed==2)foreign.workspace_id="foreign-workspace";if(changed==3)foreign.generation="stale-generation";if(changed==4)++foreign.workspace_revision;
        AgentWorkerFrameCodec sender(foreign,SecretBytes(key.view()),AgentWorkerSide::backend),receiver(scope(),SecretBytes(key.view()),AgentWorkerSide::worker);rejects<AgentWorkerProtocolError>([&]{receiver.decode(sender.encode(request));});}
    AgentWorkerFrameCodec wrong_key(scope(),new_agent_worker_channel_key(),AgentWorkerSide::worker);rejects<AgentWorkerProtocolError>([&]{wrong_key.decode(first);});
    AgentWorkerFrameCodec reflected(scope(),SecretBytes(key.view()),AgentWorkerSide::backend);rejects<AgentWorkerProtocolError>([&]{reflected.decode(first);});
    AgentWorkerFrameCodec sender(scope(),SecretBytes(key.view()),AgentWorkerSide::backend),reordered(scope(),SecretBytes(key.view()),AgentWorkerSide::worker);sender.encode(request);rejects<AgentWorkerProtocolError>([&]{reordered.decode(sender.encode(request));});
    AgentWorkerFrameCodec bounded(scope(),SecretBytes(key.view()),AgentWorkerSide::backend),bounded_receiver(scope(),SecretBytes(key.view()),AgentWorkerSide::worker);rejects<AgentWorkerProtocolError>([&]{bounded.encode({"invalid operation","x"});});rejects<AgentWorkerProtocolError>([&]{bounded.encode({"journal.run",std::string(AgentWorkerFrameCodec::max_frame_bytes,'x')});});require(bounded_receiver.decode(bounded.encode(request)).payload==request.payload,"Rejected local encodes must not consume a sequence");
    auto invalid=scope();invalid.workspace_revision=0;rejects<AgentWorkerProtocolError>([&]{AgentWorkerFrameCodec bad(invalid,SecretBytes(key.view()),AgentWorkerSide::backend);});std::array<std::uint8_t,1> short_key{};rejects<std::invalid_argument>([&]{AgentWorkerFrameCodec bad(scope(),SecretBytes(short_key),AgentWorkerSide::backend);});
}
int child(int argc,char** argv){
    require(argc==4&&std::string(argv[2])=="--channel-handle","Private child arguments must contain only an inherited handle");std::uintptr_t handle=0;const std::string text=argv[3];const auto parsed=std::from_chars(text.data(),text.data()+text.size(),handle);require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size(),"Invalid fixture channel handle");
    auto channel=AgentWorkerChannel::accept_inherited(handle,Clock::now()+10s);require(channel->scope()==scope(),"Child must receive its captured scope");
    for(;;){const auto request=channel->receive(Clock::now()+20s);
        if(request.operation=="test.echo")channel->send(request,Clock::now()+5s);
        else if(request.operation=="test.environment"){
            std::array<wchar_t,2> value{};Json result={{"pid",GetCurrentProcessId()},{"private_absent",GetEnvironmentVariableW(L"XMIND_CHANNEL_TEST_PRIVATE",value.data(),static_cast<DWORD>(value.size()))==0},{"auth_absent",GetEnvironmentVariableW(L"XMIND_AUTH_TOKEN",value.data(),static_cast<DWORD>(value.size()))==0},{"path_absent",GetEnvironmentVariableW(L"PATH",value.data(),static_cast<DWORD>(value.size()))==0}};channel->send({request.operation,result.dump()},Clock::now()+5s);
        }else if(request.operation=="test.exit"){channel->send({"test.exiting","actual-normal-exit"},Clock::now()+5s);return 0;}
        else if(request.operation=="test.crash")TerminateProcess(GetCurrentProcess(),91);
        else if(request.operation=="test.hold"){channel->send({"test.holding","actual-child-held"},Clock::now()+5s);Sleep(INFINITE);}
        else if(request.operation=="test.descendant"){
            const auto file=std::filesystem::u8path(executable()).native();auto command=L"\""+file+L"\" channel-fixture-hold";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION created{};require(CreateProcessW(file.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&created),"Cannot create actual owned descendant");CloseHandle(created.hThread);CloseHandle(created.hProcess);channel->send({"test.descendant",Json{{"pid",created.dwProcessId}}.dump()},Clock::now()+5s);Sleep(INFINITE);
        }else throw std::runtime_error("Unexpected fixture operation");
    }
}
void process_contract(){
    auto bound=ForegroundProcess::bind_executable(executable());require(!bound->identity().empty()&&bound->identity()==ForegroundProcess::executable_identity(executable()),"Worker must use its exact retained native image");
    SetEnvironmentVariableW(L"XMIND_CHANNEL_TEST_PRIVATE",L"synthetic-private-parent-value");
    auto launch=[&]{return AgentWorkerProcess::launch(bound,scope(),Clock::now()+10s);};
    std::cout<<"SDK IPC phase: initial launch"<<std::endl;auto worker=launch();std::cout<<"SDK IPC phase: environment"<<std::endl;require(worker->pid()!=GetCurrentProcessId()&&worker->creation_time()!=0&&worker->executable_identity()==bound->identity()&&worker->alive(),"Ready identity must name the actual distinct child");
    worker->channel().send({"test.environment",""},Clock::now()+5s);const auto metadata=Json::parse(worker->channel().receive(Clock::now()+5s).payload);require(metadata.at("pid")==worker->pid()&&metadata.at("private_absent")==true&&metadata.at("auth_absent")==true&&metadata.at("path_absent")==true,"Private worker must not inherit ambient keys or shell environment");
    std::cout<<"SDK IPC phase: payloads"<<std::endl;const std::string data(256*1024,'x');for(unsigned i=0;i<3;++i){worker->channel().send({"test.echo",data+std::to_string(i)},Clock::now()+5s);require(worker->channel().receive(Clock::now()+5s).payload==data+std::to_string(i),"Actual xlang3 remote object must retain bounded payloads and sequences");}
    worker->channel().send({"test.exit",""},Clock::now()+5s);require(worker->channel().receive(Clock::now()+5s).operation=="test.exiting"&&worker->wait(Clock::now()+5s)==0&&!worker->alive(),"Actual normal child exit must be observed");worker.reset();
    std::cout<<"SDK IPC phase: multiple workers"<<std::endl;{auto first=launch(),second=launch();require(first->pid()!=second->pid(),"Concurrent workers must have distinct actual identities");first->channel().send({"test.echo","first isolated remote object"},Clock::now()+5s);second->channel().send({"test.echo","second isolated remote object"},Clock::now()+5s);require(first->channel().receive(Clock::now()+5s).payload=="first isolated remote object"&&second->channel().receive(Clock::now()+5s).payload=="second isolated remote object","Multiple worker remote objects must retain their separate routes");for(auto* child:{first.get(),second.get()}){child->channel().send({"test.exit",""},Clock::now()+5s);child->channel().receive(Clock::now()+5s);require(child->wait(Clock::now()+5s)==0,"Each remote worker must drain independently");}}
    std::cout<<"SDK IPC phase: crash"<<std::endl;worker=launch();try{worker->channel().send({"test.crash",""},Clock::now()+5s);}catch(const AgentWorkerProtocolError&){}rejects<AgentWorkerProtocolError>([&]{worker->channel().receive(Clock::now()+5s);});require(worker->wait(Clock::now()+5s)==91,"Crash must retain its actual exit without restart");worker.reset();
    std::cout<<"SDK IPC phase: hold 1"<<std::endl;worker=launch();worker->channel().send({"test.hold",""},Clock::now()+5s);require(worker->channel().receive(Clock::now()+5s).operation=="test.holding","Held child must acknowledge actual dispatch");rejects<AgentWorkerChannelTimeout>([&]{worker->wait(Clock::now()+30ms);});require(worker->alive(),"Observation timeout must not retire or replace a live child");rejects<AgentWorkerChannelTimeout>([&]{worker->channel().receive(Clock::now()+30ms);});require(worker->alive()&&worker->channel().statistics().cancelled_reads==1,"Read timeout must cancel its IPC rendezvous while retaining the owned process");rejects<AgentWorkerProtocolError>([&]{worker->channel().send({"test.echo","no-reuse"},Clock::now()+5s);});worker->terminate();require(!worker->alive(),"Explicit retirement must observe the owned child terminal");worker.reset();
    std::cout<<"SDK IPC phase: hold 2"<<std::endl;worker=launch();worker->channel().send({"test.hold",""},Clock::now()+5s);worker->channel().receive(Clock::now()+5s);std::stop_source cancel;std::jthread timer([&]{Sleep(100);cancel.request_stop();});rejects<AgentWorkerChannelCancelled>([&]{worker->channel().send({"test.echo",std::string(1024*1024,'b')},Clock::now()+5s,cancel.get_token());});require(worker->alive()&&worker->channel().statistics().cancelled_writes==1,"Blocked remote delivery cancellation must retain the live owned worker");worker->terminate();worker.reset();
    std::cout<<"SDK IPC phase: descendant"<<std::endl;worker=launch();worker->channel().send({"test.descendant",""},Clock::now()+5s);const auto descendant=Json::parse(worker->channel().receive(Clock::now()+5s).payload).at("pid").get<DWORD>();HANDLE observed=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,descendant);require(observed&&WaitForSingleObject(observed,0)==WAIT_TIMEOUT,"Fixture must create an actual live descendant");worker->terminate();require(!worker->alive()&&WaitForSingleObject(observed,1000)==WAIT_OBJECT_0,"Owned job retirement must include its actual descendant");CloseHandle(observed);worker.reset();
    std::cout<<"SDK IPC phase: hold 3"<<std::endl;worker=launch();worker->channel().send({"test.hold",""},Clock::now()+5s);worker->channel().receive(Clock::now()+5s);HANDLE retained=OpenProcess(SYNCHRONIZE,FALSE,worker->pid());require(retained!=nullptr,"Fixture must retain its exact worker handle");worker.reset();require(WaitForSingleObject(retained,1000)==WAIT_OBJECT_0,"Owner destruction must retire only its created worker");CloseHandle(retained);
    std::stop_source cancelled;cancelled.request_stop();rejects<AgentWorkerChannelCancelled>([&]{AgentWorkerProcess::launch(bound,scope(),Clock::now()+5s,cancelled.get_token());});SetEnvironmentVariableW(L"XMIND_CHANNEL_TEST_PRIVATE",nullptr);
}
}
int main(int argc,char** argv){try{if(argc>1&&std::string(argv[1])=="agent-channel-worker")return child(argc,argv);if(argc==2&&std::string(argv[1])=="channel-fixture-hold"){Sleep(INFINITE);return 1;}require(argc==1,"Unexpected fixture arguments");codec_contract();process_contract();std::cout<<"Authenticated native channel, captured scope, distinct PID, cancellation and owned tree retirement passed; AgentRunner placement is not implemented.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
