#include "agentflow/agent_worker_native_planning.hpp"
#include "agentflow/agent_runner.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace agentflow;using Json=nlohmann::json;using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class F>void rejects(F action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected native planning refusal");}
AgentWorkerScope scope(){return {"planning-root","planning-root","planning-workspace","planning-generation",3};}
DynamicExecutionResult result(bool paused=false){return {{"planning-root","planning-session",paused?RunState::paused:RunState::running,"","",false,R"({"model_id":"controlled"})"},"accepted-native-call",R"({"observed":"\u96ea","native_receipt":true})",paused};}
std::string image(){wchar_t path[32768]{};const auto size=GetModuleFileNameW(nullptr,path,32768);check(size>0&&size<32768,"Planning fixture image unavailable");return std::filesystem::path(path).string();}
int child(char** argv){
    auto channel=AgentWorkerChannel::accept_inherited(std::stoull(argv[3]),Clock::now()+10s);check(channel->scope()==scope(),"Planning scope differs");
    const auto boot=channel->receive(Clock::now()+10s);check(boot.operation=="planning.fixture","Missing planning access");const auto value=Json::parse(boot.payload);NativePlanningAccess access{value.at("endpoint"),value.at("ticket"),value.at("pid")};
    auto wrong=access;++wrong.pid;rejects<AgentWorkerProtocolError>([&]{NativePlanningClient invalid(wrong,scope(),Clock::now()+5s);});auto foreign=scope();foreign.generation="foreign";rejects<AgentWorkerProtocolError>([&]{NativePlanningClient invalid(access,foreign,Clock::now()+5s);});wrong=access;wrong.ticket[0]=wrong.ticket[0]=='a'?'b':'a';rejects<DynamicOutcomeUnrecorded>([&]{NativePlanningClient invalid(wrong,scope(),Clock::now()+5s);});
    NativePlanningClient client(access,scope(),Clock::now()+20s);AgentSettings settings;settings.instructions="NEVER_SERIALIZE_NATIVE_SETTINGS";DynamicPlanCapabilities capabilities;capabilities.backend_identity="NEVER_SERIALIZE_PRIVATE_CAPABILITIES";const ModelToolCall call{"provider-call","plan_tasks",R"({"native_only":"NEVER_SERIALIZE_ARGUMENTS"})"};
    const auto actual=client.invoke(scope().run_id,call,"NEVER_SERIALIZE_ASSISTANT","native-attempt",settings,capabilities,{});check(actual.root.id==scope().run_id&&!actual.paused&&actual.call_id=="accepted-native-call"&&actual.result_json==result().result_json,"Native planning result differs");
    rejects<Conflict>([&]{client.invoke(scope().run_id,call,"","native-attempt",settings,capabilities,{});});rejects<Conflict>([&]{client.invoke("foreign-root",call,"","new-attempt",settings,capabilities,{});});check(client.healthy(),"Native planning health unavailable");
    const auto paused=client.resume(scope().run_id,"accepted-native-call",settings,capabilities,{});check(paused.paused&&paused.root.state==RunState::paused&&paused.result_json==actual.result_json,"Paused planning result differs");rejects<Conflict>([&]{client.resume(scope().run_id,"accepted-native-call",settings,capabilities,{});});
    try{client.invoke(scope().run_id,{"rejected-call","plan_tasks","{}"},"","rejected-attempt",settings,capabilities,{});throw std::runtime_error("Rejected plan succeeded");}catch(const DynamicPlanRejected& e){check(e.code=="plan_changed","Native rejection code differs");}
    for(const auto* attempt:{"uncertain-attempt","bad-result-attempt"})rejects<DynamicOutcomeUnrecorded>([&]{client.invoke(scope().run_id,{attempt,"plan_tasks","{}"},"",attempt,settings,capabilities,{});});
    rejects<DynamicOutcomeUnrecorded>([&]{client.resume(scope().run_id,"foreign-native-call",settings,capabilities,{});});
    std::stop_source stop;std::exception_ptr notice_error;std::jthread timer([&]{try{check(channel->receive(Clock::now()+5s).operation=="planning.cancel.started","Cancellation must follow actual callback entry");}catch(...){notice_error=std::current_exception();}stop.request_stop();});rejects<DynamicOutcomeUnrecorded>([&]{client.invoke(scope().run_id,{"cancel-call","plan_tasks","{}"},"","cancel-attempt",settings,capabilities,{},stop.get_token());});
    timer.join();if(notice_error)std::rethrow_exception(notice_error);
    channel->send({"planning.done","native"},Clock::now()+5s);return 0;
}
void lifetime(){std::atomic<bool> finished=false,started=false;std::unique_ptr<NativePlanningClient> retained;NativePlanningCallbacks callbacks;callbacks.scope=scope();callbacks.healthy=[] {return true;};callbacks.invoke=[&](auto&,auto&,std::stop_token stop){started=true;while(!stop.stop_requested())Sleep(1);finished=true;return result();};callbacks.resume=[](auto&,std::stop_token){return result(true);};X::Runtime runtime;X::Value operation;
    {NativePlanningBinding binding(callbacks);retained=std::make_unique<NativePlanningClient>(binding.access(),scope(),Clock::now()+5s);auto root=runtime.ImportRemote("xmind",binding.access().endpoint.c_str());auto planning=root["Planning"]();auto service=planning["Open"](binding.access().ticket);operation=service["Invoke"]("retained-attempt","retained-call");const auto until=Clock::now()+2s;while(!started&&Clock::now()<until)Sleep(1);check(started,"Retirement must follow actual callback entry");}
    check(finished,"Planning retirement must join its cancelled callback");rejects<DynamicOutcomeUnrecorded>([&]{retained->healthy();});X::Value ignored;check(!operation["Status"].Call(std::vector<X::Value>{},ignored),"Retained planning operation survived retirement");
}
}
int main(int argc,char** argv){try{
    if(argc==4&&std::string(argv[1])=="agent-channel-worker")return child(argv);check(argc==1,"Unexpected planning fixture arguments");
    std::atomic<unsigned> invokes=0,resumes=0;std::atomic<bool> cancelled=false;AgentWorkerChannel* callback_channel=nullptr;NativePlanningCallbacks callbacks;callbacks.scope=scope();callbacks.healthy=[] {return true;};callbacks.invoke=[&](const std::string& attempt,const std::string& call,std::stop_token stop){++invokes;check(attempt.find("NEVER_SERIALIZE")==std::string::npos&&call.find("NEVER_SERIALIZE")==std::string::npos,"Private native capabilities crossed IPC");if(attempt=="rejected-attempt")throw DynamicPlanRejected("plan_changed");if(attempt=="uncertain-attempt")throw DynamicOutcomeUnrecorded("Synthetic lost durable result");if(attempt=="cancel-attempt"){callback_channel->send({"planning.cancel.started","native"},Clock::now()+5s);while(!stop.stop_requested())Sleep(1);cancelled=true;throw TransportCancelled("Controlled cancel");}auto actual=result();if(attempt=="bad-result-attempt")actual.root.id="foreign-root";return actual;};callbacks.resume=[&](const std::string&,std::stop_token){++resumes;return result(true);};
    {NativePlanningBinding binding(callbacks);auto worker=AgentWorkerProcess::launch(ForegroundProcess::bind_executable(image()),scope(),Clock::now()+10s);check(worker->pid()!=GetCurrentProcessId(),"Planning requires a distinct process");callback_channel=&worker->channel();const auto access=binding.access();worker->channel().send({"planning.fixture",Json{{"endpoint",access.endpoint},{"ticket",access.ticket},{"pid",access.pid}}.dump()},Clock::now()+5s);check(worker->channel().receive(Clock::now()+10s).operation=="planning.done","Native planning child did not finish");check(worker->wait(Clock::now()+5s)==0,"Native planning child failed");const auto until=Clock::now()+2s;while(!cancelled&&Clock::now()<until)Sleep(1);check(invokes==5&&resumes==2&&cancelled&&binding.outcome_unrecorded(),"Native planning replay, cancellation or uncertainty facts differ");}
    lifetime();std::cout<<"Native planning SDK contract passed: distinct child, exact scope/PID/ticket, invoke/resume/health, paused result, duplicate refusal, bounded rejection, invalid/unrecorded results, cancellation and retired-object refusal. Controlled callbacks only; production scheduler integration remains open."<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
