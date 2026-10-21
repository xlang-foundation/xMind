#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agentflow/http_stream_transport.hpp"
#include <atomic>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
namespace agentflow {
namespace {
constexpr std::size_t chunk_limit=65536,document_limit=8*1024*1024,event_limit=32*1024*1024;
struct ModelJob;
struct ModelInput {std::mutex mutex;std::uint64_t id=0;std::string bytes;bool sealed=false;};
std::string new_ticket(){unsigned char bytes[32];if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Native worker entropy unavailable");static constexpr char digits[]="0123456789abcdef";std::string out;for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;}
std::mutex registry_mutex;
std::map<std::string,std::weak_ptr<NativeWorkerModelState>> registry;
}
struct NativeWorkerModelState {
    AgentWorkerModelHost* host;
    AgentWorkerLeaseSpec lease;
    std::atomic<bool> active=true;
    mutable std::shared_mutex lifetime;
    std::mutex mutex;
    std::vector<std::shared_ptr<ModelJob>> jobs;
    std::map<std::uint64_t,std::shared_ptr<ModelInput>> inputs;
    std::uint64_t next_input=0;
    explicit NativeWorkerModelState(AgentWorkerModelHost& owner):host(&owner),lease(owner.lease()){}
    auto enter()const{std::shared_lock lock(lifetime);if(!active)throw Conflict("Native worker service closed");return lock;}
    void close();
};
namespace {
struct ModelJob {
    std::weak_ptr<NativeWorkerModelState> owner;
    std::mutex mutex;
    std::vector<std::string> events;
    std::size_t event_bytes=0;
    std::string result,error,provider_code;
    int provider_status=0;
    bool done=false;
    std::stop_source stop;
    std::thread thread;
    ~ModelJob(){stop.request_stop();if(thread.joinable())thread.join();}
};
void fail(ModelJob& job,const std::string& code){std::lock_guard lock(job.mutex);job.error=code;job.done=true;}
void execute(const std::shared_ptr<NativeWorkerModelState>& state,const std::shared_ptr<ModelJob>& job,std::function<std::string(AgentWorkerModelHost&,ChatCompletionStream::Sink,std::stop_token)> action){
    try{
        auto life=state->enter();if(job->stop.stop_requested())throw TransportCancelled("Native model operation cancelled");
        auto result=action(*state->host,[&](const ModelEvent& event){auto bytes=encode_agent_model_event(event);std::lock_guard lock(job->mutex);if(job->events.size()>=8192||bytes.size()>document_limit||job->event_bytes+bytes.size()>event_limit)throw ModelProtocolError("Native model event capacity exhausted");job->event_bytes+=bytes.size();job->events.push_back(std::move(bytes));},job->stop.get_token());
        if(result.size()>document_limit)throw ModelProtocolError("Native model result capacity exhausted");std::lock_guard lock(job->mutex);job->result=std::move(result);job->done=true;
    }catch(const AgentWorkerModelOutcomeUnrecorded&){fail(*job,"outcome_unrecorded");}catch(const TransportCancelled&){fail(*job,"cancelled");}catch(const AgentWorkerAuthorityCancelled&){fail(*job,"cancelled");}catch(const TransportTimeout&){fail(*job,"timeout");}catch(const AgentWorkerAuthorityExpired&){fail(*job,"timeout");}catch(const ProviderHttpError& error){std::lock_guard lock(job->mutex);job->error="provider_http";job->provider_status=error.status;job->provider_code=error.status==400&&error.code=="context_length_exceeded"?error.code:std::string{};job->done=true;}catch(const Conflict&){fail(*job,"rejected");}catch(const NotFound&){fail(*job,"rejected");}catch(const std::invalid_argument&){fail(*job,"rejected");}catch(const ModelProtocolError&){fail(*job,"model_protocol");}catch(...){fail(*job,"outcome_unrecorded");}
}
X::Value read_bytes(X3PackageHost* host,const std::string& bytes,std::uint64_t offset,std::uint64_t count){if(count>chunk_limit||offset>bytes.size()||count>bytes.size()-offset)throw Conflict("Native model byte range invalid");return X::Value::Bytes(host,bytes.data()+offset,count);}
struct ModelRequestApi {
    std::shared_ptr<NativeWorkerModelState> state;
    std::shared_ptr<ModelInput> input;
    ModelRequestApi()=default;
    ModelRequestApi(std::shared_ptr<NativeWorkerModelState> owner,std::shared_ptr<ModelInput> value):state(std::move(owner)),input(std::move(value)){}
    std::uint64_t Id(){if(!state||!input)throw Conflict("Unbound native model request");auto life=state->enter();return input->id;}
    void Append(X::Value bytes){if(!state||!input)throw Conflict("Unbound native model request");auto life=state->enter();std::uint64_t length=0;const auto* data=bytes.BytesData(&length);std::lock_guard lock(input->mutex);if(!bytes.IsBin()||(!data&&length)||length>chunk_limit||input->sealed||input->bytes.size()+length>document_limit)throw Conflict("Native model request append invalid");if(length)input->bytes.append(static_cast<const char*>(data),length);}
    void Seal(){if(!state||!input)throw Conflict("Unbound native model request");auto life=state->enter();std::lock_guard lock(input->mutex);(void)decode_agent_model_request(input->bytes);input->sealed=true;}
    void Release(){if(!state||!input)return;auto life=state->enter();std::lock_guard lock(state->mutex);state->inputs.erase(input->id);}
    BEGIN_PACKAGE(ModelRequestApi)
        APISET().AddFunc<0>("Id",&ModelRequestApi::Id);
        APISET().AddFunc<1>("Append",&ModelRequestApi::Append,X3_NATIVE_IPC_ARGS_BY_VALUE);
        APISET().AddFunc<0>("Seal",&ModelRequestApi::Seal);
        APISET().AddFunc<0>("Release",&ModelRequestApi::Release);
    END_PACKAGE
};
struct ModelOperationApi {
    std::shared_ptr<ModelJob> job;
    ModelOperationApi()=default;
    explicit ModelOperationApi(std::shared_ptr<ModelJob> value):job(std::move(value)){}
    auto enter(){if(!job)throw Conflict("Unbound native model operation");auto state=job->owner.lock();if(!state)throw Conflict("Native model owner retired");return state->enter();}
    X::Value Status(){auto life=enter();std::lock_guard lock(job->mutex);auto value=X::Value::Dict(Host()),sizes=X::Value::List(Host());for(const auto& event:job->events)sizes.Append(X::Value(static_cast<std::uint64_t>(event.size())));value.Set("events",sizes);value.Set("done",X::Value(job->done));value.Set("error",X::Value::String(Host(),job->error));value.Set("provider_status",X::Value(static_cast<std::int64_t>(job->provider_status)));value.Set("provider_code",X::Value::String(Host(),job->provider_code));value.Set("result_size",X::Value(static_cast<std::uint64_t>(job->result.size())));return value;}
    X::Value ReadEvent(std::uint64_t ordinal,std::uint64_t offset,std::uint64_t count){auto life=enter();std::lock_guard lock(job->mutex);return read_bytes(Host(),job->events.at(ordinal),offset,count);}
    X::Value ReadResult(std::uint64_t offset,std::uint64_t count){auto life=enter();std::lock_guard lock(job->mutex);if(!job->done||!job->error.empty())throw Conflict("Native model result unavailable");return read_bytes(Host(),job->result,offset,count);}
    void Cancel(){if(job)job->stop.request_stop();}
    BEGIN_PACKAGE(ModelOperationApi)
        APISET().AddFunc<0>("Status",&ModelOperationApi::Status);
        APISET().AddFunc<3>("ReadEvent",&ModelOperationApi::ReadEvent);
        APISET().AddFunc<2>("ReadResult",&ModelOperationApi::ReadResult);
        APISET().AddFunc<0>("Cancel",&ModelOperationApi::Cancel);
    END_PACKAGE
};
struct ModelObjects {
    BEGIN_PACKAGE(ModelObjects)
        APISET().AddClass<0,NativeWorkerApi>("Worker");
        APISET().AddClass<0,NativeWorkerModelApi>("Model");
        APISET().AddClass<0,ModelRequestApi>("Request");
        APISET().AddClass<0,ModelOperationApi>("Operation");
    END_PACKAGE
};
ModelObjects& objects(){static auto object=[] {auto value=std::make_shared<ModelObjects>();retain_xmind_ipc_service(value);(void)xmind_ipc_runtime().RegisterPackage("_xmind_model_objects",*value);return value;}();return *object;}
ModelRequest consume(const std::shared_ptr<NativeWorkerModelState>& state,std::uint64_t id){std::shared_ptr<ModelInput> input;{std::lock_guard lock(state->mutex);const auto found=state->inputs.find(id);if(found==state->inputs.end())throw Conflict("Native model request does not belong to this worker");input=found->second;state->inputs.erase(found);}std::lock_guard lock(input->mutex);if(!input->sealed)throw Conflict("Native model request is not sealed");return decode_agent_model_request(input->bytes);}
X::Value begin(const std::shared_ptr<NativeWorkerModelState>& state,std::function<std::string(AgentWorkerModelHost&,ChatCompletionStream::Sink,std::stop_token)> action){auto life=state->enter();auto job=std::make_shared<ModelJob>();job->owner=state;{std::lock_guard lock(state->mutex);if(state->jobs.size()>=128)throw Conflict("Native model operation allowance exhausted");state->jobs.push_back(job);}job->thread=std::thread([state,job,action=std::move(action)]{execute(state,job,action);});return objects().__xlang3_package_->CreateInstance("Operation",std::make_shared<ModelOperationApi>(job));}
}
void NativeWorkerModelState::close(){active=false;std::vector<std::shared_ptr<ModelJob>> pending;{std::lock_guard lock(mutex);pending=jobs;}for(auto& job:pending)job->stop.request_stop();{std::unique_lock lock(lifetime);std::lock_guard data(mutex);pending=jobs;}for(auto& job:pending){job->stop.request_stop();if(job->thread.joinable())job->thread.join();}{std::unique_lock lock(lifetime);host=nullptr;std::lock_guard data(mutex);jobs.clear();inputs.clear();}}
NativeWorkerModelBinding::NativeWorkerModelBinding(AgentWorkerModelHost& host):state_(std::make_shared<NativeWorkerModelState>(host)),ticket_(new_ticket()){ensure_xmind_ipc_api();(void)objects();std::lock_guard lock(registry_mutex);registry.emplace(ticket_,state_);}
NativeWorkerModelBinding::~NativeWorkerModelBinding(){{std::lock_guard lock(registry_mutex);registry.erase(ticket_);}state_->close();}
NativeWorkerModelAccess NativeWorkerModelBinding::access()const{return {"lrpc:"+std::to_string(xmind_ipc_port()),ticket_,GetCurrentProcessId()};}
NativeWorkerApi::NativeWorkerApi(NativeWorkersApi*,std::string ticket){std::lock_guard lock(registry_mutex);const auto found=registry.find(ticket);if(found==registry.end()||!(state=found->second.lock()))throw Conflict("Native worker access unavailable");auto life=state->enter();}
X::Value NativeWorkersApi::Open(std::string ticket){return objects().__xlang3_package_->CreateInstance("Worker",std::make_shared<NativeWorkerApi>(this,std::move(ticket)));}
X::Value NativeWorkerApi::Model(){if(!state)throw Conflict("Unbound native worker");auto life=state->enter();return objects().__xlang3_package_->CreateInstance("Model",std::make_shared<NativeWorkerModelApi>(this));}
X::Value NativeWorkerApi::Identity(){if(!state)throw Conflict("Unbound native worker");auto life=state->enter();auto value=X::Value::Dict(Host());const auto& s=state->lease;for(const auto& [name,text]:std::initializer_list<std::pair<const char*,std::string>>{{"root",s.root_run_id},{"owner",s.owner_run_id},{"workspace",s.workspace_id},{"generation",s.generation}})value.Set(name,X::Value::String(Host(),text));value.Set("revision",X::Value(s.workspace_revision));return value;}
NativeWorkerModelApi::NativeWorkerModelApi(NativeWorkerApi* worker):state(worker->state){}
X::Value NativeWorkerModelApi::Admission(){return begin(state,[](auto& host,auto,std::stop_token stop){return encode_agent_worker_model_admission(host.admission(stop));});}
X::Value NativeWorkerModelApi::NewRequest(){auto life=state->enter();auto input=std::make_shared<ModelInput>();{std::lock_guard lock(state->mutex);if(state->inputs.size()>=4)throw Conflict("Native model request capacity exhausted");input->id=++state->next_input;state->inputs.emplace(input->id,input);}return objects().__xlang3_package_->CreateInstance("Request",std::make_shared<ModelRequestApi>(state,input));}
X::Value NativeWorkerModelApi::Prepare(){return begin(state,[](auto& host,auto,std::stop_token stop){return encode_agent_model_request(host.prepare_request(stop));});}
X::Value NativeWorkerModelApi::Rebuild(std::string attempt,std::uint64_t request){auto life=state->enter();auto input=consume(state,request);life.unlock();return begin(state,[attempt=std::move(attempt),input=std::move(input)](auto& host,auto,std::stop_token stop){return encode_agent_model_request(host.rebuild_request(attempt,input,stop));});}
X::Value NativeWorkerModelApi::Complete(std::string attempt,std::uint64_t request){auto life=state->enter();auto input=consume(state,request);life.unlock();return begin(state,[attempt=std::move(attempt),input=std::move(input)](auto& host,auto sink,std::stop_token stop){return encode_agent_model_completion(host.complete(attempt,input,std::move(sink),stop));});}
}
