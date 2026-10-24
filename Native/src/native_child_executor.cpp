#include "agentflow/native_child_executor.hpp"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>

namespace agentflow {
struct NativeChildSlotAccess {
    static std::shared_ptr<NativeChildSlot> acquire(std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel,std::shared_ptr<const void> identity){return std::shared_ptr<NativeChildSlot>(new NativeChildSlot(std::move(budget),cancel,std::move(identity)));}
    static void retire(const std::shared_ptr<NativeChildSlot>& slot){slot->retire();}
    static std::shared_ptr<RootExecutionBudget> begin(const std::shared_ptr<NativeChildSlot>& slot,const std::shared_ptr<const void>& identity){
        if(!slot)throw Conflict("Native assistance requires its exact yielded child slot");
        std::lock_guard lock(slot->mutex_);
        if(slot->pool_identity_!=identity||slot->state_!=NativeChildSlot::State::yielded||slot->assisting_)throw Conflict("Native assistance lost its yielded pool job");
        slot->assisting_=true;return slot->budget_;
    }
    static void end(const std::shared_ptr<NativeChildSlot>& slot){std::lock_guard lock(slot->mutex_);slot->assisting_=false;}
};
NativeChildSlot::NativeChildSlot(std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel,std::shared_ptr<const void> identity):budget_(std::move(budget)),cancel_(cancel),pool_identity_(std::move(identity)){if(!budget_)throw std::invalid_argument("Child slot requires its acquired root budget");}
bool NativeChildSlot::yield(){std::lock_guard lock(mutex_);if(state_!=State::held)return false;budget_->release_leaf();state_=State::yielded;return true;}
void NativeChildSlot::reacquire(std::stop_token cancel){
    for(;;){if(cancel_.stop_requested())throw TransportCancelled("Child slot owner cancelled");budget_->check(cancel);{std::lock_guard lock(mutex_);if(state_==State::retired)throw Conflict("Child slot lease retired");if(state_!=State::yielded||assisting_)throw Conflict("Child slot is not available for reacquisition");if(budget_->try_acquire_leaf()){state_=State::held;return;}}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
void NativeChildSlot::retire()noexcept{std::lock_guard lock(mutex_);if(state_==State::held)budget_->release_leaf();state_=State::retired;}
struct NativeChildExecutor::Impl {
    struct Job {
        enum class Phase {staged,dispatched,retired};
        std::shared_ptr<RootExecutionBudget> budget;
        std::function<Run(std::stop_token)> action;
        std::promise<Run> promise;
        std::shared_future<Run> result;
        std::stop_source stop;
        std::function<void(std::shared_ptr<NativeChildSlot>)> bind_slot;
        std::shared_ptr<NativeChildSlot> slot;
        Phase phase=Phase::staged;
        std::stop_callback<std::function<void()>> external;
        Job(NativeChildWork work,std::condition_variable& changed)
            :budget(std::move(work.budget)),action(std::move(work.action)),result(promise.get_future().share()),bind_slot(std::move(work.bind_slot)),
             external(work.cancel,[this,&changed]{stop.request_stop();changed.notify_all();}){}
    };
    mutable std::mutex mutex;
    std::mutex closing;
    std::condition_variable changed;
    std::list<std::shared_ptr<Job>> staged,pending,dispatched;
    std::vector<std::thread> workers;
    std::size_t capacity,reserved=0;
    const std::size_t worker_limit;
    const std::shared_ptr<const void> identity=std::make_shared<const char>(0);
    bool accepting=true,faulted=false;
    Impl(std::size_t count,std::size_t limit):capacity(limit),worker_limit(count){
        if(count<1||count>8||limit<4||limit>64)throw std::invalid_argument("Invalid native child executor capacity");
        try{for(std::size_t i=0;i<count;++i)workers.emplace_back([this]{work();});}catch(...){close();throw;}
    }
    ~Impl(){close();}
    struct StopSnapshot {
        // At most 72 total jobs, including parked planners and assisted work.
        std::array<std::shared_ptr<Job>,72> jobs;
        std::array<bool,72> retired{};
        std::size_t count=0;
    };
    StopSnapshot stop_locked(bool failure){
        if(failure)faulted=true;accepting=false;StopSnapshot result;
        for(auto& job:staged){job->phase=Job::Phase::retired;result.jobs[result.count]=job;result.retired[result.count++]=true;}
        staged.clear();reserved=0;
        for(auto& job:pending)result.jobs[result.count++]=job;
        for(auto& job:dispatched)result.jobs[result.count++]=job;
        return result;
    }
    void stop_observed(StopSnapshot snapshot){
        // stop callbacks execute synchronously. They may query/fault this pool,
        // so neither the pool mutex nor job-tracking lock may be held here.
        for(std::size_t i=0;i<snapshot.count;++i){snapshot.jobs[i]->stop.request_stop();
            if(snapshot.retired[i])snapshot.jobs[i]->promise.set_exception(std::make_exception_ptr(NativeChildOutcomeUnrecorded("Staged native child batch retired before dispatch")));}
        changed.notify_all();
    }
    void fail(){StopSnapshot snapshot;{std::lock_guard lock(mutex);snapshot=stop_locked(true);}stop_observed(std::move(snapshot));}
    std::shared_ptr<Job> take_locked(bool& child_slot,const std::shared_ptr<RootExecutionBudget>& budget={}){
        for(auto item=pending.begin();item!=pending.end();++item){
            if(budget&&(*item)->budget!=budget)continue;
            if(!(*item)->stop.stop_requested()&&std::chrono::steady_clock::now()<(*item)->budget->deadline()){
                if(!(*item)->budget->try_acquire_leaf())continue;child_slot=true;
            }
            auto job=*item;dispatched.splice(dispatched.end(),pending,item);return job;
        }
        return {};
    }
    void execute_job(const std::shared_ptr<Job>& job,bool child_slot){
        try{
            if(child_slot&&job->bind_slot){job->slot=NativeChildSlotAccess::acquire(job->budget,job->stop.get_token(),identity);child_slot=false;job->bind_slot(job->slot);}
            auto actual=job->action(job->stop.get_token());
            if(job->slot)NativeChildSlotAccess::retire(job->slot);
            if(child_slot){job->budget->release_leaf();child_slot=false;}
            job->promise.set_value(std::move(actual));
        }catch(...){
            const auto failure=std::current_exception();if(job->slot)NativeChildSlotAccess::retire(job->slot);if(child_slot){job->budget->release_leaf();child_slot=false;}
            fail();job->promise.set_exception(failure);
        }
        {std::lock_guard lock(mutex);dispatched.erase(std::find(dispatched.begin(),dispatched.end(),job));}changed.notify_all();
    }
    bool assist(const std::shared_ptr<NativeChildSlot>& slot){
        const auto budget=NativeChildSlotAccess::begin(slot,identity);
        const auto end=[&](void*){NativeChildSlotAccess::end(slot);changed.notify_all();};
        std::unique_ptr<void,decltype(end)> guard(this,end);
        bool child_slot=false;std::shared_ptr<Job> job;
        {std::lock_guard lock(mutex);job=take_locked(child_slot,budget);}
        if(!job)return false;
        execute_job(job,child_slot);return true;
    }
    void work(){
        for(;;){
            std::shared_ptr<Job> job;bool child_slot=false;
            {std::unique_lock lock(mutex);for(;;){
                job=take_locked(child_slot);if(job)break;
                if(!accepting&&pending.empty()&&reserved==0)return;
                changed.wait_for(lock,std::chrono::milliseconds(10));
            }}
            execute_job(job,child_slot);
        }
    }
    void close(){
        std::lock_guard serial(closing);
        StopSnapshot snapshot;{std::lock_guard lock(mutex);snapshot=stop_locked(false);}
        stop_observed(std::move(snapshot));for(auto& worker:workers)if(worker.joinable())worker.join();
        std::unique_lock lock(mutex);changed.wait(lock,[&]{return dispatched.empty();});
    }
};
struct NativeChildExecutor::Batch::State {
    std::shared_ptr<Impl> owner;
    std::list<std::shared_ptr<Impl::Job>> staged;
    std::vector<std::shared_ptr<Impl::Job>> jobs;
    std::vector<std::shared_future<Run>> results;
    bool reserved=false,dispatched=false;
};
NativeChildExecutor::Batch::Batch(std::unique_ptr<State> value):state_(std::move(value)){}
NativeChildExecutor::Batch::~Batch(){
    if(!state_)return;
    request_stop();
    if(state_->reserved){std::lock_guard lock(state_->owner->mutex);
        for(const auto& job:state_->jobs)if(job->phase==Impl::Job::Phase::staged){
            const auto tracked=std::find(state_->owner->staged.begin(),state_->owner->staged.end(),job);
            state_->owner->staged.erase(tracked);job->phase=Impl::Job::Phase::retired;--state_->owner->reserved;
        }
        state_->reserved=false;state_->owner->changed.notify_all();}
    if(state_->dispatched)for(const auto& result:state_->results)result.wait();
}
const std::vector<std::shared_future<Run>>& NativeChildExecutor::Batch::results()const{return state_->results;}
void NativeChildExecutor::Batch::request_stop(){for(auto& job:state_->jobs)job->stop.request_stop();state_->owner->changed.notify_all();}
void NativeChildExecutor::Batch::dispatch(){
    std::lock_guard lock(state_->owner->mutex);
    if(state_->dispatched||!state_->reserved)throw Conflict("Native child batch has already dispatched or retired");
    if(!state_->owner->accepting||std::any_of(state_->jobs.begin(),state_->jobs.end(),[](const auto& job){return job->phase!=Impl::Job::Phase::staged;}))throw NativeChildOutcomeUnrecorded("Native child batch retired before owned dispatch");
    for(auto& job:state_->jobs){const auto tracked=std::find(state_->owner->staged.begin(),state_->owner->staged.end(),job);
        job->phase=Impl::Job::Phase::dispatched;state_->owner->pending.splice(state_->owner->pending.end(),state_->owner->staged,tracked);}
    state_->owner->reserved-=state_->jobs.size();state_->reserved=false;state_->dispatched=true;state_->owner->changed.notify_all();
}
NativeChildExecutor::NativeChildExecutor(std::size_t count,std::size_t capacity):impl_(std::make_shared<Impl>(count,capacity)){}
NativeChildExecutor::~NativeChildExecutor(){impl_->close();}
std::unique_ptr<NativeChildExecutor::Batch> NativeChildExecutor::stage(std::vector<NativeChildWork> work){
    if(work.empty()||work.size()>8)throw std::invalid_argument("Native child staging requires a bounded nonempty batch");
    auto state=std::make_unique<Batch::State>();state->owner=impl_;state->jobs.reserve(work.size());state->results.reserve(work.size());
    for(auto& value:work){if(!value.budget||!value.action)throw std::invalid_argument("Native child requires its shared owner budget and retirement closure");
        auto job=std::make_shared<Impl::Job>(std::move(value),impl_->changed);state->results.push_back(job->result);state->jobs.push_back(job);state->staged.push_back(std::move(job));}
    auto result=std::unique_ptr<Batch>(new Batch(std::move(state)));
    {std::lock_guard lock(impl_->mutex);if(!impl_->accepting||impl_->faulted)throw NativeChildOutcomeUnrecorded("Native child executor is faulted");
        const auto count=result->state_->jobs.size();if(count>impl_->capacity-std::min(impl_->capacity,impl_->pending.size()+impl_->reserved)||count>impl_->capacity+impl_->worker_limit-std::min(impl_->capacity+impl_->worker_limit,impl_->pending.size()+impl_->reserved+impl_->dispatched.size()))throw NativeChildCapacityUnavailable("Native child queue is full");
        impl_->reserved+=count;result->state_->reserved=true;impl_->staged.splice(impl_->staged.end(),result->state_->staged);}
    return result;
}
bool NativeChildExecutor::assist(const std::shared_ptr<NativeChildSlot>& slot){auto owner=impl_;return owner->assist(slot);}
bool NativeChildExecutor::healthy()const{std::lock_guard lock(impl_->mutex);return impl_->accepting&&!impl_->faulted;}
void NativeChildExecutor::fail(){impl_->fail();}
void NativeChildExecutor::close(){impl_->close();}
}
