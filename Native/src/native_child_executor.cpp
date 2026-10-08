#include "agentflow/native_child_executor.hpp"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>

namespace agentflow {
struct NativeChildExecutor::Impl {
    struct Job {
        enum class Phase {staged,dispatched,retired};
        std::shared_ptr<RootExecutionBudget> budget;
        std::function<Run(std::stop_token)> action;
        std::promise<Run> promise;
        std::shared_future<Run> result;
        std::stop_source stop;
        Phase phase=Phase::staged;
        std::stop_callback<std::function<void()>> external;
        Job(NativeChildWork work,std::condition_variable& changed)
            :budget(std::move(work.budget)),action(std::move(work.action)),result(promise.get_future().share()),
             external(work.cancel,[this,&changed]{stop.request_stop();changed.notify_all();}){}
    };
    mutable std::mutex mutex;
    std::mutex closing;
    std::condition_variable changed;
    std::list<std::shared_ptr<Job>> staged,pending,dispatched;
    std::vector<std::thread> workers;
    std::size_t capacity,reserved=0;
    bool accepting=true,faulted=false;
    Impl(std::size_t count,std::size_t limit):capacity(limit){
        if(count<1||count>4||limit<4||limit>64)throw std::invalid_argument("Invalid native child executor capacity");
        try{for(std::size_t i=0;i<count;++i)workers.emplace_back([this]{work();});}catch(...){close();throw;}
    }
    ~Impl(){close();}
    struct StopSnapshot {
        std::array<std::shared_ptr<Job>,68> jobs;
        std::array<bool,68> retired{};
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
    void work(){
        for(;;){
            std::shared_ptr<Job> job;std::list<std::shared_ptr<Job>>::iterator tracked;bool child_slot=false;
            {
                std::unique_lock lock(mutex);
                for(;;){
                    auto next=pending.end();
                    for(auto item=pending.begin();item!=pending.end();++item){
                        // Stopped/deadline jobs still execute their retirement
                        // closure, which checks BEFORE runner/peer construction.
                        if((*item)->stop.stop_requested()||std::chrono::steady_clock::now()>=(*item)->budget->deadline()){next=item;break;}
                        if((*item)->budget->try_acquire_leaf()){next=item;child_slot=true;break;}
                    }
                    if(next!=pending.end()){job=*next;dispatched.splice(dispatched.end(),pending,next);tracked=next;break;}
                    if(!accepting&&pending.empty()&&reserved==0)return;
                    changed.wait_for(lock,std::chrono::milliseconds(10));
                }
            }
            try{
                auto actual=job->action(job->stop.get_token());
                if(child_slot){job->budget->release_leaf();child_slot=false;}
                job->promise.set_value(std::move(actual));
            }catch(...){
                const auto failure=std::current_exception();if(child_slot){job->budget->release_leaf();child_slot=false;}
                fail();
                job->promise.set_exception(failure);
            }
            {std::lock_guard lock(mutex);dispatched.erase(tracked);}changed.notify_all();
        }
    }
    void close(){
        std::lock_guard serial(closing);
        StopSnapshot snapshot;{std::lock_guard lock(mutex);snapshot=stop_locked(false);}
        stop_observed(std::move(snapshot));for(auto& worker:workers)if(worker.joinable())worker.join();
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
        const auto count=result->state_->jobs.size();if(count>impl_->capacity-std::min(impl_->capacity,impl_->pending.size()+impl_->reserved))throw NativeChildCapacityUnavailable("Native child queue is full");
        impl_->reserved+=count;result->state_->reserved=true;impl_->staged.splice(impl_->staged.end(),result->state_->staged);}
    return result;
}
bool NativeChildExecutor::healthy()const{std::lock_guard lock(impl_->mutex);return impl_->accepting&&!impl_->faulted;}
void NativeChildExecutor::fail(){impl_->fail();}
void NativeChildExecutor::close(){impl_->close();}
}
