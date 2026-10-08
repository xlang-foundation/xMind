#include "agentflow/agent_service.hpp"
#include "agentflow/delegation_executor.hpp"
#include "agentflow/dynamic_plan_executor.hpp"
#include <algorithm>
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <thread>
#include "nlohmann/json.hpp"

namespace agentflow {
namespace {
using Json=nlohmann::json;
bool terminal(RunState state){return state==RunState::completed||state==RunState::failed||state==RunState::cancelled;}
void controller(const std::string& actor){
    if(actor.empty()||actor.size()>128||actor.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
        throw std::invalid_argument("Invalid authenticated plan controller");
}
AgentSettings ordinary_settings(AgentSettings settings){
    // Existing workspace/tool and turn authority admits this ordinary Agent
    // policy. Larger turn allowances remain unchanged rather than being clamped.
    if(!settings.planning&&settings.workspace&&settings.provider.tools==Capability::supported&&settings.max_turns<=16)
        settings.planning=DynamicPlanningPolicy{};
    return settings;
}
bool runnable(const DynamicPlanRecord& plan){const auto decision=inspect_dynamic_plan(plan);return !decision.halted&&(!decision.ready.empty()||decision.report_ready);}
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
}
struct AgentService::Impl {
    enum class Phase {queued,working,waiting,faulted};
    struct Job {std::string id,model;Phase phase=Phase::queued;std::stop_source stop;bool user_cancel=false;};
    struct Prepared {};
    PersistenceService& persistence;
    std::string provider_context;
    bool planning_enabled;
    std::shared_ptr<NativeChildExecutor> children;
    std::shared_ptr<DelegationExecutor> delegation;
    std::shared_ptr<DynamicPlanExecutor> planning;
    AgentRunner runner;
    mutable std::mutex mutex;
    std::mutex close_mutex;
    std::condition_variable changed;
    // The expiry waiter has a different predicate. It must not consume a
    // single work-queue notification intended for an Agent owner.
    std::condition_variable expiry_changed;
    // A working job may hold a reserved wake ticket, but only queued jobs can
    // dispatch. Human input cannot create a second concurrently running owner.
    std::list<std::shared_ptr<Job>> pending;
    std::map<std::string,std::shared_ptr<Job>> active;
    std::vector<std::thread> workers;
    std::thread expiry_worker;
    std::size_t limit,owned_limit;
    bool accepting=true,faulted=false;

    Impl(PersistenceService& store,AgentSettings settings,std::size_t count,std::size_t capacity)
        :Impl(store,ordinary_settings(std::move(settings)),count,capacity,Prepared{}){}
    Impl(PersistenceService& store,AgentSettings settings,std::size_t count,std::size_t capacity,Prepared)
        :persistence(store),provider_context(provider_context_json(settings)),planning_enabled(settings.planning.has_value()),
         children(settings.delegation||settings.planning?std::make_shared<NativeChildExecutor>():nullptr),
         delegation(settings.delegation?std::make_shared<DelegationExecutor>(store,children):nullptr),
         planning(settings.planning?std::make_shared<DynamicPlanExecutor>(store,children):nullptr),
         runner(store,std::move(settings),delegation,planning),limit(capacity),owned_limit(capacity+count){
        if(count==0||count>16||capacity==0||capacity>4096)throw std::invalid_argument("Invalid agent worker capacity");
        // Recovery already interrupts every non-clean owner. Adopt only ordinary
        // typed closed human pauses. Stale authority retains inspection/cancel.
        for(const auto& session:store.sessions().get())for(const auto& run:store.runs(session.id).get()){
            if(run.graph_root||!run.parent_id.empty()||run.state!=RunState::paused)continue;
            const auto plan=store.dynamic_plan_for_root(run.id).get();if(!plan)throw DatabaseError("Ordinary pause has no owned dynamic plan");
            if(active.size()>=limit)throw RunBusy("Paused agent ownership exceeds service capacity");
            verify_pause(run,*plan);
            auto job=std::make_shared<Job>();job->id=run.id;job->phase=Phase::waiting;
            try{job->model=runner.admitted_dynamic_model(run.id);runner.validate_dynamic_owner(run.id,job->model);}
            catch(const DynamicPlanUnavailable&){}
            active.emplace(run.id,job);
            // Even an already-answered clean pause waits for an authenticated
            // duplicate answer or explicit resume; startup never replays it.
        }
        try{for(std::size_t i=0;i<count;++i)workers.emplace_back([this]{work();});expiry_worker=std::thread([this]{expire_waiting();});}
        catch(...){close();throw;}
    }
    ~Impl(){close();}
    void require_available()const{
        if(!accepting||faulted||(children&&!children->healthy()))throw RunUnavailable("Agent executor is unavailable");
    }
    void verify_effects(const std::string& id){for(const auto& op:persistence.operations(id).get())
        if(op.state==OperationState::executing||op.state==OperationState::ready||op.state==OperationState::awaiting_approval||op.state==OperationState::uncertain)
            throw DatabaseError("Paused dynamic owner retains an unresolved effect");}
    void verify_pause(const Run& run,const DynamicPlanRecord& plan){
        const auto segment=persistence.dynamic_budget_segment(run.id).get();const auto decision=inspect_dynamic_plan(plan);
        if(plan.root_run_id!=run.id||plan.state!="waiting_human"||!plan.capabilities.catalogue_finalized||segment.state!="closed"||
           !segment.closed_event_seq||decision.halted||!decision.claimed.empty())throw DatabaseError("Paused dynamic owner is not clean");
        for(const auto& child:persistence.owned_children(run.id).get()){
            if(!terminal(child.run.state))throw DatabaseError("Paused dynamic owner has a live child");verify_effects(child.run.id);
        }
        verify_effects(run.id);
    }
    std::vector<std::shared_ptr<Job>> fault_locked(){
        faulted=true;accepting=false;std::vector<std::shared_ptr<Job>> jobs;jobs.reserve(active.size());
        for(const auto& [id,job]:active){(void)id;
            // A queued wake still owns a closed pause. Fault stops its token,
            // but removes its scheduler entry before a stopped Runner could
            // accidentally retire that durable pause as user cancellation.
            if(job->phase==Phase::queued&&!job->user_cancel)try{
                const auto current=persistence.run(job->id).get();
                if(current.state==RunState::paused){const auto plan=persistence.dynamic_plan_for_root(job->id).get();
                    if(!plan)throw DatabaseError("Queued pause lost its plan");verify_pause(current,*plan);remove_ticket(job);job->phase=Phase::waiting;}
            }catch(...){}
            jobs.push_back(job);
        }changed.notify_all();return jobs;
    }
    void stop_jobs(const std::vector<std::shared_ptr<Job>>& jobs){for(const auto& job:jobs)job->stop.request_stop();changed.notify_all();expiry_changed.notify_all();}
    void fail(){std::vector<std::shared_ptr<Job>> jobs;{std::lock_guard lock(mutex);jobs=fault_locked();}stop_jobs(jobs);if(children)children->fail();}
    auto ticket(const std::shared_ptr<Job>& job){return std::find(pending.begin(),pending.end(),job);}
    bool reserve_wake(const std::shared_ptr<Job>& job){
        if(ticket(job)!=pending.end())return false;if(pending.size()>=limit)throw RunBusy("Agent wake queue is full");
        pending.push_back(job);return true;
    }
    void remove_ticket(const std::shared_ptr<Job>& job){const auto found=ticket(job);if(found!=pending.end())pending.erase(found);}
    std::size_t waiting_count()const{std::size_t count=0;for(const auto& [id,job]:active){(void)id;if(job->phase==Phase::waiting)++count;}return count;}
    void require_admission()const{
        require_available();if(active.size()>=owned_limit||pending.size()+waiting_count()>=limit)throw RunBusy("Agent ownership queue is full");
    }
    void retire_waiting(const std::shared_ptr<Job>& job,RunState state,const std::string& reason){
        persistence.retire_dynamic_owner(job->id,state,reason).get();remove_ticket(job);active.erase(job->id);
    }
    void expire_waiting(){
        try{for(;;){
            std::unique_lock lock(mutex);expiry_changed.wait_for(lock,std::chrono::milliseconds(100),[&]{return !accepting;});if(!accepting)return;
            for(auto item=active.begin();item!=active.end();){const auto job=item->second;++item;if(job->phase!=Phase::waiting)continue;
                const auto plan=persistence.dynamic_plan_for_root(job->id).get();if(!plan)throw DatabaseError("Waiting owner lost its plan");
                bool expired=false;for(const auto& request:persistence.dynamic_human_requests(plan->id).get()){
                    if(request.state=="expired"){expired=true;break;}
                    if(request.state=="waiting"&&request.expires_unix_ms<=now_ms()){
                        if(persistence.expire_dynamic_human(plan->id,request.id).get().state=="expired"){expired=true;break;}
                    }
                }
                // Closed-segment retirement consumes no provider attempt and
                // never starts the held parent continuation.
                if(expired)retire_waiting(job,RunState::failed,R"({"reason":"human_input_expired"})");
            }
        }}catch(...){fail();}
    }
    void work(){
        for(;;){std::shared_ptr<Job> job;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock,[&]{return !accepting||std::any_of(pending.begin(),pending.end(),[](const auto& value){return value->phase==Phase::queued;});});
                const auto next=std::find_if(pending.begin(),pending.end(),[](const auto& value){return value->phase==Phase::queued;});
                if(next==pending.end())return;job=*next;pending.erase(next);job->phase=Phase::working;
            }
            if(children&&!children->healthy())fail();
            try{
                const auto observed=runner.execute(job->id,job->stop.get_token(),job->model);std::unique_lock lock(mutex);
                if(terminal(observed.state)){remove_ticket(job);active.erase(job->id);continue;}
                if(observed.state!=RunState::paused)throw DatabaseError("Agent worker returned without actual retirement or a human pause");
                // Re-read under the input owner lock: a final answer may have
                // committed between the runner pause and worker retirement.
                const auto current=persistence.run(job->id).get();
                if(terminal(current.state)){remove_ticket(job);active.erase(job->id);continue;}
                const auto plan=persistence.dynamic_plan_for_root(job->id).get();if(!plan||current.state!=RunState::paused)throw DatabaseError("Dynamic pause ownership changed unexpectedly");
                verify_pause(current,*plan);
                if(job->user_cancel)retire_waiting(job,RunState::cancelled,R"({"reason":"cancelled_during_human_wait"})");
                // Shutdown can observe Running just before Runner atomically
                // closes a pause. Preserve that now-clean owner even if the
                // shutdown stop request arrived after its commit. Explicit
                // controller cancellation has a separate owner-lock flag.
                else if(!accepting||job->stop.stop_requested()){remove_ticket(job);job->phase=Phase::waiting;}
                else if(runnable(*plan)){reserve_wake(job);job->phase=Phase::queued;changed.notify_one();}
                else{remove_ticket(job);job->phase=Phase::waiting;changed.notify_all();}
            }catch(const DynamicPlanUnavailable&){
                // Metadata may drift after an answer commits but before this
                // physical wake. A still-closed pause has not dispatched: keep
                // its owner inspectable/cancellable rather than faulting peers.
                bool retained=false;
                try{const auto current=persistence.run(job->id).get();const auto plan=persistence.dynamic_plan_for_root(job->id).get();
                    if(current.state==RunState::paused&&plan){std::lock_guard lock(mutex);verify_pause(current,*plan);
                        if(job->user_cancel)retire_waiting(job,RunState::cancelled,R"({"reason":"cancelled_during_human_wait"})");
                        else{remove_ticket(job);job->phase=Phase::waiting;}retained=true;}}
                catch(...){}
                if(!retained){{std::lock_guard lock(mutex);job->phase=Phase::faulted;remove_ticket(job);}fail();}
            }catch(const Conflict&){
                // A lost claim never authorizes modifying another owner's run.
                // A live local owner remains quarantined rather than erased.
                try{const auto current=persistence.run(job->id).get();if(terminal(current.state)){std::lock_guard lock(mutex);remove_ticket(job);active.erase(job->id);continue;}}catch(...){}
                {std::lock_guard lock(mutex);job->phase=Phase::faulted;remove_ticket(job);}fail();
            }catch(...){
                {std::lock_guard lock(mutex);job->phase=Phase::faulted;remove_ticket(job);}fail();
                // Actual children are drained by Runner/executor. Keep an
                // unrecorded or uncertain ledger for recovery; invent no result.
            }
        }
    }
    void close(){
        std::lock_guard serial(close_mutex);std::vector<std::shared_ptr<Job>> stopping;
        {
            std::lock_guard lock(mutex);accepting=false;
            for(const auto& [id,job]:active){(void)id;
                if(job->phase==Phase::working){
                    // Runner can already have committed a clean pause while
                    // its return/worker retirement still awaits this mutex.
                    // Shutdown retains that actual closed owner, not merely
                    // jobs whose in-memory phase has caught up to waiting.
                    try{const auto current=persistence.run(job->id).get();
                        if(current.state==RunState::paused){const auto plan=persistence.dynamic_plan_for_root(job->id).get();
                            if(!plan)throw DatabaseError("Paused shutdown owner lost its plan");verify_pause(current,*plan);}
                        else stopping.push_back(job);
                    }catch(...){faulted=true;stopping.push_back(job);}
                }
                else if(job->phase==Phase::queued){
                    try{const auto current=persistence.run(job->id).get();
                        if(current.state==RunState::paused){remove_ticket(job);job->phase=Phase::waiting;}
                        else stopping.push_back(job);
                    }catch(...){faulted=true;stopping.push_back(job);}
                }
            }
        }
        stop_jobs(stopping);changed.notify_all();
        for(auto& worker:workers)if(worker.joinable())worker.join();if(expiry_worker.joinable())expiry_worker.join();
        // Roots drain all actual/staged batches first. Both execution paths
        // share this pool's four workers and 64 pending reservations.
        if(children)children->close();
    }
};
AgentService::AgentService(PersistenceService& store,AgentSettings settings,std::size_t workers,std::size_t capacity)
    :impl_(std::make_unique<Impl>(store,std::move(settings),workers,capacity)){}
AgentService::~AgentService()=default;
Run AgentService::submit(std::string id,std::string session,std::string prompt){return submit_model(std::move(id),std::move(session),std::move(prompt),{});}
std::vector<std::string> AgentService::models()const{return impl_->runner.models();}
bool AgentService::supports_delegation()const{std::lock_guard lock(impl_->mutex);return impl_->accepting&&!impl_->faulted&&impl_->delegation&&impl_->children->healthy();}
bool AgentService::supports_dynamic_planning()const{std::lock_guard lock(impl_->mutex);return impl_->accepting&&!impl_->faulted&&impl_->planning_enabled&&impl_->children->healthy();}
Run AgentService::submit_model(std::string id,std::string session,std::string prompt,std::string model){
    if(!model.empty()){const auto configured=models();if(std::find(configured.begin(),configured.end(),model)==configured.end())throw std::invalid_argument("Model is not configured on this backend");}
    auto job=std::make_shared<Impl::Job>();job->id=id;job->model=std::move(model);
    std::unique_lock lock(impl_->mutex);impl_->require_admission();if(!impl_->active.emplace(id,job).second)throw Conflict("Run already exists");
    try{impl_->pending.push_back(job);}catch(...){impl_->active.erase(id);throw;}
    Run result;try{result=impl_->runner.start(id,std::move(session),std::move(prompt),job->model);}
    catch(...){impl_->remove_ticket(job);impl_->active.erase(id);throw;}
    lock.unlock();impl_->changed.notify_one();return result;
}
Run AgentService::submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity){
    if(content.empty()||content.size()>65536||content.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid incoming message content");
    std::unique_lock lock(impl_->mutex);if(const auto replay=impl_->persistence.incoming_message(message,context,identity,content).get())return *replay;
    impl_->require_admission();auto job=std::make_shared<Impl::Job>();job->id=id;if(!impl_->active.emplace(id,job).second)throw Conflict("Run already exists");
    try{impl_->pending.push_back(job);}catch(...){impl_->active.erase(id);throw;}
    auto prompt=Json{{"content",content},{"a2a_message_id",message}};if(!impl_->provider_context.empty())prompt["provider_context"]=Json::parse(impl_->provider_context);
    Run result;try{result=impl_->persistence.start_incoming_message(id,context,message,prompt.dump(),identity,impl_->runner.execution_budget(),impl_->runner.execution_capabilities()).get();}
    catch(...){impl_->remove_ticket(job);impl_->active.erase(id);throw;}
    if(result.id!=id){impl_->remove_ticket(job);impl_->active.erase(id);return result;}
    lock.unlock();impl_->changed.notify_one();return result;
}
Run AgentService::plan_input(const std::string& root,const std::string& request,std::string input,const std::string& actor,std::int64_t revision,std::int64_t sequence){
    controller(actor);std::unique_lock lock(impl_->mutex);impl_->require_available();
    const auto owned=impl_->active.find(root);if(owned==impl_->active.end())throw NotFound("Dynamic root is not owned by this service");const auto job=owned->second;
    if(job->phase==Impl::Phase::faulted||job->user_cancel||job->stop.stop_requested())throw Conflict("Dynamic cancellation or recovery is pending");
    impl_->runner.validate_dynamic_owner(root,job->model);const auto plan=impl_->persistence.dynamic_plan_for_root(root).get();if(!plan)throw NotFound("Owned root has no dynamic plan");
    const bool reserved=impl_->reserve_wake(job);
    DynamicHumanInputSpec spec;spec.plan_id=plan->id;spec.request_id=request;spec.input_json=std::move(input);spec.authenticated_actor=actor;
    spec.backend_identity=plan->capabilities.backend_identity;spec.expected_revision=revision;spec.expected_state_sequence=sequence;
    try{impl_->persistence.input_dynamic_human(std::move(spec)).get();}catch(...){if(reserved)impl_->remove_ticket(job);throw;}
    try{const auto current=impl_->persistence.dynamic_plan(plan->id).get();
        if(job->phase==Impl::Phase::waiting){if(runnable(current))job->phase=Impl::Phase::queued;else impl_->remove_ticket(job);}
        // A working owner keeps its preallocated wake until pause re-read. A
        // queued owner already has exactly one scheduler entry.
        const auto result=impl_->persistence.run(root).get();lock.unlock();impl_->changed.notify_all();return result;
    }catch(...){
        // Input already committed. An unavailable scheduling observation cannot
        // be reported as a rejected answer or authorize an unobserved wake.
        const auto stopping=impl_->fault_locked();lock.unlock();impl_->stop_jobs(stopping);if(impl_->children)impl_->children->fail();
        throw DynamicOutcomeUnrecorded("Committed human answer could not be observed by its native owner");
    }
}
Run AgentService::resume_plan(const std::string& root,const std::string& actor,std::int64_t revision,std::int64_t sequence){
    controller(actor);std::unique_lock lock(impl_->mutex);impl_->require_available();
    const auto owned=impl_->active.find(root);if(owned==impl_->active.end())throw NotFound("Dynamic root is not owned by this service");const auto job=owned->second;
    if(job->phase!=Impl::Phase::waiting||job->user_cancel||job->stop.stop_requested())throw Conflict("Dynamic owner is not waiting for resume");
    impl_->runner.validate_dynamic_owner(root,job->model);const auto current=impl_->persistence.run(root).get();const auto plan=impl_->persistence.dynamic_plan_for_root(root).get();
    if(!plan||current.state!=RunState::paused)throw Conflict("Dynamic owner is not durably paused");
    if(plan->revision!=revision||plan->state_sequence!=sequence)throw DynamicPlanChanged("Dynamic plan observation changed");
    impl_->verify_pause(current,*plan);if(!runnable(*plan))throw Conflict("Dynamic owner has no ready frontier or report");
    impl_->reserve_wake(job);job->phase=Impl::Phase::queued;lock.unlock();impl_->changed.notify_one();return current;
}
void AgentService::cancel(const std::string& id){
    std::shared_ptr<Impl::Job> job;
    {
        std::lock_guard lock(impl_->mutex);if(impl_->faulted)throw RunUnavailable("Agent executor is unavailable");const auto found=impl_->active.find(id);
        if(found==impl_->active.end()){impl_->persistence.run(id).get();throw Conflict("Run is no longer owned by this executor");}job=found->second;
        if(job->phase==Impl::Phase::working)job->user_cancel=true;
        else{const auto current=impl_->persistence.run(id).get();
            if(current.state==RunState::paused)impl_->retire_waiting(job,RunState::cancelled,R"({"reason":"cancelled_during_human_wait"})");
            else{impl_->persistence.transition(id,RunState::queued,RunState::cancelled,R"({"reason":"cancelled_before_start"})").get();impl_->remove_ticket(job);impl_->active.erase(found);}
        }
    }
    // Synchronous stop callbacks may inspect this service.
    job->stop.request_stop();impl_->changed.notify_all();
}
bool AgentService::healthy()const{std::lock_guard lock(impl_->mutex);return impl_->accepting&&!impl_->faulted&&(!impl_->children||impl_->children->healthy());}
bool AgentService::idle()const{std::lock_guard lock(impl_->mutex);return impl_->accepting&&!impl_->faulted&&impl_->active.empty();}
void AgentService::close(){impl_->close();}
}
