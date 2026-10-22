#include "agentflow/agent_worker_native_execution.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "nlohmann/json.hpp"
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>
#define NOMINMAX
#include <windows.h>
using namespace agentflow;
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action> void refuses(Action action){bool refused=false;try{action();}catch(const AgentWorkerProtocolError&){refused=true;}check(refused,"Native execution must refuse mismatched/repeated access");}
X::Value call(const X::Value& method,std::vector<X::Value> args={}){X::Value out;check(method.Call(args,out),"Native execution SDK call failed");return out;}
X::Value open(X::Runtime& runtime,const NativeExecutionAccess& access){auto root=runtime.ImportRemote("xmind",access.endpoint.c_str());return call(call(root["Executions"])["Open"],{X::Value(runtime,access.ticket)});}
std::string image(){wchar_t path[32768]{};const auto size=GetModuleFileNameW(nullptr,path,32768);check(size>0&&size<32768,"Fixture executable identity unavailable");return std::filesystem::path(path).string();}
int child(char** argv){const auto child_started=Clock::now();auto channel=AgentWorkerChannel::accept_inherited(std::stoull(argv[3]),Clock::now()+5s);const auto boot=channel->receive(Clock::now()+5s);check(boot.operation=="execution.fixture","Native execution fixture bootstrap differs");const bool cancel=boot.payload=="cancel";std::atomic<unsigned> calls=0;{
    const auto registration_started=Clock::now();NativeExecutionBinding owner(channel->scope(),[&](std::stop_token stop){check(++calls==1,"Native execution dispatched twice");if(cancel){while(!stop.stop_requested())std::this_thread::sleep_for(1ms);return RunState::cancelled;}return RunState::completed;});const auto registration_finished=Clock::now();const auto access=owner.access();channel->send({"execution.access",nlohmann::json{{"endpoint",access.endpoint},{"ticket",access.ticket},{"pid",access.pid},{"child_pre_registration_ms",std::chrono::duration<double,std::milli>(registration_started-child_started).count()},{"root_registration_ms",std::chrono::duration<double,std::milli>(registration_finished-registration_started).count()}}.dump()},Clock::now()+5s);owner.wait_closed(Clock::now()+10s);
    }check(calls==1,"Native execution child must finish exactly one callback");return 0;
}
void process(bool cancel){const auto started=Clock::now();const AgentWorkerScope scope{"execution-root","execution-owner","workspace:execution",cancel?"execution-cancel":"execution-complete",7};auto worker=AgentWorkerProcess::launch(ForegroundProcess::bind_executable(image()),scope,Clock::now()+5s);const auto launched=Clock::now();worker->channel().send({"execution.fixture",cancel?"cancel":"complete"},Clock::now()+5s);const auto ready=worker->channel().receive(Clock::now()+5s);check(ready.operation=="execution.access","Missing native execution access");const auto ready_at=Clock::now();const auto value=nlohmann::json::parse(ready.payload);NativeExecutionAccess access{value.at("endpoint").get<std::string>(),value.at("ticket").get<std::string>(),value.at("pid").get<std::uint32_t>()};check(access.pid==worker->pid()&&worker->alive(),"Execution endpoint must be the exact owned child");
    auto wrong=access;++wrong.pid;refuses([&]{NativeExecutionClient invalid(wrong,scope);});auto foreign=scope;foreign.run_id="foreign-owner";refuses([&]{NativeExecutionClient invalid(access,foreign);});wrong=access;wrong.ticket[0]=wrong.ticket[0]=='a'?'b':'a';refuses([&]{NativeExecutionClient invalid(wrong,scope);});const auto refused_at=Clock::now();NativeExecutionClient client(access,scope);const auto bound_at=Clock::now();auto running=std::async(std::launch::async,[&]{return client.execute(Clock::now()+5s);});
    if(cancel){X::Runtime runtime;auto remote=open(runtime,access);const auto status=remote["Status"];const auto until=Clock::now()+2s;bool started=false;while(Clock::now()<until){started=call(status)["started"].ToInt64()!=0;if(started)break;std::this_thread::sleep_for(1ms);}check(started&&running.wait_for(0ms)!=std::future_status::ready,"Cancel must follow actual live native execution");client.cancel();}
    check(running.get()==(cancel?RunState::cancelled:RunState::completed),"Native execution returned wrong terminal intent");const auto executed_at=Clock::now();check(worker->alive(),"Terminal intent alone must not retire the child");
    X::Runtime timing_runtime;auto timing_remote=open(timing_runtime,access);const auto timing_status=timing_remote["Status"];std::vector<double> call_ms;for(unsigned i=0;i<100;++i){const auto before=Clock::now();call(timing_status);call_ms.push_back(std::chrono::duration<double,std::milli>(Clock::now()-before).count());}std::sort(call_ms.begin(),call_ms.end());
    refuses([&]{client.execute(Clock::now()+1s);});NativeExecutionClient duplicate(access,scope);refuses([&]{duplicate.execute(Clock::now()+1s);});const auto closing_at=Clock::now();client.close();check(worker->wait(Clock::now()+5s)==0&&!worker->alive(),"Native close must drain the exact child");worker->channel().close();const auto finished=Clock::now();auto ms=[](auto end,auto begin){return std::chrono::duration<double,std::milli>(end-begin).count();};std::cout<<nlohmann::json{{"mode",cancel?"cancel":"complete"},{"total_ms",ms(finished,started)},{"launch_ms",ms(launched,started)},{"publish_ms",ms(ready_at,launched)},{"negative_identity_probes_ms",ms(refused_at,ready_at)},{"client_bind_ms",ms(bound_at,refused_at)},{"execute_ms",ms(executed_at,bound_at)},{"status_and_duplicate_probes_ms",ms(closing_at,executed_at)},{"close_and_exit_ms",ms(finished,closing_at)},{"child_pre_registration_ms",value.at("child_pre_registration_ms")},{"root_registration_ms",value.at("root_registration_ms")},{"status_call_ms_p50",call_ms[50]},{"status_call_ms_p95",call_ms[95]},{"status_call_ms_max",call_ms.back()}}.dump()<<std::endl;
}
void lifetime(){const AgentWorkerScope scope{"lifetime-root","lifetime-owner","workspace:execution","execution-lifetime",1};X::Runtime runtime;X::Value retained;NativeExecutionAccess access;std::atomic<unsigned> finished=0;{
    NativeExecutionBinding owner(scope,[&](std::stop_token stop){while(!stop.stop_requested())std::this_thread::sleep_for(1ms);++finished;return RunState::cancelled;});access=owner.access();retained=open(runtime,access);call(retained["Execute"]);
    }check(finished==1,"Binding retirement must cancel and join its owned execution");X::Value ignored;check(!retained["Status"].Call(std::vector<X::Value>{},ignored),"Retained execution object survived owner retirement");refuses([&]{NativeExecutionClient invalid(access,scope);});
}
}
int main(int argc,char** argv){try{if(argc==4&&std::string(argv[1])=="agent-channel-worker")return child(argv);if(argc!=1)return 2;process(false);process(true);lifetime();std::cout<<"Native xmind execution subobject verified: exact process/scope, wrong-ticket refusal, one dispatch, concurrent cancel, terminal intent before physical close, retained-object refusal and joined retirement."<<std::endl;return 0;}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}}
