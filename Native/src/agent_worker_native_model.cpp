#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/agent_worker_native_planning.hpp"
#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agent_worker_sdk_values.hpp"
#include "agentflow/agent_worker_journal_host.hpp"
#include "agentflow/agent_worker_read_host.hpp"
#include "agentflow/process_executor.hpp"
#include "agentflow/agent_worker_remote_host.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/a2a_tool_registry.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/agent_permissions.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/agent_worker_journal_values.hpp"
#include "nlohmann/json.hpp"
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
constexpr std::size_t document_limit=8*1024*1024,event_limit=32*1024*1024;
struct ModelJob;
std::string new_ticket(){unsigned char bytes[32];if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Native worker entropy unavailable");static constexpr char digits[]="0123456789abcdef";std::string out;for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;}
std::mutex registry_mutex;
std::map<std::string,std::weak_ptr<NativeWorkerModelState>> registry;
}
struct NativeWorkerModelState {
    AgentWorkerModelHost* host;
    AgentWorkerJournalHost* journal=nullptr;
    AgentReadToolHost* reads=nullptr;
    AgentFileToolHost* files=nullptr;
    AgentWorkerProcessService* processes=nullptr;
    AgentRemoteToolHost* remotes=nullptr;
    AgentWorkerCallableService* callables=nullptr;
    NativePlanningBinding* planning=nullptr;
    AgentWorkerLeaseSpec lease;
    std::atomic<bool> active=true;
    std::atomic<bool> outcome_unrecorded=false;
    mutable std::shared_mutex lifetime;
    std::mutex mutex;
    std::vector<std::shared_ptr<ModelJob>> jobs;
    explicit NativeWorkerModelState(AgentWorkerModelHost& owner,AgentWorkerJournalHost* captured,AgentReadToolHost* captured_reads,AgentFileToolHost* captured_files,AgentWorkerProcessService* captured_processes,AgentRemoteToolHost* captured_remotes,AgentWorkerCallableService* captured_callables,NativePlanningBinding* captured_planning):host(&owner),journal(captured),reads(captured_reads),files(captured_files),processes(captured_processes),remotes(captured_remotes),callables(captured_callables),planning(captured_planning),lease(owner.lease()){}
    auto enter()const{std::shared_lock lock(lifetime);if(!active)throw Conflict("Native worker service closed");return lock;}
    void close();
};
namespace {
struct ModelJob {
    std::weak_ptr<NativeWorkerModelState> owner;
    std::mutex mutex;
    std::vector<ModelEvent> events;
    std::size_t event_bytes=0;
    nlohmann::json result;
    std::string error,provider_code;
    int provider_status=0;
    bool done=false;
    std::stop_source stop;
    std::thread thread;
    ~ModelJob(){stop.request_stop();if(thread.joinable())thread.join();}
};
void fail(ModelJob& job,const std::string& code){std::lock_guard lock(job.mutex);job.error=code;job.done=true;}
void execute(const std::shared_ptr<NativeWorkerModelState>& state,const std::shared_ptr<ModelJob>& job,std::function<nlohmann::json(AgentWorkerModelHost&,ChatCompletionStream::Sink,std::stop_token)> action){
    try{
        auto life=state->enter();if(job->stop.stop_requested())throw TransportCancelled("Native model operation cancelled");
        auto result=action(*state->host,[&](const ModelEvent& event){const auto size=event.kind.size()+event.json.size();std::lock_guard lock(job->mutex);if(job->events.size()>=8192||size>document_limit||job->event_bytes+size>event_limit)throw ModelProtocolError("Native model event capacity exhausted");job->event_bytes+=size;job->events.push_back(event);},job->stop.get_token());
        if(result.dump().size()>document_limit)throw ModelProtocolError("Native model result capacity exhausted");std::lock_guard lock(job->mutex);job->result=std::move(result);job->done=true;
    }catch(const AgentCallableOutcomeUnrecorded&){state->outcome_unrecorded=true;fail(*job,"outcome_unrecorded");}catch(const AgentCallableEffectUncertain&){fail(*job,"callable_effect_uncertain");}catch(const AgentWorkerRemoteOutcomeUnrecorded&){state->outcome_unrecorded=true;fail(*job,"outcome_unrecorded");}catch(const McpOutcomeUnrecorded&){state->outcome_unrecorded=true;fail(*job,"outcome_unrecorded");}catch(const A2aOutcomeUnrecorded&){state->outcome_unrecorded=true;fail(*job,"outcome_unrecorded");}catch(const McpEffectUncertain&){fail(*job,"remote_effect_uncertain");}catch(const A2aEffectUncertain&){fail(*job,"remote_effect_uncertain");}catch(const ProcessOutcomeUnrecorded&){state->outcome_unrecorded=true;fail(*job,"outcome_unrecorded");}catch(const ProcessEffectUncertain&){fail(*job,"effect_uncertain");}catch(const AgentFileToolOutcomeUnrecorded&){state->outcome_unrecorded=true;fail(*job,"outcome_unrecorded");}catch(const AgentWorkerReadOutcomeUnrecorded&){state->outcome_unrecorded=true;fail(*job,"outcome_unrecorded");}catch(const ToolCancelled&){fail(*job,"cancelled");}catch(const PermissionCancelled&){fail(*job,"cancelled");}catch(const PermissionExpired&){fail(*job,"timeout");}catch(const PermissionDenied&){fail(*job,"rejected");}catch(const AgentPermissionDenied&){fail(*job,"rejected");}catch(const ToolAccessDenied&){fail(*job,"rejected");}catch(const McpProtocolError&){fail(*job,"rejected");}catch(const RootBudgetExhausted&){fail(*job,"budget_exhausted");}catch(const AgentWorkerJournalCapacity&){fail(*job,"capacity");}catch(const AgentWorkerModelOutcomeUnrecorded&){state->outcome_unrecorded=true;fail(*job,"outcome_unrecorded");}catch(const TransportCancelled&){fail(*job,"cancelled");}catch(const AgentWorkerAuthorityCancelled&){fail(*job,"cancelled");}catch(const TransportTimeout&){fail(*job,"timeout");}catch(const AgentWorkerAuthorityExpired&){fail(*job,"timeout");}catch(const ProviderHttpError& error){std::lock_guard lock(job->mutex);job->error="provider_http";job->provider_status=error.status;job->provider_code=error.status==400&&error.code=="context_length_exceeded"?error.code:std::string{};job->done=true;}catch(const Conflict&){fail(*job,"rejected");}catch(const NotFound&){fail(*job,"rejected");}catch(const std::invalid_argument&){fail(*job,"rejected");}catch(const ModelProtocolError&){fail(*job,"model_protocol");}catch(...){fail(*job,"outcome_unrecorded");}
}
struct ModelOperationApi {
    std::shared_ptr<ModelJob> job;
    ModelOperationApi()=default;
    explicit ModelOperationApi(std::shared_ptr<ModelJob> value):job(std::move(value)){}
    auto enter(){if(!job)throw Conflict("Unbound native worker operation");auto state=job->owner.lock();if(!state)throw Conflict("Native worker owner retired");return state->enter();}
    X::Value Status(){auto life=enter();std::lock_guard lock(job->mutex);auto value=X::Value::Dict(Host());value.Set("done",X::Value(job->done));value.Set("error",X::Value::String(Host(),job->error));value.Set("provider_status",X::Value(static_cast<std::int64_t>(job->provider_status)));value.Set("provider_code",X::Value::String(Host(),job->provider_code));value.Set("event_count",X::Value(static_cast<std::uint64_t>(job->events.size())));return value;}
    X::Value Events(std::uint64_t after){auto life=enter();std::lock_guard lock(job->mutex);if(after>job->events.size())throw Conflict("Native event position differs");auto result=X::Value::List(Host());for(auto i=after;i<job->events.size();++i)result.Append(sdk_values::to_value(Host(),nlohmann::json{{"kind",job->events[i].kind},{"json",job->events[i].json}}));return result;}
    X::Value Result(){auto life=enter();std::lock_guard lock(job->mutex);if(!job->done||!job->error.empty())throw Conflict("Native operation result unavailable");return sdk_values::to_value(Host(),job->result);}
    void Cancel(){if(job)job->stop.request_stop();}
    BEGIN_PACKAGE(ModelOperationApi)
        APISET().AddFunc<0>("Status",&ModelOperationApi::Status);
        APISET().AddFunc<1>("Events",&ModelOperationApi::Events);
        APISET().AddFunc<0>("Result",&ModelOperationApi::Result);
        APISET().AddFunc<0>("Cancel",&ModelOperationApi::Cancel);
    END_PACKAGE
};
struct ModelObjects {
    BEGIN_PACKAGE(ModelObjects)
        APISET().AddClass<0,NativeWorkerApi>("Worker");
        APISET().AddClass<0,NativeWorkerModelApi>("Model");
        APISET().AddClass<0,NativeWorkerJournalApi>("Journal");
        APISET().AddClass<0,NativeWorkerReadApi>("Reads");
        APISET().AddClass<0,NativeWorkerFileApi>("Files");
        APISET().AddClass<0,NativeWorkerProcessApi>("Processes");
        APISET().AddClass<0,NativeWorkerRemoteApi>("Remotes");
        APISET().AddClass<0,NativeWorkerCallableApi>("Callables");
        APISET().AddClass<0,ModelOperationApi>("Operation");
    END_PACKAGE
};
ModelObjects& objects(){static auto object=[] {auto value=std::make_shared<ModelObjects>();retain_xmind_ipc_service(value);(void)xmind_ipc_runtime().RegisterPackage("_xmind_model_objects",*value);return value;}();return *object;}
X::Value begin(const std::shared_ptr<NativeWorkerModelState>& state,std::function<nlohmann::json(AgentWorkerModelHost&,ChatCompletionStream::Sink,std::stop_token)> action){auto life=state->enter();auto job=std::make_shared<ModelJob>();job->owner=state;{std::lock_guard lock(state->mutex);for(auto it=state->jobs.begin();it!=state->jobs.end();){bool done=false;{std::lock_guard finished((*it)->mutex);done=(*it)->done;}if(done){if((*it)->thread.joinable())(*it)->thread.join();it=state->jobs.erase(it);}else ++it;}if(state->jobs.size()>=128)throw Conflict("Native worker active operation allowance exhausted");state->jobs.push_back(job);}job->thread=std::thread([state,job,action=std::move(action)]{execute(state,job,action);});return objects().__xlang3_package_->CreateInstance("Operation",std::make_shared<ModelOperationApi>(job));}
X::Value journal_result(const std::shared_ptr<NativeWorkerModelState>& state,X3PackageHost* package,std::function<std::string(AgentWorkerJournalHost&,std::stop_token)> action){
    std::string result,error;
    try{if(!state)throw Conflict("Unbound native worker journal");auto life=state->enter();if(!state->journal)throw Conflict("Native worker journal unavailable");result=action(*state->journal,{});}
    catch(const AgentWorkerAuthorityCancelled&){error="cancelled";}catch(const AgentWorkerAuthorityExpired&){error="timeout";}catch(const RootBudgetExhausted&){error="budget_exhausted";}catch(const AgentWorkerJournalCapacity&){error="capacity";}catch(const Conflict&){error="rejected";}catch(const NotFound&){error="rejected";}catch(const std::invalid_argument&){error="rejected";}catch(...){error="outcome_unrecorded";}
    auto value=X::Value::Dict(package);value.Set("error",X::Value::String(package,error));value.Set("value",error.empty()?sdk_values::to_value(package,nlohmann::json::parse(result)):X::Value(nullptr));return value;
}
X::Value begin_journal(const std::shared_ptr<NativeWorkerModelState>& state,std::function<std::string(AgentWorkerJournalHost&,std::stop_token)> action){if(!state)throw Conflict("Unbound native worker journal");return begin(state,[state,action=std::move(action)](auto&,auto,std::stop_token stop){if(!state->journal)throw Conflict("Worker journal unavailable");return nlohmann::json::parse(action(*state->journal,stop));});}
}
void NativeWorkerModelState::close(){active=false;std::vector<std::shared_ptr<ModelJob>> pending;{std::lock_guard lock(mutex);pending=jobs;}for(auto& job:pending)job->stop.request_stop();{std::unique_lock lock(lifetime);std::lock_guard data(mutex);pending=jobs;}for(auto& job:pending){job->stop.request_stop();if(job->thread.joinable())job->thread.join();}{std::unique_lock lock(lifetime);host=nullptr;journal=nullptr;reads=nullptr;files=nullptr;processes=nullptr;remotes=nullptr;callables=nullptr;planning=nullptr;std::lock_guard data(mutex);jobs.clear();}}
NativeWorkerModelBinding::NativeWorkerModelBinding(AgentWorkerModelHost& host,AgentWorkerJournalHost* journal,AgentReadToolHost* reads,AgentFileToolHost* files,AgentWorkerProcessService* processes,AgentRemoteToolHost* remotes,AgentWorkerCallableService* callables,NativePlanningBinding* planning):state_(std::make_shared<NativeWorkerModelState>(host,journal,reads,files,processes,remotes,callables,planning)),ticket_(new_ticket()){if(journal&&journal->lease()!=host.lease())throw Conflict("Native journal differs from worker model lease");if(planning){const auto& lease=host.lease();if(planning->scope()!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Native planning differs from worker lease");}ensure_xmind_ipc_api();(void)objects();std::lock_guard lock(registry_mutex);registry.emplace(ticket_,state_);}
NativeWorkerModelBinding::~NativeWorkerModelBinding(){{std::lock_guard lock(registry_mutex);registry.erase(ticket_);}state_->close();}
bool NativeWorkerModelBinding::outcome_unrecorded()const noexcept{try{auto life=state_->enter();return state_->outcome_unrecorded.load()||(state_->planning&&state_->planning->outcome_unrecorded());}catch(...){return true;}}
NativeWorkerModelAccess NativeWorkerModelBinding::access()const{return {"lrpc:"+std::to_string(xmind_ipc_port()),ticket_,GetCurrentProcessId()};}
NativeWorkerApi::NativeWorkerApi(NativeWorkersApi*,std::string ticket){std::lock_guard lock(registry_mutex);const auto found=registry.find(ticket);if(found==registry.end()||!(state=found->second.lock()))throw Conflict("Native worker access unavailable");auto life=state->enter();}
X::Value NativeWorkersApi::Open(std::string ticket){return objects().__xlang3_package_->CreateInstance("Worker",std::make_shared<NativeWorkerApi>(this,std::move(ticket)));}
X::Value NativeWorkerApi::Model(){if(!state)throw Conflict("Unbound native worker");auto life=state->enter();return objects().__xlang3_package_->CreateInstance("Model",std::make_shared<NativeWorkerModelApi>(this));}
X::Value NativeWorkerApi::Journal(){if(!state)throw Conflict("Unbound native worker");auto life=state->enter();if(!state->journal)throw Conflict("Worker journal unavailable");return objects().__xlang3_package_->CreateInstance("Journal",std::make_shared<NativeWorkerJournalApi>(this));}
X::Value NativeWorkerApi::Reads(){if(!state)throw Conflict("Unbound native worker");auto life=state->enter();if(!state->reads)throw Conflict("Worker reads unavailable");return objects().__xlang3_package_->CreateInstance("Reads",std::make_shared<NativeWorkerReadApi>(this));}
NativeWorkerReadApi::NativeWorkerReadApi(NativeWorkerApi* worker):state(worker->state){}
X::Value NativeWorkerReadApi::Invoke(X::Value invocation){auto input=AgentWorkspaceInvocation::parse(sdk_values::from_value(invocation).dump());return begin(state,[state=state,input=std::move(input)](auto&,auto,std::stop_token stop){if(!state->reads)throw Conflict("Worker reads unavailable");const auto result=state->reads->invoke(input,stop);return nlohmann::json{{"success",result.success},{"output_json",result.output_json}};});}
X::Value NativeWorkerApi::Files(){if(!state)throw Conflict("Unbound native worker");auto life=state->enter();if(!state->files)throw Conflict("Worker files unavailable");return objects().__xlang3_package_->CreateInstance("Files",std::make_shared<NativeWorkerFileApi>(this));}
NativeWorkerFileApi::NativeWorkerFileApi(NativeWorkerApi* worker):state(worker->state){}
X::Value NativeWorkerFileApi::Invoke(X::Value invocation){auto input=AgentFileToolInvocation::parse(sdk_values::from_value(invocation).dump());return begin(state,[state=state,input=std::move(input)](auto&,auto,std::stop_token stop){if(!state->files)throw Conflict("Worker files unavailable");const auto result=state->files->invoke(input,stop);return nlohmann::json{{"success",result.success},{"output_json",result.output_json}};});}
X::Value NativeWorkerApi::Processes(){if(!state)throw Conflict("Unbound native worker");auto life=state->enter();if(!state->processes)throw Conflict("Worker processes unavailable");return objects().__xlang3_package_->CreateInstance("Processes",std::make_shared<NativeWorkerProcessApi>(this));}
NativeWorkerProcessApi::NativeWorkerProcessApi(NativeWorkerApi* worker):state(worker->state){}
X::Value NativeWorkerProcessApi::Invoke(X::Value invocation){auto input=AgentProcessToolInvocation::parse(sdk_values::from_value(invocation).dump());return begin(state,[state=state,input=std::move(input)](auto&,auto,std::stop_token stop){if(!state->processes)throw Conflict("Worker processes unavailable");const auto result=state->processes->invoke(input,stop);return nlohmann::json{{"success",result.success},{"output_json",result.output_json}};});}
X::Value NativeWorkerProcessApi::Join(std::string attempt){return begin(state,[state=state,attempt=std::move(attempt)](auto&,auto,std::stop_token stop){if(!state->processes)throw Conflict("Worker processes unavailable");return nlohmann::json{{"joined",state->processes->join_processes(attempt,stop)}};});}
X::Value NativeWorkerApi::Remotes(){if(!state)throw Conflict("Unbound native worker");auto life=state->enter();if(!state->remotes)throw Conflict("Worker remotes unavailable");return objects().__xlang3_package_->CreateInstance("Remotes",std::make_shared<NativeWorkerRemoteApi>(this));}
NativeWorkerRemoteApi::NativeWorkerRemoteApi(NativeWorkerApi* worker):state(worker->state){}
X::Value NativeWorkerRemoteApi::Invoke(X::Value invocation){auto input=AgentRemoteToolInvocation::parse(sdk_values::from_value(invocation).dump());return begin(state,[state=state,input=std::move(input)](auto&,auto,std::stop_token stop){if(!state->remotes)throw Conflict("Worker remotes unavailable");const auto result=state->remotes->invoke(input,stop);return nlohmann::json{{"success",result.success},{"output_json",result.output_json}};});}
X::Value NativeWorkerApi::Callables(){if(!state)throw Conflict("Unbound native worker");auto life=state->enter();if(!state->callables)throw Conflict("Worker callables unavailable");return objects().__xlang3_package_->CreateInstance("Callables",std::make_shared<NativeWorkerCallableApi>(this));}
X::Value NativeWorkerApi::Planning(){if(!state)throw Conflict("Unbound native worker");auto life=state->enter();if(!state->planning)throw Conflict("Worker planning unavailable");return state->planning->object();}
NativeWorkerCallableApi::NativeWorkerCallableApi(NativeWorkerApi* worker):state(worker->state){}
X::Value NativeWorkerCallableApi::Invoke(X::Value invocation){auto input=AgentCallableInvocation::parse(sdk_values::from_value(invocation).dump());return begin(state,[state=state,input=std::move(input)](auto&,auto,std::stop_token stop){if(!state->callables)throw Conflict("Worker callables unavailable");const auto result=state->callables->invoke(input,stop);return nlohmann::json{{"success",result.success},{"output_json",result.output_json}};});}
X::Value NativeWorkerCallableApi::Catalogue(){return begin(state,[state=state](auto&,auto,std::stop_token stop){if(!state->callables)throw Conflict("Worker callables unavailable");return nlohmann::json(state->callables->tool_names(stop));});}
X::Value NativeWorkerApi::Identity(){if(!state)throw Conflict("Unbound native worker");auto life=state->enter();auto value=X::Value::Dict(Host());const auto& s=state->lease;for(const auto& [name,text]:std::initializer_list<std::pair<const char*,std::string>>{{"root",s.root_run_id},{"owner",s.owner_run_id},{"workspace",s.workspace_id},{"generation",s.generation}})value.Set(name,X::Value::String(Host(),text));value.Set("revision",X::Value(s.workspace_revision));return value;}
NativeWorkerModelApi::NativeWorkerModelApi(NativeWorkerApi* worker):state(worker->state){}
X::Value NativeWorkerModelApi::Admission(){return begin(state,[](auto& host,auto,std::stop_token stop){return nlohmann::json::parse(encode_agent_worker_model_admission(host.admission(stop)));});}
X::Value NativeWorkerModelApi::Prepare(){return begin(state,[](auto& host,auto,std::stop_token stop){return nlohmann::json::parse(encode_agent_model_request(host.prepare_request(stop)));});}
X::Value NativeWorkerModelApi::Rebuild(std::string attempt,X::Value request){auto input=decode_agent_model_request(sdk_values::from_value(request).dump());return begin(state,[attempt=std::move(attempt),input=std::move(input)](auto& host,auto,std::stop_token stop){return nlohmann::json::parse(encode_agent_model_request(host.rebuild_request(attempt,input,stop)));});}
X::Value NativeWorkerModelApi::Complete(std::string attempt,X::Value request){auto input=decode_agent_model_request(sdk_values::from_value(request).dump());return begin(state,[attempt=std::move(attempt),input=std::move(input)](auto& host,auto sink,std::stop_token stop){return nlohmann::json::parse(encode_agent_model_completion(host.complete(attempt,input,std::move(sink),stop)));});}
NativeWorkerJournalApi::NativeWorkerJournalApi(NativeWorkerApi* worker):state(worker->state){}
X::Value NativeWorkerJournalApi::Run(std::string id){return journal_result(state,Host(),[id](auto& host,std::stop_token stop){return encode_worker_journal_value(host.run(id,stop));});}
X::Value NativeWorkerJournalApi::Session(std::string id){return journal_result(state,Host(),[id](auto& host,std::stop_token stop){return encode_worker_journal_value(host.session(id,stop));});}
X::Value NativeWorkerJournalApi::History(std::string id){return journal_result(state,Host(),[id](auto& host,std::stop_token stop){return encode_worker_journal_value(host.history(id,stop));});}
X::Value NativeWorkerJournalApi::Events(std::string id,std::int64_t after){return journal_result(state,Host(),[id,after](auto& host,std::stop_token stop){return encode_worker_journal_value(host.events(id,after,stop));});}
X::Value NativeWorkerJournalApi::RootBudget(std::string id){return journal_result(state,Host(),[id](auto& host,std::stop_token stop){return encode_worker_journal_value(host.root_budget(id,stop));});}
X::Value NativeWorkerJournalApi::ChildAdmission(std::string id){return journal_result(state,Host(),[id](auto& host,std::stop_token stop){return encode_worker_journal_value(host.child_admission(id,stop));});}
X::Value NativeWorkerJournalApi::WorkspaceMode(){return journal_result(state,Host(),[](auto& host,std::stop_token stop){return nlohmann::json{{"coding",host.coding_workspace_admitted(stop)}}.dump();});}
X::Value NativeWorkerJournalApi::ExecutionWindow(){return journal_result(state,Host(),[](auto& host,std::stop_token stop){return nlohmann::json{{"remaining_ms",host.remaining_ms(stop)}}.dump();});}
X::Value NativeWorkerJournalApi::Begin(){return journal_result(state,Host(),[](auto& host,std::stop_token stop){return encode_worker_journal_value(host.begin_run(stop));});}
X::Value NativeWorkerJournalApi::ReserveModel(std::string attempt){return begin_journal(state,[attempt](auto& host,std::stop_token stop){return encode_worker_model_reservation(host.reserve_model(attempt,stop));});}
X::Value NativeWorkerJournalApi::ReservePlanningContinuation(std::string call,std::string attempt){return begin_journal(state,[call,attempt](auto& host,std::stop_token stop){return encode_worker_model_reservation(host.reserve_planning_continuation(call,attempt,stop));});}
X::Value NativeWorkerJournalApi::StartModel(std::string attempt){return begin_journal(state,[attempt](auto& host,std::stop_token stop){return encode_worker_model_reservation(host.start_model(attempt,stop));});}
X::Value NativeWorkerJournalApi::FinishModel(std::string attempt){return begin_journal(state,[attempt](auto& host,std::stop_token stop){return encode_worker_model_reservation(host.finish_model(attempt,stop));});}
X::Value NativeWorkerJournalApi::ModelEvent(std::string attempt,std::int64_t ordinal,std::string kind,std::string json){return journal_result(state,Host(),[attempt,ordinal,kind,json](auto& host,std::stop_token stop){return encode_worker_journal_value(std::vector<Event>{host.model_event(attempt,ordinal,kind,json,stop)});});}
X::Value NativeWorkerJournalApi::ToolEvent(std::string attempt,std::int64_t ordinal,bool returned){return journal_result(state,Host(),[attempt,ordinal,returned](auto& host,std::stop_token stop){return encode_worker_journal_value(std::vector<Event>{host.tool_event(attempt,ordinal,returned,stop)});});}
X::Value NativeWorkerJournalApi::WorkspaceToolEvent(std::string attempt,std::int64_t ordinal,bool returned){return journal_result(state,Host(),[attempt,ordinal,returned](auto& host,std::stop_token stop){return encode_worker_journal_value(std::vector<Event>{host.workspace_tool_event(attempt,ordinal,returned,stop)});});}
X::Value NativeWorkerJournalApi::CommitReadTurn(std::string attempt){return journal_result(state,Host(),[attempt](auto& host,std::stop_token stop){return encode_worker_tool_turn(host.commit_read_turn(attempt,stop));});}
X::Value NativeWorkerJournalApi::CommitWorkspaceTurn(std::string attempt){return journal_result(state,Host(),[attempt](auto& host,std::stop_token stop){return encode_worker_tool_turn(host.commit_workspace_turn(attempt,stop));});}
X::Value NativeWorkerJournalApi::Complete(std::string attempt){return journal_result(state,Host(),[attempt](auto& host,std::stop_token stop){return encode_worker_journal_value(host.complete_run(attempt,stop));});}

}
