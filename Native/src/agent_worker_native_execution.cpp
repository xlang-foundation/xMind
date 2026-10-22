#include "agentflow/agent_worker_native_execution.hpp"
#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
namespace agentflow {
namespace {
std::mutex registry_mutex;
std::map<std::string,std::weak_ptr<NativeExecutionState>> registry;
std::string ticket(){unsigned char bytes[32];if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Execution entropy unavailable");static constexpr char digits[]="0123456789abcdef";std::string out;for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;}
void validate(const AgentWorkerScope& scope){for(const auto* field:{&scope.root_id,&scope.run_id,&scope.workspace_id,&scope.generation}){if(field->empty()||field->size()>256)throw AgentWorkerProtocolError("Invalid native execution scope");for(unsigned char c:*field)if(c<33||c>126)throw AgentWorkerProtocolError("Invalid native execution scope");}if(scope.workspace_revision<1||scope.workspace_revision>9007199254740991LL)throw AgentWorkerProtocolError("Invalid native execution revision");}
}
struct NativeExecutionState {
    AgentWorkerScope scope;
    std::function<RunState(std::stop_token)> action;
    std::mutex mutex;std::condition_variable_any changed;
    bool active=true,started=false,done=false,closed=false;
    std::string outcome;
    std::stop_source stop;
    std::thread thread;
    NativeExecutionState(AgentWorkerScope captured,std::function<RunState(std::stop_token)> callback):scope(std::move(captured)),action(std::move(callback)){}
    void check()const{if(!active)throw AgentWorkerProtocolError("Native execution owner retired");}
};
namespace {
struct ExecutionApi {
    std::shared_ptr<NativeExecutionState> state;
    ExecutionApi()=default;
    explicit ExecutionApi(std::shared_ptr<NativeExecutionState> owner):state(std::move(owner)){}
    std::unique_lock<std::mutex> enter(){if(!state)throw AgentWorkerProtocolError("Unbound native execution");std::unique_lock lock(state->mutex);state->check();return lock;}
    X::Value Identity(){auto lock=enter();auto value=X::Value::Dict(Host());const auto& s=state->scope;for(const auto& [key,text]:std::initializer_list<std::pair<const char*,std::string>>{{"root",s.root_id},{"owner",s.run_id},{"workspace",s.workspace_id},{"generation",s.generation}})value.Set(key,X::Value::String(Host(),text));value.Set("revision",X::Value(s.workspace_revision));return value;}
    void Execute(){auto lock=enter();if(state->started||state->closed)throw AgentWorkerProtocolError("Native execution cannot restart");state->started=true;auto owner=state;state->thread=std::thread([owner]{RunState intent=RunState::failed;try{intent=owner->action(owner->stop.get_token());}catch(...){}std::lock_guard finished(owner->mutex);owner->outcome=intent==RunState::completed?"completed":intent==RunState::cancelled?"cancelled":"failed";owner->done=true;owner->changed.notify_all();});}
    X::Value Status(){auto lock=enter();auto value=X::Value::Dict(Host());value.Set("started",X::Value(state->started));value.Set("done",X::Value(state->done));value.Set("outcome",X::Value::String(Host(),state->outcome));return value;}
    void Cancel(){auto lock=enter();auto stop=state->stop;lock.unlock();stop.request_stop();}
    void Close(){auto lock=enter();state->closed=true;auto stop=state->stop;state->changed.notify_all();lock.unlock();stop.request_stop();}
    BEGIN_PACKAGE(ExecutionApi)
        APISET().AddFunc<0>("Identity",&ExecutionApi::Identity);
        APISET().AddFunc<0>("Execute",&ExecutionApi::Execute);
        APISET().AddFunc<0>("Status",&ExecutionApi::Status);
        APISET().AddFunc<0>("Cancel",&ExecutionApi::Cancel);
        APISET().AddFunc<0>("Close",&ExecutionApi::Close);
    END_PACKAGE
};
struct ExecutionObjects {BEGIN_PACKAGE(ExecutionObjects) APISET().AddClass<0,ExecutionApi>("Execution"); END_PACKAGE};
ExecutionObjects& objects(){static auto value=[] {auto owner=std::make_shared<ExecutionObjects>();retain_xmind_ipc_service(owner);(void)xmind_ipc_runtime().RegisterPackage("_xmind_execution_objects",*owner);return owner;}();return *value;}
}
X::Value NativeExecutionsApi::Open(std::string key){std::shared_ptr<NativeExecutionState> owner;{std::lock_guard lock(registry_mutex);const auto found=registry.find(key);if(found==registry.end()||!(owner=found->second.lock()))throw AgentWorkerProtocolError("Native execution access unavailable");}{std::lock_guard lock(owner->mutex);owner->check();}return objects().__xlang3_package_->CreateInstance("Execution",std::make_shared<ExecutionApi>(std::move(owner)));}
NativeExecutionBinding::NativeExecutionBinding(AgentWorkerScope scope,std::function<RunState(std::stop_token)> action):state_(std::make_shared<NativeExecutionState>(std::move(scope),std::move(action))),ticket_(ticket()){validate(state_->scope);if(!state_->action)throw std::invalid_argument("Native execution requires its captured action");ensure_xmind_ipc_api();(void)objects();std::lock_guard lock(registry_mutex);registry.emplace(ticket_,state_);}
NativeExecutionBinding::~NativeExecutionBinding(){{std::lock_guard lock(registry_mutex);registry.erase(ticket_);}{std::lock_guard lock(state_->mutex);state_->active=false;state_->closed=true;state_->changed.notify_all();}state_->stop.request_stop();if(state_->thread.joinable())state_->thread.join();state_->action={};}
NativeExecutionAccess NativeExecutionBinding::access()const{return {"lrpc:"+std::to_string(xmind_ipc_port()),ticket_,GetCurrentProcessId()};}
void NativeExecutionBinding::close()noexcept{{std::lock_guard lock(state_->mutex);state_->closed=true;state_->changed.notify_all();}state_->stop.request_stop();}
void NativeExecutionBinding::wait_closed(AgentWorkerChannel::Deadline deadline,std::stop_token stop){std::unique_lock lock(state_->mutex);if(!state_->changed.wait_until(lock,stop,deadline,[&]{return state_->closed;})){if(stop.stop_requested())throw AgentWorkerChannelCancelled("Native execution wait cancelled");throw AgentWorkerChannelTimeout("Native execution wait deadline elapsed");}}
namespace {
// The control observer needs a short bounded wait, not the system timer tick.
// Failure to obtain a high-resolution timer preserves the original wait path.
struct ExecutionPollWait {
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
    ~ExecutionPollWait(){if(timer)CloseHandle(timer);}
    void pause(){LARGE_INTEGER due{};due.QuadPart=-20000;if(timer&&SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE)){if(WaitForSingleObject(timer,10)==WAIT_OBJECT_0)return;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
};
}
struct NativeExecutionClient::Impl {
    X::Runtime runtime;X::Value execution,start,status,cancel,close;
    ExecutionPollWait poll;
    std::mutex execution_mutex,cancel_mutex;std::atomic<bool> dispatched=false,closed=false;AgentWorkerScope scope;
    X::Value invoke(const X::Value& method){X::Value result;if(!method.Call(std::vector<X::Value>{},result))throw AgentWorkerProtocolError("Native execution acknowledgement unavailable; no replay authorized");return result;}
    Impl(const NativeExecutionAccess& access,const AgentWorkerScope& scope):scope(scope){validate(scope);if(!access.endpoint.starts_with("lrpc:")||access.endpoint.size()>32||access.ticket.size()!=64||access.ticket.find_first_not_of("0123456789abcdef")!=std::string::npos||!access.pid)throw AgentWorkerProtocolError("Invalid native execution access");X3LrpcEndpointInfo info{};info.size=sizeof(info);if(x3_runtime_probe_remote(runtime.get(),access.endpoint.c_str(),1000,&info)!=X3_STATUS_OK||info.pid!=access.pid)throw AgentWorkerProtocolError("Native execution endpoint PID differs");auto root=runtime.ImportRemote("xmind",access.endpoint.c_str());auto services=invoke(root["Executions"]);X::Value opened;if(!services["Open"].Call(std::vector<X::Value>{X::Value(runtime,access.ticket)},opened))throw AgentWorkerProtocolError("Native execution access refused");execution=std::move(opened);const auto identity=invoke(execution["Identity"]);if(!identity.IsDict()||identity.Size()!=5||identity["root"].ToString()!=scope.root_id||identity["owner"].ToString()!=scope.run_id||identity["workspace"].ToString()!=scope.workspace_id||identity["generation"].ToString()!=scope.generation||!identity["revision"].IsInt64()||identity["revision"].ToInt64()!=scope.workspace_revision)throw AgentWorkerProtocolError("Native execution object scope differs");start=execution["Execute"];status=execution["Status"];cancel=execution["Cancel"];close=execution["Close"];}
};
NativeExecutionClient::NativeExecutionClient(NativeExecutionAccess access,AgentWorkerScope scope):impl_(std::make_unique<Impl>(access,scope)){}
NativeExecutionClient::~NativeExecutionClient()=default;
RunState NativeExecutionClient::execute(AgentWorkerChannel::Deadline deadline,std::stop_token stop){std::lock_guard lock(impl_->execution_mutex);if(stop.stop_requested())throw AgentWorkerChannelCancelled("Native execution cancelled before dispatch");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerChannelTimeout("Native execution expired before dispatch");if(impl_->closed.load())throw AgentWorkerProtocolError("Native execution is closed");if(impl_->dispatched.exchange(true))throw AgentWorkerProtocolError("Native execution cannot be retried");impl_->invoke(impl_->start);for(;;){if(stop.stop_requested()){cancel();throw AgentWorkerChannelCancelled("Native execution observation cancelled");}if(std::chrono::steady_clock::now()>=deadline){cancel();throw AgentWorkerChannelTimeout("Native execution observation expired; retain ownership");}const auto value=impl_->invoke(impl_->status);if(!value.IsDict()||value.Size()!=3||value["started"].raw().tag!=X3_TAG_BOOL||!value["started"].ToInt64()||value["done"].raw().tag!=X3_TAG_BOOL||!value["outcome"].IsString())throw AgentWorkerProtocolError("Native execution status differs");const auto outcome=value["outcome"].ToString();if(value["done"].ToInt64()){if(outcome=="completed")return RunState::completed;if(outcome=="cancelled")return RunState::cancelled;if(outcome=="failed")return RunState::failed;throw AgentWorkerProtocolError("Invalid native execution intent");}if(!outcome.empty())throw AgentWorkerProtocolError("Premature native execution intent");impl_->poll.pause();}}
void NativeExecutionClient::cancel(){std::lock_guard lock(impl_->cancel_mutex);impl_->invoke(impl_->cancel);}
bool NativeExecutionClient::closed()const noexcept{return impl_->closed.load();}
const AgentWorkerScope& NativeExecutionClient::scope()const noexcept{return impl_->scope;}
void NativeExecutionClient::close()noexcept{if(impl_->closed.exchange(true))return;try{std::lock_guard lock(impl_->cancel_mutex);impl_->invoke(impl_->close);}catch(...){} }
}
