#include "agentflow/persistence_service.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/graph.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <string_view>
#include <iomanip>
#include <random>
#include <sstream>

namespace agentflow {
namespace {
std::string owner_nonce(){std::random_device random;std::ostringstream out;out<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)out<<std::setw(8)<<random();return out.str();}
bool language_server_credential_id(const std::string& id){
    constexpr std::string_view prefix="language-server-env-";
    if(!id.starts_with(prefix)||id.size()>256)return false;
    const auto revision=id.rfind("-r");if(revision==std::string::npos||revision<=prefix.size()||revision+2==id.size())return false;
    for(const char value:id.substr(prefix.size(),revision-prefix.size()))
        if(!((value>='a'&&value<='z')||(value>='A'&&value<='Z')||(value>='0'&&value<='9')||value=='_'||value=='.'||value=='-'))return false;
    for(const char value:id.substr(revision+2))if(value<'0'||value>'9')return false;
    return true;
}
}
struct PersistenceService::Impl {
    using Request=std::function<void(Repository&)>;
    std::mutex mutex,close_mutex;
    std::condition_variable changed;
    std::deque<Request> pending;
    std::thread worker;
    std::size_t limit;
    bool accepting=true;
    const bool replacement_bootstrap;
    // The lease pointer exists only while the worker-owned Repository is alive.
    BackendLease* owner_lease=nullptr;
    const std::string generation=owner_nonce();

    Impl(std::string path,std::vector<std::string> roots,std::size_t max_pending,std::optional<BackendOwnerBootstrap> bootstrap,std::optional<LegacyOwnerBootstrap> legacy):limit(max_pending),replacement_bootstrap(bootstrap.has_value()) {
        if(limit==0) throw std::invalid_argument("Persistence queue capacity must be positive");
        std::promise<void> ready;auto initialized=ready.get_future();
        worker=std::thread([this,path=std::move(path),roots=std::move(roots),bootstrap=std::move(bootstrap),legacy=std::move(legacy),ready=std::move(ready)]() mutable {
            bool started=false;
            try {
                BackendLease lease(path);
                Repository repository(path,roots,&lease,bootstrap?&*bootstrap:nullptr,generation,legacy?&*legacy:nullptr);
                if(!bootstrap&&!legacy)repository.open_backend_owner(lease,generation);
                if(!bootstrap&&!legacy)repository.recover_interrupted(lease);
                owner_lease=&lease;
                ready.set_value();started=true;
                for(;;) {
                    Request request;
                    {
                        std::unique_lock lock(mutex);
                        changed.wait(lock,[this]{return !pending.empty() || !accepting;});
                        if(pending.empty()) break;
                        request=std::move(pending.front());pending.pop_front();
                    }
                    // packaged_task captures operation exceptions in its future.
                    request(repository);
                }
                // Repository destruction precedes lease release, on this thread.
                owner_lease=nullptr;
            } catch(...) {
                if(!started) ready.set_exception(std::current_exception());
                std::lock_guard lock(mutex);accepting=false;
                // Destroying tasks resolves their futures with broken_promise.
                pending.clear();
            }
        });
        try {initialized.get();} catch(...) {worker.join();throw;}
    }
    ~Impl() {close();}
    void close() {
        std::lock_guard closing(close_mutex);
        {
            std::lock_guard lock(mutex);accepting=false;
        }
        changed.notify_all();
        if(worker.joinable()) worker.join();
    }
    template<class Function> auto submit(Function action,bool mutation=true) {
        using Result=std::invoke_result_t<Function,Repository&>;
        auto task=std::make_shared<std::packaged_task<Result(Repository&)>>([this,action=std::move(action),mutation](Repository& repository)mutable->Result{
            // Read durable state on the worker for every mutation. A lost or
            // uncertain commit response cannot leave a stale memory flag that
            // reopens admission against a committed quiescence receipt.
            if(mutation){const auto owner=repository.backend_owner();if(owner.generation!=generation)throw Conflict("Native database owner generation changed");if(owner.quiesced)throw BackendQuiesced("Native backend is quiesced; admission is closed");}
            return action(repository);
        });
        auto result=task->get_future();
        {
            std::lock_guard lock(mutex);
            if(!accepting) throw PersistenceClosed("Persistence service is closed");
            if(pending.size()>=limit) throw PersistenceBusy("Persistence queue is full");
            pending.emplace_back([task=std::move(task)](Repository& repository){(*task)(repository);});
        }
        changed.notify_one();return result;
    }
    template<class Function> auto observe(Function action){return submit(std::move(action),false);}
};
PersistenceService::PersistenceService(std::string database,std::vector<std::string> roots,std::size_t limit,std::optional<BackendOwnerBootstrap> bootstrap,std::optional<LegacyOwnerBootstrap> legacy)
    :impl_(std::make_unique<Impl>(std::move(database),std::move(roots),limit,std::move(bootstrap),std::move(legacy))) {}
PersistenceService::~PersistenceService()=default;
void PersistenceService::close() {impl_->close();}
std::future<BackendOwnerState> PersistenceService::backend_owner(){return impl_->observe([](Repository& r){return r.backend_owner();});}
std::future<BackendOwnerState> PersistenceService::quiesce_backend(BackendOwnerPrecondition expected){return impl_->observe([this,expected=std::move(expected)](Repository& r){
    if(expected.generation!=impl_->generation)throw Conflict("Backend owner generation changed");
    auto result=r.quiesce_backend_owner(*impl_->owner_lease,expected,owner_nonce());
    return result;
});}
std::future<BackendOwnerState> PersistenceService::resume_backend(BackendOwnerReceipt expected){return impl_->observe([this,expected=std::move(expected)](Repository& r){
    if(expected.generation!=impl_->generation)throw Conflict("Backend owner generation changed");
    auto result=r.resume_backend_owner(*impl_->owner_lease,expected);
    return result;
});}
std::future<BackendOwnerState> PersistenceService::request_backend_retirement(BackendOwnerReceipt expected,std::optional<BackendOwnerTarget> target){return impl_->observe([this,expected=std::move(expected),target=std::move(target)](Repository& r){
    if(expected.generation!=impl_->generation)throw Conflict("Backend owner generation changed");
    return r.request_backend_retirement(*impl_->owner_lease,expected,target);
});}
std::future<BackendOwnerState> PersistenceService::activate_backend_replacement(BackendOwnerReceipt expected,BackendOwnerTarget target){return impl_->observe([this,expected=std::move(expected),target=std::move(target)](Repository& r){
    if(expected.generation!=impl_->generation)throw Conflict("Backend owner generation changed");
    return r.activate_backend_replacement(*impl_->owner_lease,expected,target);
});}
std::future<DynamicPlanCapabilities> PersistenceService::dynamic_capabilities(std::string root) {return impl_->observe([root=std::move(root)](Repository& repository){return repository.dynamic_capabilities(root);});}
std::future<DynamicPlanCapabilities> PersistenceService::finalize_dynamic_capabilities(std::string root,std::string backend_identity,std::string catalogue_json,std::vector<DynamicPresetCapability> presets) {return impl_->submit([root=std::move(root),backend_identity=std::move(backend_identity),catalogue_json=std::move(catalogue_json),presets=std::move(presets)](Repository& repository){return repository.finalize_dynamic_capabilities(root,backend_identity,catalogue_json,presets);});}
std::future<DynamicPlanRecord> PersistenceService::dynamic_plan(std::string plan) {return impl_->observe([plan=std::move(plan)](Repository& repository){return repository.dynamic_plan(plan);});}
std::future<std::optional<DynamicPlanRecord>> PersistenceService::dynamic_plan_for_root(std::string root) {return impl_->observe([root=std::move(root)](Repository& repository){return repository.dynamic_plan_for_root(root);});}
std::future<std::optional<DynamicPlanRecord>> PersistenceService::dynamic_plan_for_owner(std::string owner) {return impl_->observe([owner=std::move(owner)](Repository& repository){return repository.dynamic_plan_for_owner(owner);});}
std::future<std::vector<DynamicPlanRecord>> PersistenceService::dynamic_plans_for_root(std::string root) {return impl_->observe([root=std::move(root)](Repository& repository){return repository.dynamic_plans_for_root(root);});}
std::future<DynamicPlanCallRecord> PersistenceService::dynamic_plan_call(std::string call) {return impl_->observe([call=std::move(call)](Repository& repository){return repository.dynamic_plan_call(call);});}
std::future<std::vector<DynamicPlanCallRecord>> PersistenceService::dynamic_plan_calls(std::string root) {return impl_->observe([root=std::move(root)](Repository& repository){return repository.dynamic_plan_calls(root);});}
std::future<std::vector<DynamicPlanRevisionRecord>> PersistenceService::dynamic_plan_revisions(std::string plan) {return impl_->observe([plan=std::move(plan)](Repository& repository){return repository.dynamic_plan_revisions(plan);});}
std::future<DynamicPlanRevisionRecord> PersistenceService::dynamic_plan_revision(std::string plan,std::int64_t revision) {return impl_->observe([plan=std::move(plan),revision](Repository& repository){return repository.dynamic_plan_revision(plan,revision);});}
std::future<DynamicPlanCallRecord> PersistenceService::accept_dynamic_plan_change(DynamicPlanChangeSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.accept_dynamic_plan_change(spec);});}
std::future<DynamicFrontierClaim> PersistenceService::admit_dynamic_frontier(DynamicFrontierSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.admit_dynamic_frontier(spec);});}
std::future<DynamicNodeRecord> PersistenceService::settle_dynamic_child(std::string child) {return impl_->submit([child=std::move(child)](Repository& repository){return repository.settle_dynamic_child(child);});}
std::future<DynamicPlanStepResult> PersistenceService::settle_dynamic_plan_step(std::string call) {return impl_->submit([call=std::move(call)](Repository& repository){return repository.settle_dynamic_plan_step(call);});}
std::future<DynamicHumanRequest> PersistenceService::publish_dynamic_human(DynamicHumanRequestSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.publish_dynamic_human(spec);});}
std::future<DynamicHumanRequest> PersistenceService::dynamic_human_request(std::string plan,std::string request) {return impl_->observe([plan=std::move(plan),request=std::move(request)](Repository& repository){return repository.dynamic_human_request(plan,request);});}
std::future<std::vector<DynamicHumanRequest>> PersistenceService::dynamic_human_requests(std::string plan) {return impl_->observe([plan=std::move(plan)](Repository& repository){return repository.dynamic_human_requests(plan);});}
std::future<DynamicHumanRequest> PersistenceService::input_dynamic_human(DynamicHumanInputSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.input_dynamic_human(spec);});}
std::future<DynamicHumanRequest> PersistenceService::expire_dynamic_human(std::string plan,std::string request) {return impl_->submit([plan=std::move(plan),request=std::move(request)](Repository& repository){return repository.expire_dynamic_human(plan,request);});}
std::future<DynamicBudgetSegment> PersistenceService::open_dynamic_budget_segment(DynamicSegmentSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.open_dynamic_budget_segment(spec);});}
std::future<DynamicBudgetSegment> PersistenceService::dynamic_budget_segment(std::string root) {return impl_->observe([root=std::move(root)](Repository& repository){return repository.dynamic_budget_segment(root);});}
std::future<DynamicPauseBinding> PersistenceService::dynamic_owner_pause(std::string root) {return impl_->observe([root=std::move(root)](Repository& repository){return repository.dynamic_owner_pause(root);});}
std::future<std::optional<ContextPausePin>> PersistenceService::dynamic_context_pause(std::string root) {return impl_->submit([root=std::move(root)](Repository& repository){return repository.dynamic_context_pause(root);});}
std::future<Run> PersistenceService::suspend_dynamic_owner(DynamicPauseSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.suspend_dynamic_owner(spec);});}
std::future<DynamicResumeRecord> PersistenceService::resume_dynamic_owner(DynamicResumeSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.resume_dynamic_owner(spec);});}
std::future<void> PersistenceService::commit_dynamic_tool_turn(std::string root,std::string call) {return impl_->submit([root=std::move(root),call=std::move(call)](Repository& repository){repository.commit_dynamic_tool_turn(root,call);});}
std::future<void> PersistenceService::record_rejected_dynamic_tool_turn(std::string root,std::string origin_attempt_id,std::string actual_assistant_json,std::string safe_code) {return impl_->submit([root=std::move(root),origin_attempt_id=std::move(origin_attempt_id),actual_assistant_json=std::move(actual_assistant_json),safe_code=std::move(safe_code)](Repository& repository){repository.record_rejected_dynamic_tool_turn(root,origin_attempt_id,actual_assistant_json,safe_code);});}
std::future<ModelCallReservation> PersistenceService::reserve_dynamic_continuation(std::string call,std::string attempt) {return impl_->submit([call=std::move(call),attempt=std::move(attempt)](Repository& repository){return repository.reserve_dynamic_continuation(call,attempt);});}
std::future<Run> PersistenceService::retire_dynamic_owner(std::string root,RunState terminal_state,std::string reason_json,std::string segment_id,std::int64_t measured_active_elapsed_ms) {return impl_->submit([root=std::move(root),terminal_state,reason_json=std::move(reason_json),segment_id=std::move(segment_id),measured_active_elapsed_ms](Repository& repository){return repository.retire_dynamic_owner(root,terminal_state,reason_json,segment_id,measured_active_elapsed_ms);});}
std::future<Run> PersistenceService::complete_dynamic_owner(std::string root,std::string assistant_json,std::string segment_id,std::int64_t measured_active_elapsed_ms) {return impl_->submit([root=std::move(root),assistant_json=std::move(assistant_json),segment_id=std::move(segment_id),measured_active_elapsed_ms](Repository& repository){return repository.complete_dynamic_owner(root,assistant_json,segment_id,measured_active_elapsed_ms);});}
std::future<Session> PersistenceService::create_session(std::string id,std::string title) {
    return impl_->submit([id=std::move(id),title=std::move(title)](Repository& repository){return repository.create_session(id,title);});
}
std::future<Session> PersistenceService::import_session(std::string id,std::string title,std::vector<Message> messages,std::optional<AgentDefinition> agent) {
    return impl_->submit([id=std::move(id),title=std::move(title),messages=std::move(messages),agent=std::move(agent)](Repository& repository){return repository.import_session(id,title,messages,agent);});
}
std::future<SessionArchive> PersistenceService::export_session(std::string id) {
    return impl_->observe([id=std::move(id)](Repository& repository){return repository.export_session(id);});
}
std::future<Session> PersistenceService::fork_session(std::string id,std::string source,std::optional<std::int64_t> before) {
    return impl_->submit([id=std::move(id),source=std::move(source),before](Repository& repository){return repository.fork_session(id,source,before);});
}
std::future<Session> PersistenceService::session(std::string id) {
    return impl_->observe([id=std::move(id)](Repository& repository){return repository.session(id);});
}
std::future<Session> PersistenceService::rename_session(std::string id,std::string title,std::string expected_title) {
    return impl_->submit([id=std::move(id),title=std::move(title),expected_title=std::move(expected_title)](Repository& repository){return repository.rename_session(id,title,expected_title);});
}
std::future<std::vector<Session>> PersistenceService::sessions() {
    return impl_->observe([](Repository& repository){return repository.sessions();});
}
std::future<void> PersistenceService::delete_session(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){repository.delete_session(id);});
}
std::future<Run> PersistenceService::create_run(std::string id,std::string session_id) {
    return impl_->submit([id=std::move(id),session_id=std::move(session_id)](Repository& repository){return repository.create_run(id,session_id);});
}
std::future<Run> PersistenceService::run(std::string id) {
    return impl_->observe([id=std::move(id)](Repository& repository){return repository.run(id);});
}
std::future<std::optional<Run>> PersistenceService::incoming_message(std::string message,std::string context,std::string identity,std::string content){return impl_->observe([message=std::move(message),context=std::move(context),identity=std::move(identity),content=std::move(content)](Repository& repository){return repository.incoming_message(message,context,identity,content);});}
std::future<Run> PersistenceService::start_incoming_message(std::string id,std::string context,std::string message,std::string prompt,std::string identity,std::optional<RootBudgetSpec> budget,std::optional<DynamicPlanCapabilities> dynamic){return impl_->submit([id=std::move(id),context=std::move(context),message=std::move(message),prompt=std::move(prompt),identity=std::move(identity),budget=std::move(budget),dynamic=std::move(dynamic)](Repository& repository){return repository.start_incoming_message(id,context,message,prompt,identity,std::move(budget),std::move(dynamic));});}
std::future<std::optional<Run>> PersistenceService::a2a_continuation(std::string message,std::string task,std::string context,std::string identity,std::string content,std::string payload){return impl_->observe([message=std::move(message),task=std::move(task),context=std::move(context),identity=std::move(identity),content=std::move(content),payload=std::move(payload)](Repository& repository){return repository.a2a_continuation(message,task,context,identity,content,payload);});}
std::future<Run> PersistenceService::continue_a2a_message(DynamicHumanInputSpec answer,std::string task,std::string message,std::string context,std::string identity,std::string content,std::string payload){return impl_->submit([answer=std::move(answer),task=std::move(task),message=std::move(message),context=std::move(context),identity=std::move(identity),content=std::move(content),payload=std::move(payload)](Repository& repository){return repository.continue_a2a_message(task,answer,message,context,identity,content,payload);});}
std::future<std::optional<std::vector<Message>>> PersistenceService::task_history(std::string id){return impl_->observe([id=std::move(id)](Repository& repository){return repository.task_history(id);});}
std::future<std::optional<std::string>> PersistenceService::incoming_message_payload(std::string id,std::string message){return impl_->observe([id=std::move(id),message=std::move(message)](Repository& repository){return repository.incoming_message_payload(id,message);});}
std::future<std::vector<Event>> PersistenceService::event_batch(std::string id,std::int64_t after,std::size_t count){return impl_->observe([id=std::move(id),after,count](Repository& repository){return repository.event_batch(id,after,count);});}
std::future<RootRunPage> PersistenceService::list_root_runs(std::string context,std::string state,std::optional<std::int64_t> since,std::size_t count,std::int64_t watermark,std::optional<std::int64_t> cursor_ms,std::int64_t cursor_sequence){return impl_->observe([context=std::move(context),state=std::move(state),since,count,watermark,cursor_ms,cursor_sequence](Repository& repository){return repository.list_root_runs(context,state,since,count,watermark,cursor_ms,cursor_sequence);});}
std::future<Run> PersistenceService::start_prompt_run(std::string id,std::string session_id,std::string json,std::optional<RootBudgetSpec> budget,std::optional<DynamicPlanCapabilities> dynamic,std::string inbox_id) {
    return impl_->submit([id=std::move(id),session_id=std::move(session_id),json=std::move(json),budget=std::move(budget),dynamic=std::move(dynamic),inbox_id=std::move(inbox_id)](Repository& repository){return repository.start_prompt_run(id,session_id,json,std::move(budget),std::move(dynamic),inbox_id);});
}
std::future<RootBudgetRecord> PersistenceService::root_budget(std::string id){return impl_->observe([id=std::move(id)](Repository& repository){return repository.root_budget(id);});}

std::future<AgentWorkerLeaseRecord> PersistenceService::admit_agent_worker(AgentWorkerLeaseSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.admit_agent_worker(spec);});}
std::future<AgentWorkerLeaseRecord> PersistenceService::agent_worker_lease(std::string generation){return impl_->observe([generation=std::move(generation)](Repository& r){return r.agent_worker_lease(generation);});}
std::future<AgentWorkerJournalValue> PersistenceService::read_agent_worker_journal(AgentWorkerLeaseSpec lease,AgentWorkerJournalRead read){return impl_->observe([lease=std::move(lease),read=std::move(read)](Repository& r){return r.read_agent_worker_journal(lease,read);});}
std::future<Run> PersistenceService::start_agent_worker_run(AgentWorkerLeaseSpec lease,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){return impl_->submit([lease=std::move(lease),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker startup cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker startup deadline elapsed");return r.transition(lease.owner_run_id,RunState::queued,RunState::running,"{}",&lease);});}
std::future<Event> PersistenceService::record_agent_worker_model_event(AgentWorkerModelClaimSpec spec,std::string kind,std::string json){return impl_->submit([spec=std::move(spec),kind=std::move(kind),json=std::move(json)](Repository& r){return r.record_agent_worker_model_event(spec,kind,json);});}
std::future<Event> PersistenceService::agent_worker_model_event(AgentWorkerLeaseSpec lease,std::string attempt,std::int64_t ordinal,std::string kind,std::string json){return impl_->observe([lease=std::move(lease),attempt=std::move(attempt),ordinal,kind=std::move(kind),json=std::move(json)](Repository& r){return r.agent_worker_model_event(lease,attempt,ordinal,kind,json);});}
std::future<ModelCallReservation> PersistenceService::reserve_agent_worker_model(AgentWorkerLeaseSpec lease,std::string attempt,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){
    return impl_->submit([lease=std::move(lease),attempt=std::move(attempt),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker funding cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker funding deadline elapsed");return r.reserve_model_call(lease.root_run_id,lease.owner_run_id,attempt,lease.root_run_id==lease.owner_run_id?ModelCallRole::parent:ModelCallRole::leaf,&lease);});
}
std::future<ModelCallReservation> PersistenceService::start_agent_worker_model(AgentWorkerLeaseSpec lease,std::string attempt,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){
    return impl_->submit([lease=std::move(lease),attempt=std::move(attempt),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker start cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker start deadline elapsed");return r.start_model_call(lease.root_run_id,lease.owner_run_id,attempt,&lease);});
}
std::future<AgentWorkerLeaseRecord> PersistenceService::retire_agent_worker(std::string generation,std::string reason){return impl_->submit([generation=std::move(generation),reason=std::move(reason)](Repository& r){return r.retire_agent_worker(generation,reason);});}
std::future<Run> PersistenceService::terminate_agent_worker(AgentWorkerLeaseSpec lease,RunState outcome,std::string reason){return impl_->submit([lease=std::move(lease),outcome,reason=std::move(reason)](Repository& r){return r.terminate_agent_worker(lease,outcome,reason);});}
std::future<AgentWorkerModelClaimRecord> PersistenceService::claim_agent_worker_model(AgentWorkerModelClaimSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.claim_agent_worker_model(spec);});}
std::future<AgentWorkerModelClaimRecord> PersistenceService::agent_worker_model_claim(std::string root,std::string attempt){return impl_->observe([root=std::move(root),attempt=std::move(attempt)](Repository& r){return r.agent_worker_model_claim(root,attempt);});}
std::future<AgentWorkerModelClaimRecord> PersistenceService::interrupt_agent_worker_model(AgentWorkerModelClaimSpec spec,AgentWorkerModelFailure failure){return impl_->submit([spec=std::move(spec),failure](Repository& r){return r.interrupt_agent_worker_model(spec,failure);});}
std::future<AgentWorkerModelClaimRecord> PersistenceService::record_agent_worker_model_response(AgentWorkerModelClaimSpec spec,std::string actual,std::string assistant){return impl_->submit([spec=std::move(spec),actual=std::move(actual),assistant=std::move(assistant)](Repository& r){return r.record_agent_worker_model_response(spec,actual,assistant);});}
std::future<AgentWorkerReadClaimRecord> PersistenceService::claim_agent_worker_read(AgentWorkerLeaseSpec lease,AgentWorkspaceInvocation call,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){return impl_->submit([lease=std::move(lease),call=std::move(call),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker read cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker read deadline elapsed");return r.claim_agent_worker_read(lease,call);});}
std::future<AgentWorkerReadClaimRecord> PersistenceService::start_agent_worker_read(AgentWorkerLeaseSpec lease,AgentWorkspaceInvocation call,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){return impl_->submit([lease=std::move(lease),call=std::move(call),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker read dispatch cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker read dispatch deadline elapsed");return r.start_agent_worker_read(lease,call);});}
std::future<AgentWorkerReadClaimRecord> PersistenceService::record_agent_worker_read_result(AgentWorkerLeaseSpec lease,AgentWorkspaceInvocation call,bool success,std::string output){return impl_->submit([lease=std::move(lease),call=std::move(call),success,output=std::move(output)](Repository& r){return r.record_agent_worker_read_result(lease,call,success,output);});}
std::future<AgentWorkerReadClaimRecord> PersistenceService::agent_worker_read_claim(std::string root,std::string attempt,std::string call){return impl_->observe([root=std::move(root),attempt=std::move(attempt),call=std::move(call)](Repository& r){return r.agent_worker_read_claim(root,attempt,call);});}
std::future<AgentWorkerFileClaimRecord> PersistenceService::claim_agent_worker_file(AgentWorkerLeaseSpec lease,AgentFileToolInvocation call,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){return impl_->submit([lease=std::move(lease),call=std::move(call),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker file call cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker file call deadline elapsed");return r.claim_agent_worker_file(lease,call);});}
std::future<AgentWorkerFileClaimRecord> PersistenceService::start_agent_worker_file(AgentWorkerLeaseSpec lease,AgentFileToolInvocation call,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){return impl_->submit([lease=std::move(lease),call=std::move(call),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker file dispatch cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker file dispatch deadline elapsed");return r.start_agent_worker_file(lease,call);});}
std::future<AgentWorkerFileClaimRecord> PersistenceService::claim_agent_worker_file_effect(AgentWorkerLeaseSpec lease,AgentFileToolInvocation call,std::string id,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){return impl_->submit([lease=std::move(lease),call=std::move(call),id=std::move(id),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker file effect cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker file effect deadline elapsed");return r.claim_agent_worker_file_effect(lease,call,id);});}
std::future<AgentWorkerFileClaimRecord> PersistenceService::record_agent_worker_file_result(AgentWorkerLeaseSpec lease,AgentFileToolInvocation call,bool success,std::string output){return impl_->submit([lease=std::move(lease),call=std::move(call),success,output=std::move(output)](Repository& r){return r.record_agent_worker_file_result(lease,call,success,output);});}
std::future<AgentWorkerFileClaimRecord> PersistenceService::agent_worker_file_claim(std::string root,std::string attempt,std::string call){return impl_->observe([root=std::move(root),attempt=std::move(attempt),call=std::move(call)](Repository& r){return r.agent_worker_file_claim(root,attempt,call);});}
std::future<AgentWorkerToolTurnRecord> PersistenceService::commit_agent_worker_read_turn(AgentWorkerLeaseSpec lease,std::string attempt,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){return impl_->submit([lease=std::move(lease),attempt=std::move(attempt),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker read turn cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker read turn deadline elapsed");return r.commit_agent_worker_read_turn(lease,attempt);});}
std::future<AgentWorkerToolTurnRecord> PersistenceService::commit_agent_worker_workspace_turn(AgentWorkerLeaseSpec lease,std::string attempt,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){return impl_->submit([lease=std::move(lease),attempt=std::move(attempt),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker workspace turn cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker workspace turn deadline elapsed");return r.commit_agent_worker_workspace_turn(lease,attempt);});}
std::future<Event> PersistenceService::agent_worker_tool_event(AgentWorkerLeaseSpec lease,std::string attempt,std::int64_t ordinal,bool returned,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){return impl_->submit([lease=std::move(lease),attempt=std::move(attempt),ordinal,returned,deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker tool activity cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker tool activity deadline elapsed");return r.agent_worker_tool_event(lease,attempt,ordinal,returned);});}
std::future<ModelCallReservation> PersistenceService::finish_agent_worker_model(AgentWorkerLeaseSpec lease,std::string attempt,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){
    return impl_->submit([lease=std::move(lease),attempt=std::move(attempt),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker finish cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker finish deadline elapsed");return r.finish_model_call(lease.root_run_id,lease.owner_run_id,attempt,{},&lease);});
}
std::future<Run> PersistenceService::complete_agent_worker_run(AgentWorkerLeaseSpec lease,std::string attempt,std::chrono::steady_clock::time_point deadline,std::stop_token owner,std::stop_token caller){
    return impl_->submit([lease=std::move(lease),attempt=std::move(attempt),deadline,owner,caller](Repository& r){if(owner.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker completion cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerAuthorityExpired("Worker completion deadline elapsed");return r.complete_run(lease.owner_run_id,"",&lease,attempt);});
}

std::future<ModelCallReservation> PersistenceService::reserve_model_call(std::string root,std::string owner,std::string attempt,ModelCallRole role){return impl_->submit([root=std::move(root),owner=std::move(owner),attempt=std::move(attempt),role](Repository& repository){return repository.reserve_model_call(root,owner,attempt,role);});}
std::future<ModelCallReservation> PersistenceService::start_model_call(std::string root,std::string owner,std::string attempt){return impl_->submit([root=std::move(root),owner=std::move(owner),attempt=std::move(attempt)](Repository& repository){return repository.start_model_call(root,owner,attempt);});}
std::future<ModelCallReservation> PersistenceService::finish_model_call(std::string root,std::string owner,std::string attempt,std::optional<std::string> actual_assistant_json){return impl_->submit([root=std::move(root),owner=std::move(owner),attempt=std::move(attempt),actual_assistant_json=std::move(actual_assistant_json)](Repository& repository){return repository.finish_model_call(root,owner,attempt,std::move(actual_assistant_json));});}
std::future<ContextSnapshot> PersistenceService::context_snapshot(ContextScope scope,ContextBinding binding,ContextReadBound bound){return impl_->submit([scope=std::move(scope),binding=std::move(binding),bound](Repository& r){return r.context_snapshot(scope,binding,bound);});}
std::future<ContextStatusObservation> PersistenceService::context_status_observation(ContextScope scope,ContextBinding binding){return impl_->observe([scope=std::move(scope),binding=std::move(binding)](Repository& r){return r.context_status_observation(scope,binding);});}
std::future<std::vector<ContextGroupPayload>> PersistenceService::context_group_payloads(ContextSnapshot snapshot,std::vector<std::int64_t> ordinals,ContextReadBound bound){return impl_->submit([snapshot=std::move(snapshot),ordinals=std::move(ordinals),bound](Repository& r){return r.context_group_payloads(snapshot,ordinals,bound);});}
std::future<std::vector<ContextHistoryPayload>> PersistenceService::context_history_payloads(ContextSnapshot snapshot,std::vector<std::int64_t> ordinals,ContextReadBound bound){return impl_->submit([snapshot=std::move(snapshot),ordinals=std::move(ordinals),bound](Repository& r){return r.context_history_payloads(snapshot,ordinals,bound);});}
std::future<void> PersistenceService::record_context_history_replay(ContextHistoryReplaySpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){r.record_context_history_replay(spec);});}
std::future<ProviderHistorySnapshot> PersistenceService::provider_history(ProviderHistoryOwner owner,ProviderHistorySelection selection){return impl_->submit([owner=std::move(owner),selection=std::move(selection)](Repository& r){return r.provider_history(owner,selection);});}
std::future<void> PersistenceService::record_provider_history_replay(ProviderHistoryReplaySpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){r.record_provider_history_replay(spec);});}
std::future<std::optional<ContextProjection>> PersistenceService::context_projection(ContextScope scope,ContextBinding binding,ContextReadBound bound){return impl_->submit([scope=std::move(scope),binding=std::move(binding),bound](Repository& r){return r.context_projection(scope,binding,bound);});}
std::future<ContextStepReservation> PersistenceService::begin_context_compaction(ContextCompactionSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.begin_context_compaction(spec);});}
std::future<ContextStepReservation> PersistenceService::start_context_compaction(std::string id,std::string attempt){return impl_->submit([id=std::move(id),attempt=std::move(attempt)](Repository& r){return r.start_context_compaction(id,attempt);});}
std::future<ContextProjection> PersistenceService::record_context_compaction_response(ContextCompactionCommit result){return impl_->submit([result=std::move(result)](Repository& r){return r.record_context_compaction_response(result);});}
std::future<ContextProjection> PersistenceService::commit_context_compaction(ContextCompactionCommit result){return impl_->submit([result=std::move(result)](Repository& r){return r.commit_context_compaction(result);});}
std::future<void> PersistenceService::retire_context_compaction(ContextCompactionFailure failure){return impl_->submit([failure=std::move(failure)](Repository& r){r.retire_context_compaction(failure);});}
std::future<ContextManualRequest> PersistenceService::request_context_compaction(ContextManualRequestSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.request_context_compaction(spec);});}
std::future<ContextManualRequest> PersistenceService::context_manual_request(std::string id,ContextBinding binding){return impl_->observe([id=std::move(id),binding=std::move(binding)](Repository& r){return r.context_manual_request(id,binding);});}
std::future<std::optional<ContextManualRequest>> PersistenceService::context_current_manual_request(ContextScope scope,ContextBinding binding){return impl_->observe([scope=std::move(scope),binding=std::move(binding)](Repository& r){return r.context_current_manual_request(scope,binding);});}
std::future<ContextManualRequest> PersistenceService::retire_context_request(std::string id,ContextBinding binding,ContextFailureCode code){return impl_->submit([id=std::move(id),binding=std::move(binding),code](Repository& r){return r.retire_context_request(id,binding,code);});}
std::future<IdleContextOwnerRecord> PersistenceService::claim_idle_context_owner(IdleContextOwnerSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.claim_idle_context_owner(spec);});}
std::future<IdleContextOwnerRecord> PersistenceService::idle_context_owner(std::string id){return impl_->observe([id=std::move(id)](Repository& r){return r.idle_context_owner(id);});}
std::future<void> PersistenceService::retire_idle_context_owner(std::string id,ContextFailureCode code,std::int64_t elapsed){return impl_->submit([id=std::move(id),code,elapsed](Repository& r){r.retire_idle_context_owner(id,code,elapsed);});}
std::future<ContextMeasureReservation> PersistenceService::begin_context_measure(ContextMeasureRequestSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.begin_context_measure(spec);});}
std::future<ContextMeasureReservation> PersistenceService::start_context_measure(std::string id){return impl_->submit([id=std::move(id)](Repository& r){return r.start_context_measure(id);});}
std::future<void> PersistenceService::finish_context_measure(ContextInputMeasure measure){return impl_->submit([measure=std::move(measure)](Repository& r){r.finish_context_measure(measure);});}
std::future<void> PersistenceService::retire_context_measure(std::string id,ContextFailureCode code,std::int64_t elapsed){return impl_->submit([id=std::move(id),code,elapsed](Repository& r){r.retire_context_measure(id,code,elapsed);});}
std::future<InferenceStepRecord> PersistenceService::reserve_inference_step(InferenceStepSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.reserve_inference_step(spec);});}
std::future<InferenceStepRecord> PersistenceService::inference_step(std::string id){return impl_->observe([id=std::move(id)](Repository& r){return r.inference_step(id);});}
std::future<InferenceStepRecord> PersistenceService::finish_inference_attempt(InferenceAttemptResult result){return impl_->submit([result=std::move(result)](Repository& r){return r.finish_inference_attempt(result);});}
std::future<InferenceStepRecord> PersistenceService::reserve_inference_rebuild(InferenceRebuildSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.reserve_inference_rebuild(spec);});}
std::future<DelegationBatchRecord> PersistenceService::accept_delegation_batch(DelegationBatchSpec spec){return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.accept_delegation_batch(spec);});}
std::future<DelegationBatchRecord> PersistenceService::delegation_batch(std::string id){return impl_->observe([id=std::move(id)](Repository& repository){return repository.delegation_batch(id);});}
std::future<std::vector<DelegationBatchRecord>> PersistenceService::delegation_batches(std::string parent){return impl_->observe([parent=std::move(parent)](Repository& repository){return repository.delegation_batches(parent);});}
std::future<DelegationTaskRecord> PersistenceService::settle_delegation_child(std::string child){return impl_->submit([child=std::move(child)](Repository& repository){return repository.settle_delegation_child(child);});}
std::future<DelegationBatchRecord> PersistenceService::settle_delegation_batch(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.settle_delegation_batch(id);});}
std::future<ChildAdmissionRecord> PersistenceService::child_admission(std::string child){return impl_->observe([child=std::move(child)](Repository& repository){return repository.child_admission(child);});}
std::future<std::vector<OwnedChildRecord>> PersistenceService::owned_children(std::string parent){return impl_->observe([parent=std::move(parent)](Repository& repository){return repository.owned_children(parent);});}
std::future<std::vector<Message>> PersistenceService::owned_child_history(std::string parent,std::string child){return impl_->observe([parent=std::move(parent),child=std::move(child)](Repository& repository){return repository.owned_child_history(parent,child);});}
std::future<std::vector<Event>> PersistenceService::tree_events(std::string parent,std::int64_t after,std::size_t count){return impl_->observe([parent=std::move(parent),after,count](Repository& repository){return repository.tree_events(parent,after,count);});}
std::future<void> PersistenceService::append_user_message(std::string id,std::string json) {
    return impl_->submit([id=std::move(id),json=std::move(json)](Repository& repository){repository.append_user_message(id,json);});
}
std::future<Run> PersistenceService::start_graph_run(std::string id,std::string session,std::string graph,std::int64_t revision,GraphPlan plan,std::string prompt,std::optional<RootBudgetSpec> budget,std::optional<GraphContextSpec> context,std::optional<WorkspaceMembershipSnapshot> membership){return impl_->submit([id=std::move(id),session=std::move(session),graph=std::move(graph),revision,plan=std::move(plan),prompt=std::move(prompt),budget=std::move(budget),context=std::move(context),membership=std::move(membership)](Repository& repository){return repository.start_graph_run(id,session,graph,revision,plan,prompt,budget,context,membership);});}
std::future<GraphContextOwnerRecord> PersistenceService::graph_context_owner(std::string root){return impl_->observe([root=std::move(root)](Repository& r){return r.graph_context_owner(root);});}
std::future<GraphContextOpenRecord> PersistenceService::open_graph_context_owner(GraphContextOpenSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.open_graph_context_owner(spec);});}
std::future<GraphContextOpenRecord> PersistenceService::resume_graph_context_owner(GraphContextOpenSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.resume_graph_context_owner(spec);});}
std::future<GraphRootRecord> PersistenceService::suspend_graph_context_owner(GraphContextBoundarySpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.suspend_graph_context_owner(spec);});}
std::future<Run> PersistenceService::complete_graph_context_owner(GraphContextBoundarySpec spec,std::string join){return impl_->submit([spec=std::move(spec),join=std::move(join)](Repository& r){return r.complete_graph_context_owner(spec,join);});}
std::future<Run> PersistenceService::retire_graph_context_owner(std::string root,RunState terminal,std::string reason,std::string segment,std::int64_t elapsed){return impl_->submit([root=std::move(root),terminal,reason=std::move(reason),segment=std::move(segment),elapsed](Repository& r){return r.retire_graph_context_owner(root,terminal,reason,segment,elapsed);});}
std::future<GraphRootRecord> PersistenceService::graph_run(std::string id){return impl_->observe([id=std::move(id)](Repository& repository){return repository.graph_run(id);});}
std::future<Run> PersistenceService::start_graph_child(std::string id,std::string parent,std::string node,std::string prompt,std::int64_t expected){return impl_->submit([id=std::move(id),parent=std::move(parent),node=std::move(node),prompt=std::move(prompt),expected](Repository& repository){return repository.start_graph_child(id,parent,node,prompt,expected);});}
std::future<GraphRootRecord> PersistenceService::start_graph_invocation(std::string id,std::string parent,std::string node,std::int64_t expected){return impl_->submit([id=std::move(id),parent=std::move(parent),node=std::move(node),expected](Repository& repository){return repository.start_graph_invocation(id,parent,node,expected);});}
std::future<GraphRootRecord> PersistenceService::settle_graph_child(std::string id,std::int64_t expected){return impl_->submit([id=std::move(id),expected](Repository& repository){return repository.settle_graph_child(id,expected);});}
std::future<GraphRootRecord> PersistenceService::start_graph_human(std::string id,std::string node,std::int64_t expected){return impl_->submit([id=std::move(id),node=std::move(node),expected](Repository& repository){return repository.start_graph_human(id,node,expected);});}
std::future<GraphRootRecord> PersistenceService::input_graph_tree_human(std::string root,std::string owner,std::string node,std::string input,std::string actor,std::int64_t root_revision,std::int64_t owner_revision){return impl_->submit([root=std::move(root),owner=std::move(owner),node=std::move(node),input=std::move(input),actor=std::move(actor),root_revision,owner_revision](Repository& r){return r.input_graph_tree_human(root,owner,node,input,actor,root_revision,owner_revision);});}
std::future<GraphRootRecord> PersistenceService::input_graph_human(std::string id,std::string node,std::string input,std::string actor,std::int64_t expected){return impl_->submit([id=std::move(id),node=std::move(node),input=std::move(input),actor=std::move(actor),expected](Repository& repository){return repository.input_graph_human(id,node,input,actor,expected);});}
std::future<GraphRootRecord> PersistenceService::skip_graph_node(std::string id,std::string node,std::int64_t expected){return impl_->submit([id=std::move(id),node=std::move(node),expected](Repository& repository){return repository.skip_graph_node(id,node,expected);});}
std::future<Run> PersistenceService::retire_graph_run(std::string id,RunState state,std::string reason){return impl_->submit([id=std::move(id),state,reason=std::move(reason)](Repository& repository){return repository.retire_graph_run(id,state,reason);});}
std::future<std::vector<Run>> PersistenceService::children(std::string id){return impl_->observe([id=std::move(id)](Repository& repository){return repository.children(id);});}
std::future<std::vector<Message>> PersistenceService::run_history(std::string id){return impl_->observe([id=std::move(id)](Repository& repository){return repository.run_history(id);});}
std::future<std::vector<Event>> PersistenceService::graph_events(std::string id,std::int64_t after){return impl_->observe([id=std::move(id),after](Repository& repository){return repository.graph_events(id,after);});}
std::future<SkillSelections> PersistenceService::initialize_run_skills(std::string id,std::string workspace){return impl_->submit([id=std::move(id),workspace=std::move(workspace)](Repository& repository){return repository.initialize_run_skills(id,workspace);});}
std::future<SkillSelections> PersistenceService::run_skills(std::string id){return impl_->observe([id=std::move(id)](Repository& repository){return repository.run_skills(id);});}
std::future<SessionSkillState> PersistenceService::session_skills(std::string session,std::string workspace){return impl_->observe([session=std::move(session),workspace=std::move(workspace)](Repository& repository){return repository.session_skills(session,workspace);});}
std::future<SessionSkillState> PersistenceService::replace_session_skills(std::string session,SkillSelections selections,std::int64_t revision){return impl_->submit([session=std::move(session),selections=std::move(selections),revision](Repository& repository){return repository.replace_session_skills(session,selections,revision);});}
std::future<SessionAgentState> PersistenceService::session_agent(std::string session){return impl_->observe([session=std::move(session)](Repository& repository){return repository.session_agent(session);});}
std::future<SessionAgentState> PersistenceService::replace_session_agent(std::string session,std::optional<AgentDefinition> selected,std::int64_t revision){return impl_->submit([session=std::move(session),selected=std::move(selected),revision](Repository& repository){return repository.replace_session_agent(session,selected,revision);});}
std::future<std::vector<SessionAgentEvent>> PersistenceService::session_agent_events(std::string session,std::int64_t after,std::size_t count){return impl_->observe([session=std::move(session),after,count](Repository& repository){return repository.session_agent_events(session,after,count);});}
std::future<SessionModelState> PersistenceService::session_model(std::string session){return impl_->observe([session=std::move(session)](Repository& repository){return repository.session_model(session);});}
std::future<SessionModelState> PersistenceService::replace_session_model(std::string session,std::optional<SessionModelSelection> selected,std::int64_t revision){return impl_->submit([session=std::move(session),selected=std::move(selected),revision](Repository& repository){return repository.replace_session_model(session,selected,revision);});}
std::future<void> PersistenceService::record_tool_turn(std::string id,std::string assistant,std::vector<std::string> tools,std::optional<SkillSelections> skills) {
    return impl_->submit([id=std::move(id),assistant=std::move(assistant),tools=std::move(tools),skills=std::move(skills)](Repository& repository){repository.record_tool_turn(id,assistant,tools,skills);});
}
std::future<Run> PersistenceService::complete_run(std::string id,std::string assistant) {
    return impl_->submit([id=std::move(id),assistant=std::move(assistant)](Repository& repository){return repository.complete_run(id,assistant);});
}
std::future<std::vector<Run>> PersistenceService::runs(std::string session_id) {
    return impl_->observe([session_id=std::move(session_id)](Repository& repository){return repository.runs(session_id);});
}
std::future<Run> PersistenceService::transition(std::string id,RunState expected,RunState next,std::string json) {
    return impl_->submit([id=std::move(id),expected,next,json=std::move(json)](Repository& repository){return repository.transition(id,expected,next,json);});
}
std::future<Event> PersistenceService::append_event(std::string id,std::string kind,std::string json) {
    return impl_->submit([id=std::move(id),kind=std::move(kind),json=std::move(json)](Repository& repository){return repository.append_event(id,kind,json);});
}
std::future<bool> PersistenceService::append_a2a_task_event(std::string run_id,std::string operation_id,std::string json) {
    return impl_->submit([run_id=std::move(run_id),operation_id=std::move(operation_id),json=std::move(json)](Repository& repository){return repository.append_a2a_task_event(run_id,operation_id,json);});
}
std::future<std::vector<Event>> PersistenceService::events(std::string id,std::int64_t after) {
    return impl_->observe([id=std::move(id),after](Repository& repository){return repository.events(id,after);});
}
std::future<std::optional<std::string>> PersistenceService::latest_a2a_event_id(std::string run_id,std::string operation_id) {
    return impl_->observe([run_id=std::move(run_id),operation_id=std::move(operation_id)](Repository& repository){return repository.latest_a2a_event_id(run_id,operation_id);});
}
std::future<ProcessOutputArtifact> PersistenceService::begin_process_output(std::string operation_id,std::string run_id){return impl_->submit([operation_id=std::move(operation_id),run_id=std::move(run_id)](Repository& repository){return repository.begin_process_output(operation_id,run_id);});}
std::future<ProcessOutputArtifact> PersistenceService::append_process_output(std::string operation_id,std::string run_id,bool stderr_channel,std::string bytes){return impl_->submit([operation_id=std::move(operation_id),run_id=std::move(run_id),stderr_channel,bytes=std::move(bytes)](Repository& repository){return repository.append_process_output(operation_id,run_id,stderr_channel,bytes);});}
std::future<ProcessOutputArtifact> PersistenceService::finish_process_output(std::string operation_id,std::string run_id,bool complete){return impl_->submit([operation_id=std::move(operation_id),run_id=std::move(run_id),complete](Repository& repository){return repository.finish_process_output(operation_id,run_id,complete);});}
std::future<ProcessOutputPage> PersistenceService::read_process_output(std::string operation_id,std::string channel,std::uint64_t offset,std::size_t limit){return impl_->observe([operation_id=std::move(operation_id),channel=std::move(channel),offset,limit](Repository& repository){return repository.read_process_output(operation_id,channel,offset,limit);});}
std::future<void> PersistenceService::append_message(std::string id,std::string role,std::string json) {
    return impl_->submit([id=std::move(id),role=std::move(role),json=std::move(json)](Repository& repository){repository.append_message(id,role,json);});
}
std::future<SessionInboxItem> PersistenceService::enqueue_session_input(std::string id,std::string session_id,std::string content,std::string files_json,std::string workspace_id,std::int64_t workspace_revision) {
    return impl_->submit([id=std::move(id),session_id=std::move(session_id),content=std::move(content),files_json=std::move(files_json),workspace_id=std::move(workspace_id),workspace_revision](Repository& repository){return repository.enqueue_session_input(id,session_id,content,files_json,workspace_id,workspace_revision);});
}
std::future<std::vector<SessionInboxItem>> PersistenceService::session_inbox(std::string session_id) {
    return impl_->submit([session_id=std::move(session_id)](Repository& repository){return repository.session_inbox(session_id);});
}
std::future<void> PersistenceService::cancel_session_input(std::string session_id,std::string input_id) {
    return impl_->submit([session_id=std::move(session_id),input_id=std::move(input_id)](Repository& repository){repository.cancel_session_input(session_id,input_id);});
}
std::future<std::vector<Message>> PersistenceService::history(std::string id) {
    return impl_->observe([id=std::move(id)](Repository& repository){return repository.history(id);});
}
std::future<MessagePage> PersistenceService::history_page(std::string id,std::optional<std::int64_t> before,std::size_t count) {
    return impl_->observe([id=std::move(id),before,count](Repository& repository){return repository.history_page(id,before,count);});
}
std::future<Message> PersistenceService::message(std::string id,std::int64_t sequence) {
    return impl_->observe([id=std::move(id),sequence](Repository& repository){return repository.message(id,sequence);});
}
std::future<void> PersistenceService::put_information(std::string category,std::string id,std::string json) {
    return impl_->submit([category=std::move(category),id=std::move(id),json=std::move(json)](Repository& repository){repository.put_information(category,id,json);});
}
std::future<void> PersistenceService::put_process_profiles_for_replacement(std::string json) {
    if(!impl_->replacement_bootstrap)throw Conflict("Process profiles may bypass quiescence only during a protected replacement startup");
    return impl_->submit([this,json=std::move(json)](Repository& repository){
        const auto owner=repository.backend_owner();
        if(owner.generation!=impl_->generation||!owner.quiesced)throw Conflict("Process-profile replacement import requires the protected quiesced owner");
        repository.put_information("native-process","profiles",json);
    },false);
}
std::future<void> PersistenceService::put_mcp_servers_for_replacement(std::string json) {
    if(!impl_->replacement_bootstrap)throw Conflict("MCP servers may bypass quiescence only during a protected replacement startup");
    return impl_->submit([this,json=std::move(json)](Repository& repository){
        const auto owner=repository.backend_owner();
        if(owner.generation!=impl_->generation||!owner.quiesced)throw Conflict("MCP replacement import requires the protected quiesced owner");
        repository.put_information("native-mcp","servers",json);
    },false);
}
std::future<void> PersistenceService::put_language_server_profiles_for_replacement(std::string json) {
    if(!impl_->replacement_bootstrap)throw Conflict("Language-server profiles may bypass quiescence only during a protected replacement startup");
    return impl_->submit([this,json=std::move(json)](Repository& repository){
        const auto owner=repository.backend_owner();
        if(owner.generation!=impl_->generation||!owner.quiesced)throw Conflict("Language-server replacement import requires the protected quiesced owner");
        repository.put_information("native-language-servers","profiles",json);
    },false);
}
std::future<CredentialMetadata> PersistenceService::put_language_server_credential_for_replacement(
    std::string id,std::string purpose,std::string label,SecretBytes secret) {
    const auto prefix=std::string("language-server.environment.");
    if(!language_server_credential_id(id)||!purpose.starts_with(prefix)||
       purpose.substr(prefix.size())!=id.substr(std::string("language-server-env-").size(),id.rfind("-r")-std::string("language-server-env-").size())||
       label!="Native language-server environment")throw Conflict("Invalid protected language-server credential update");
    if(!impl_->replacement_bootstrap)throw Conflict("Language-server credentials may bypass quiescence only during a protected replacement startup");
    return impl_->submit([this,id=std::move(id),purpose=std::move(purpose),label=std::move(label),secret=std::move(secret)](Repository& repository) mutable {
        const auto owner=repository.backend_owner();
        if(owner.generation!=impl_->generation||!owner.quiesced)throw Conflict("Language-server credential update requires the protected quiesced owner");
        return repository.put_credential("server",id,purpose,label,secret,0);
    },false);
}
std::future<void> PersistenceService::delete_language_server_credential_for_replacement(std::string id,std::int64_t revision) {
    if(!language_server_credential_id(id)||revision<1)throw Conflict("Invalid protected language-server credential removal");
    if(!impl_->replacement_bootstrap)throw Conflict("Language-server credentials may bypass quiescence only during a protected replacement startup");
    return impl_->submit([this,id=std::move(id),revision](Repository& repository){
        const auto owner=repository.backend_owner();
        if(owner.generation!=impl_->generation||!owner.quiesced)throw Conflict("Language-server credential removal requires the protected quiesced owner");
        repository.delete_credential("server",id,revision);
    },false);
}
std::future<std::string> PersistenceService::information(std::string category,std::string id) {
    return impl_->observe([category=std::move(category),id=std::move(id)](Repository& repository){return repository.information(category,id);});
}
std::future<void> PersistenceService::compare_information(std::string category,std::string id,std::string json,std::optional<std::string> expected) {
    return impl_->submit([category=std::move(category),id=std::move(id),json=std::move(json),expected=std::move(expected)](Repository& repository){repository.compare_information(category,id,json,expected);});
}
std::future<CredentialMetadata> PersistenceService::put_credential(std::string scope,std::string id,
    std::string purpose,std::string label,SecretBytes secret,std::int64_t revision) {
    return impl_->submit([scope=std::move(scope),id=std::move(id),purpose=std::move(purpose),label=std::move(label),secret=std::move(secret),revision](Repository& repository){
        return repository.put_credential(scope,id,purpose,label,secret,revision);
    });
}
std::future<Operation> PersistenceService::request_operation(std::string id,OperationSpec spec,std::int64_t expiry) {
    return impl_->submit([id=std::move(id),spec=std::move(spec),expiry](Repository& repository){return repository.request_operation(id,spec,expiry);});
}
std::future<Operation> PersistenceService::operation(std::string id) {
    return impl_->observe([id=std::move(id)](Repository& repository){return repository.operation(id);});
}
std::future<std::vector<Operation>> PersistenceService::operations(std::string id) {
    return impl_->observe([id=std::move(id)](Repository& repository){return repository.operations(id);});
}
std::future<std::vector<Operation>> PersistenceService::pending_operations_for_session(std::string id) {
    return impl_->observe([id=std::move(id)](Repository& repository){return repository.pending_operations_for_session(id);});
}
std::future<Operation> PersistenceService::decide_operation(std::string id,OperationDecision decision,std::string actor) {
    return impl_->submit([id=std::move(id),decision,actor=std::move(actor)](Repository& repository){return repository.decide_operation(id,decision,actor);});
}
std::future<Operation> PersistenceService::claim_operation(std::string id,OperationSpec spec) {
    return impl_->submit([id=std::move(id),spec=std::move(spec)](Repository& repository){return repository.claim_operation(id,spec);});
}
std::future<Operation> PersistenceService::finish_operation(std::string id,OperationState outcome,std::string result) {
    return impl_->submit([id=std::move(id),outcome,result=std::move(result)](Repository& repository){return repository.finish_operation(id,outcome,result);});
}
std::future<McpInputRequest> PersistenceService::publish_mcp_input_request(McpInputRequestSpec spec) {
    return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.publish_mcp_input_request(spec);});
}
std::future<McpInputRequest> PersistenceService::mcp_input_request(std::string id) {
    return impl_->observe([id=std::move(id)](Repository& repository){return repository.mcp_input_request(id);});
}
std::future<std::vector<McpInputRequest>> PersistenceService::mcp_input_requests(std::string run_id) {
    return impl_->observe([run_id=std::move(run_id)](Repository& repository){return repository.mcp_input_requests(run_id);});
}
std::future<McpInputRequest> PersistenceService::answer_mcp_input_request(std::string run_id,std::string id,std::string response,std::string actor) {
    return impl_->submit([run_id=std::move(run_id),id=std::move(id),response=std::move(response),actor=std::move(actor)](Repository& repository){return repository.answer_mcp_input_request(run_id,id,response,actor);});
}
std::future<McpInputRequest> PersistenceService::retire_mcp_input_request(std::string id,std::string reason) {
    return impl_->submit([id=std::move(id),reason=std::move(reason)](Repository& repository){return repository.retire_mcp_input_request(id,reason);});
}
std::future<Operation> PersistenceService::cancel_operation(std::string id,OperationSpec spec) {
    return impl_->submit([id=std::move(id),spec=std::move(spec)](Repository& repository){return repository.cancel_operation(id,spec);});
}
std::future<Operation> PersistenceService::expire_operation(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.expire_operation(id);});
}
std::future<Operation> PersistenceService::reconcile_interrupted_process(std::string id,std::string actor,std::string note) {
    return impl_->submit([id=std::move(id),actor=std::move(actor),note=std::move(note)](Repository& repository){return repository.reconcile_interrupted_process(id,actor,note);});
}
std::future<std::vector<CredentialMetadata>> PersistenceService::credentials(std::string scope) {
    return impl_->observe([scope=std::move(scope)](Repository& repository){return repository.credentials(scope);});
}
std::future<SecretBytes> PersistenceService::resolve_credential(std::string scope,std::string id,std::string purpose) {
    return impl_->observe([scope=std::move(scope),id=std::move(id),purpose=std::move(purpose)](Repository& repository){return repository.resolve_credential(scope,id,purpose);});
}
std::future<void> PersistenceService::delete_credential(std::string scope,std::string id,std::int64_t revision) {
    return impl_->submit([scope=std::move(scope),id=std::move(id),revision](Repository& repository){repository.delete_credential(scope,id,revision);});
}
std::future<ResolvedCredential> PersistenceService::resolve_credential_snapshot(std::string scope,std::string id,std::string purpose) {
    return impl_->observe([scope=std::move(scope),id=std::move(id),purpose=std::move(purpose)](Repository& repository){return repository.resolve_credential_snapshot(scope,id,purpose);});
}
std::future<McpOAuthRefreshClaim> PersistenceService::claim_mcp_oauth_refresh(McpOAuthRefreshSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.claim_mcp_oauth_refresh(spec);});}
std::future<McpOAuthRefreshRecord> PersistenceService::mcp_oauth_refresh(std::string id){return impl_->observe([id=std::move(id)](Repository& r){return r.mcp_oauth_refresh(id);});}
std::future<McpOAuthRefreshRecord> PersistenceService::dispatch_mcp_oauth_refresh(std::string id,std::string generation){return impl_->submit([id=std::move(id),generation=std::move(generation)](Repository& r){return r.dispatch_mcp_oauth_refresh(id,generation);});}
std::future<McpOAuthRefreshRecord> PersistenceService::abandon_mcp_oauth_refresh(std::string id,std::string generation){return impl_->submit([id=std::move(id),generation=std::move(generation)](Repository& r){return r.abandon_mcp_oauth_refresh(id,generation);});}
std::future<McpOAuthRefreshRecord> PersistenceService::publish_mcp_oauth_refresh(std::string id,std::string generation,SecretBytes grant){return impl_->submit([id=std::move(id),generation=std::move(generation),grant=std::move(grant)](Repository& r){return r.publish_mcp_oauth_refresh(id,generation,grant);});}
}
