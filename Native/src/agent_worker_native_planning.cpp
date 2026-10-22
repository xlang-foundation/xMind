#include "agentflow/agent_worker_native_planning.hpp"
#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agent_worker_sdk_values.hpp"
#include "agent_worker_ipc_wait.hpp"
#include <atomic>
#include <array>
#include <tuple>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <thread>
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
namespace agentflow {
namespace {
using Json=nlohmann::json;
void id(const std::string& text){if(text.empty()||text.size()>256||text.find('\0')!=std::string::npos)throw Conflict("Invalid planning identifier");}
void scope_valid(const AgentWorkerScope& s){id(s.root_id);id(s.run_id);id(s.workspace_id);id(s.generation);if(s.workspace_revision<1||s.workspace_revision>9007199254740991LL)throw Conflict("Invalid planning revision");}
RunState run_state(const std::string& text){for(auto s:{RunState::running,RunState::paused,RunState::completed,RunState::failed,RunState::cancelled})if(text==to_string(s))return s;throw Conflict("Invalid planning state");}
void validate(const DynamicExecutionResult& result,const AgentWorkerScope& scope){const auto& r=result.root;if(r.id!=scope.run_id||r.graph_root||r.parent_id.size()>256||r.node_id.size()>256||r.provider_context_json.size()>65536||(scope.root_id==scope.run_id)!=r.parent_id.empty()||result.paused!=(r.state==RunState::paused)||result.call_id.size()>256||result.result_json.size()>4*1024*1024)throw Conflict("Planning result differs from admitted owner");id(r.session_id);(void)run_state(to_string(r.state));if(!result.call_id.empty())id(result.call_id);if(!result.result_json.empty())(void)mcp_compact_object(result.result_json);if(!r.provider_context_json.empty())(void)mcp_compact_object(r.provider_context_json);}
Json identity(const AgentWorkerScope& s){return {{"root",s.root_id},{"owner",s.run_id},{"workspace",s.workspace_id},{"generation",s.generation},{"revision",s.workspace_revision}};}
Json projection(const DynamicExecutionResult& result,const AgentWorkerScope& scope,const std::string& request){validate(result,scope);const auto& r=result.root;return {{"request_call",request},{"call_id",result.call_id},{"result_json",result.result_json},{"paused",result.paused},{"run",{{"id",r.id},{"session_id",r.session_id},{"state",to_string(r.state)},{"parent_id",r.parent_id},{"node_id",r.node_id},{"graph_root",r.graph_root},{"provider_context_json",r.provider_context_json}}}};}
DynamicExecutionResult decode(const Json& value,const AgentWorkerScope& scope,const std::string& request,bool resume){if(!value.is_object()||value.size()!=5||value.at("request_call")!=request||!value.at("paused").is_boolean()||!value.at("run").is_object()||value.at("run").size()!=7||!value.at("run").at("graph_root").is_boolean())throw Conflict("Planning result shape differs");const auto& r=value.at("run");DynamicExecutionResult result{{r.at("id").get<std::string>(),r.at("session_id").get<std::string>(),run_state(r.at("state").get<std::string>()),r.at("parent_id").get<std::string>(),r.at("node_id").get<std::string>(),r.at("graph_root").get<bool>(),r.at("provider_context_json").get<std::string>()},value.at("call_id").get<std::string>(),value.at("result_json").get<std::string>(),value.at("paused").get<bool>()};validate(result,scope);if(resume&&!result.call_id.empty()&&result.call_id!=request)throw Conflict("Planning resumed a foreign accepted call");return result;}
std::string ticket(){std::array<unsigned char,32> bytes{};if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw Conflict("Planning entropy unavailable");constexpr char digits[]="0123456789abcdef";std::string out;for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;}
struct Job;
std::mutex registry_mutex;std::map<std::string,std::weak_ptr<NativePlanningState>> registry;
}
struct NativePlanningState {
    NativePlanningCallbacks callbacks;std::atomic<bool> active=true,unrecorded=false;
    std::shared_mutex lifetime;std::mutex mutex;std::set<std::tuple<std::string,std::string,std::string>> entered;
    bool busy=false;std::vector<std::shared_ptr<Job>> jobs;
    explicit NativePlanningState(NativePlanningCallbacks value):callbacks(std::move(value)){}
    auto enter(){std::shared_lock lock(lifetime);if(!active)throw Conflict("Planning owner retired");return lock;}
};
namespace {
struct Job {
    std::weak_ptr<NativePlanningState> owner;std::mutex mutex;std::stop_source stop;std::thread thread;
    bool done=false;std::string error;Json result;
    ~Job(){stop.request_stop();if(thread.joinable())thread.join();}
};
struct PlanningOperation {
    std::shared_ptr<Job> job;
    PlanningOperation()=default;explicit PlanningOperation(std::shared_ptr<Job> value):job(std::move(value)){}
    auto owner(){if(!job)throw Conflict("Unbound planning operation");auto state=job->owner.lock();if(!state)throw Conflict("Planning owner retired");return state;}
    X::Value Status(){auto state=owner();auto life=state->enter();std::lock_guard lock(job->mutex);return sdk_values::to_value(Host(),Json{{"done",job->done},{"error",job->error}});}
    X::Value Result(){auto state=owner();auto life=state->enter();std::lock_guard lock(job->mutex);if(!job->done||!job->error.empty())throw Conflict("Planning result unavailable");return sdk_values::to_value(Host(),job->result);}
    void Cancel(){auto state=owner();{auto life=state->enter();}job->stop.request_stop();}
    BEGIN_PACKAGE(PlanningOperation)
        APISET().AddFunc<0>("Status",&PlanningOperation::Status);
        APISET().AddFunc<0>("Result",&PlanningOperation::Result);
        APISET().AddFunc<0>("Cancel",&PlanningOperation::Cancel);
    END_PACKAGE
};
X::Value begin(const std::shared_ptr<NativePlanningState>&,std::string,std::string,bool);
struct PlanningService {
    std::shared_ptr<NativePlanningState> state;
    PlanningService()=default;explicit PlanningService(std::shared_ptr<NativePlanningState> value):state(std::move(value)){}
    auto enter(){if(!state)throw Conflict("Unbound planning service");return state->enter();}
    X::Value Identity(){auto life=enter();return sdk_values::to_value(Host(),identity(state->callbacks.scope));}
    bool Healthy(){auto life=enter();return state->callbacks.healthy();}
    X::Value Invoke(std::string attempt,std::string call){id(attempt);id(call);auto life=enter();return begin(state,std::move(attempt),std::move(call),false);}
    X::Value Resume(std::string call){id(call);auto life=enter();return begin(state,{},std::move(call),true);}
    BEGIN_PACKAGE(PlanningService)
        APISET().AddFunc<0>("Identity",&PlanningService::Identity);
        APISET().AddFunc<0>("Healthy",&PlanningService::Healthy);
        APISET().AddFunc<2>("Invoke",&PlanningService::Invoke);
        APISET().AddFunc<1>("Resume",&PlanningService::Resume);
    END_PACKAGE
};
struct Objects {BEGIN_PACKAGE(Objects) APISET().AddClass<0,PlanningService>("Service");APISET().AddClass<0,PlanningOperation>("Operation");END_PACKAGE};
Objects& objects(){static auto value=[] {auto owner=std::make_shared<Objects>();retain_xmind_ipc_service(owner);(void)xmind_ipc_runtime().RegisterPackage("_xmind_planning_objects",*owner);return owner;}();return *value;}
X::Value begin(const std::shared_ptr<NativePlanningState>& state,std::string attempt,std::string call,bool resume){
    auto job=std::make_shared<Job>();job->owner=state;
    {std::lock_guard lock(state->mutex);if(!state->active)throw Conflict("Planning owner retired");if(state->busy||state->entered.size()>=64||!state->entered.emplace(resume?"resume":"invoke",attempt,call).second){job->done=true;job->error="rejected";}else{state->busy=true;for(auto it=state->jobs.begin();it!=state->jobs.end();){bool done;{std::lock_guard finished((*it)->mutex);done=(*it)->done;}if(done){if((*it)->thread.joinable())(*it)->thread.join();it=state->jobs.erase(it);}else ++it;}state->jobs.push_back(job);}}
    if(!job->done)job->thread=std::thread([state,job,attempt=std::move(attempt),call=std::move(call),resume]{
        Json result;std::string error;
        try{auto life=state->enter();if(job->stop.stop_requested())throw TransportCancelled("Planning cancelled before entry");const auto actual=resume?state->callbacks.resume(call,job->stop.get_token()):state->callbacks.invoke(attempt,call,job->stop.get_token());try{result=projection(actual,state->callbacks.scope,call);}catch(...){throw DynamicOutcomeUnrecorded("Planning result cannot be projected; no replay authorized");}}
        catch(const DynamicPlanRejected& e){const std::set<std::string> allowed{"invalid_plan_arguments","plan_changed","planning_budget_exhausted","planning_depth_exhausted"};if(allowed.contains(e.code))error="rejected:"+e.code;else{state->unrecorded=true;error="outcome_unrecorded";}}
        catch(const DynamicPlanUnavailable&){error="unsupported";}catch(const TransportCancelled&){error="cancelled";}catch(...){state->unrecorded=true;error="outcome_unrecorded";}
        {std::lock_guard owner(state->mutex);std::lock_guard lock(job->mutex);job->result=std::move(result);job->error=std::move(error);state->busy=false;job->done=true;}
    });
    return objects().__xlang3_package_->CreateInstance("Operation",std::make_shared<PlanningOperation>(job));
}
}
X::Value NativePlanningApi::Open(std::string key){std::shared_ptr<NativePlanningState> state;{std::lock_guard lock(registry_mutex);const auto it=registry.find(key);if(it==registry.end()||!(state=it->second.lock()))throw Conflict("Planning access unavailable");}auto life=state->enter();return objects().__xlang3_package_->CreateInstance("Service",std::make_shared<PlanningService>(std::move(state)));}
NativePlanningBinding::NativePlanningBinding(NativePlanningCallbacks callbacks):state_(std::make_shared<NativePlanningState>(std::move(callbacks))),ticket_(ticket()){scope_valid(state_->callbacks.scope);if(!state_->callbacks.invoke||!state_->callbacks.resume||!state_->callbacks.healthy)throw std::invalid_argument("Planning requires captured backend callbacks");ensure_xmind_ipc_api();(void)objects();std::lock_guard lock(registry_mutex);registry.emplace(ticket_,state_);}
NativePlanningBinding::~NativePlanningBinding(){
    {std::lock_guard lock(registry_mutex);registry.erase(ticket_);}state_->active=false;
    std::vector<std::shared_ptr<Job>> jobs;{std::lock_guard lock(state_->mutex);jobs=state_->jobs;}
    for(auto& job:jobs)job->stop.request_stop();{std::unique_lock life(state_->lifetime);}
    for(auto& job:jobs)if(job->thread.joinable())job->thread.join();
    {std::unique_lock life(state_->lifetime);state_->callbacks.invoke={};state_->callbacks.resume={};state_->callbacks.healthy={};}
}
NativePlanningAccess NativePlanningBinding::access()const{return {"lrpc:"+std::to_string(xmind_ipc_port()),ticket_,GetCurrentProcessId()};}
AgentWorkerScope NativePlanningBinding::scope()const{return state_->callbacks.scope;}
X::Value NativePlanningBinding::object()const{return NativePlanningApi{}.Open(ticket_);}
bool NativePlanningBinding::outcome_unrecorded()const noexcept{return state_->unrecorded.load();}
struct NativePlanningClient::Impl {
    std::unique_ptr<X::Runtime> owned_runtime;std::shared_ptr<void> lifetime;X::Runtime& runtime;X::Value service,invoke,resume,health;AgentWorkerScope scope;AgentWorkerChannel::Deadline deadline;mutable std::mutex mutex;
    X::Value call(const X::Value& method,std::vector<X::Value> args={})const{X::Value out;if(!method.Call(args,out))throw DynamicOutcomeUnrecorded("Native planning acknowledgement unavailable; no replay authorized");return out;}
    Impl(const NativePlanningAccess& access,AgentWorkerScope captured,AgentWorkerChannel::Deadline until):owned_runtime(std::make_unique<X::Runtime>()),runtime(*owned_runtime),scope(std::move(captured)),deadline(until){scope_valid(scope);if(!access.endpoint.starts_with("lrpc:")||access.endpoint.size()>32||access.ticket.size()!=64||access.ticket.find_first_not_of("0123456789abcdef")!=std::string::npos||!access.pid)throw AgentWorkerProtocolError("Invalid planning access");X3LrpcEndpointInfo info{};info.size=sizeof(info);if(x3_runtime_probe_remote(runtime.get(),access.endpoint.c_str(),1000,&info)!=X3_STATUS_OK||info.pid!=access.pid)throw AgentWorkerProtocolError("Planning endpoint PID differs");auto root=runtime.ImportRemote("xmind",access.endpoint.c_str());auto services=call(root["Planning"]);service=call(services["Open"],{X::Value(runtime,access.ticket)});if(sdk_values::from_value(call(service["Identity"]))!=identity(scope))throw AgentWorkerProtocolError("Planning service scope differs");invoke=service["Invoke"];resume=service["Resume"];health=service["Healthy"];}
    Impl(X::Runtime& borrowed,X::Value object,AgentWorkerScope captured,AgentWorkerChannel::Deadline until,std::shared_ptr<void> owner):lifetime(std::move(owner)),runtime(borrowed),service(std::move(object)),scope(std::move(captured)),deadline(until){scope_valid(scope);if(!lifetime||!service.IsValid())throw AgentWorkerProtocolError("Planning requires its captured runtime and object");if(sdk_values::from_value(call(service["Identity"]))!=identity(scope))throw AgentWorkerProtocolError("Planning subobject scope differs");invoke=service["Invoke"];resume=service["Resume"];health=service["Healthy"];}
    DynamicExecutionResult request(std::string attempt,std::string key,bool resuming,std::stop_token stop){std::lock_guard lock(mutex);if(stop.stop_requested())throw TransportCancelled("Planning cancelled before dispatch");if(std::chrono::steady_clock::now()>=deadline)throw DynamicOutcomeUnrecorded("Planning observation deadline expired; retain ownership");auto operation=call(resuming?resume:invoke,resuming?std::vector<X::Value>{X::Value(runtime,key)}:std::vector<X::Value>{X::Value(runtime,attempt),X::Value(runtime,key)});const auto status=operation["Status"],result=operation["Result"],cancel=operation["Cancel"];AgentWorkerIpcWait poll;
        for(;;){if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline){try{call(cancel);}catch(...){}throw DynamicOutcomeUnrecorded("Planning observation interrupted after dispatch; retain ownership");}Json observed;try{observed=sdk_values::from_value(call(status));if(!observed.is_object()||observed.size()!=2||!observed.at("done").is_boolean()||!observed.at("error").is_string())throw Conflict("Invalid planning status");}catch(...){throw DynamicOutcomeUnrecorded("Planning status invalid; no replay authorized");}if(observed.at("done").get<bool>()){const auto error=observed.at("error").get<std::string>();if(error=="rejected")throw Conflict("Native planning invocation rejected");if(error.starts_with("rejected:")){const auto code=error.substr(9);if(code=="invalid_plan_arguments"||code=="plan_changed"||code=="planning_budget_exhausted"||code=="planning_depth_exhausted")throw DynamicPlanRejected(code);}if(error=="unsupported")throw DynamicPlanUnavailable("Native planning owner unavailable");if(!error.empty())throw DynamicOutcomeUnrecorded("Native planning outcome requires ownership recovery");try{return decode(sdk_values::from_value(call(result)),scope,key,resuming);}catch(...){throw DynamicOutcomeUnrecorded("Native planning result invalid; no replay authorized");}}poll.pause();}
    }
};
NativePlanningClient::NativePlanningClient(NativePlanningAccess access,AgentWorkerScope scope,AgentWorkerChannel::Deadline deadline):impl_(std::make_unique<Impl>(access,std::move(scope),deadline)){}
NativePlanningClient::NativePlanningClient(X::Runtime& runtime,X::Value object,AgentWorkerScope scope,AgentWorkerChannel::Deadline deadline,std::shared_ptr<void> owner):impl_(std::make_unique<Impl>(runtime,std::move(object),std::move(scope),deadline,std::move(owner))){}
NativePlanningClient::~NativePlanningClient()=default;
DynamicExecutionResult NativePlanningClient::invoke(const std::string& parent,const ModelToolCall& call,const std::string&,const std::string& attempt,const AgentSettings&,const DynamicPlanCapabilities&,std::shared_ptr<RootExecutionBudget>,std::stop_token stop,DynamicInstructionPrecondition){if(parent!=impl_->scope.run_id)throw Conflict("Planning caller differs from admitted owner");id(attempt);id(call.id);return impl_->request(attempt,call.id,false,stop);}
DynamicExecutionResult NativePlanningClient::resume(const std::string& parent,const std::string& call,const AgentSettings&,const DynamicPlanCapabilities&,std::shared_ptr<RootExecutionBudget>,std::stop_token stop,DynamicInstructionPrecondition){if(parent!=impl_->scope.run_id)throw Conflict("Planning resume differs from admitted owner");id(call);return impl_->request({},call,true,stop);}
bool NativePlanningClient::healthy()const{std::lock_guard lock(impl_->mutex);try{const auto value=impl_->call(impl_->health);if(value.raw().tag!=X3_TAG_BOOL)throw Conflict("Invalid planning health");return value.ToInt64()!=0;}catch(...){throw DynamicOutcomeUnrecorded("Native planning health unavailable");}}
}
