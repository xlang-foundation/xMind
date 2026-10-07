#include "agentflow/agent_service.hpp"
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <algorithm>

namespace agentflow {
struct AgentService::Impl {
    struct Job {std::string id;std::stop_source stop;};
    PersistenceService& persistence;
    AgentRunner runner;
    mutable std::mutex mutex;
    std::mutex close_mutex;
    std::condition_variable changed;
    std::deque<std::shared_ptr<Job>> pending;
    std::map<std::string,std::shared_ptr<Job>> active;
    std::vector<std::thread> workers;
    std::size_t limit;
    bool accepting=true,faulted=false;
    Impl(PersistenceService& store,AgentSettings settings,std::size_t count,std::size_t capacity)
        :persistence(store),runner(store,std::move(settings)),limit(capacity) {
        if(count==0 || count>16 || capacity==0 || capacity>4096) throw std::invalid_argument("Invalid agent worker capacity");
        try {for(std::size_t i=0;i<count;++i) workers.emplace_back([this]{work();});}
        catch(...) {close();throw;}
    }
    ~Impl() {close();}
    void fail() {
        std::lock_guard lock(mutex);faulted=true;accepting=false;
        for(const auto& [id,job]:active) {(void)id;job->stop.request_stop();}
        changed.notify_all();
    }
    void work() {
        for(;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(mutex);changed.wait(lock,[&]{return !pending.empty() || !accepting;});
                if(pending.empty()) return;job=std::move(pending.front());pending.pop_front();
            }
            try {runner.execute(job->id,job->stop.get_token());}
            catch(const Conflict&) {
                // A lost claim does not authorize modifying another worker's run.
            }
            catch(...) {
                // Runner normally journals failures. If it cannot, mark service
                // health degraded; do not invent a terminal record or ownership.
                fail();
            }
            {std::lock_guard lock(mutex);active.erase(job->id);}
        }
    }
    void close() {
        std::lock_guard closing(close_mutex);
        {
            std::lock_guard lock(mutex);accepting=false;
            for(const auto& [id,job]:active) {(void)id;job->stop.request_stop();}
        }
        changed.notify_all();for(auto& worker:workers) if(worker.joinable()) worker.join();
    }
};
AgentService::AgentService(PersistenceService& store,AgentSettings settings,std::size_t workers,std::size_t capacity)
    :impl_(std::make_unique<Impl>(store,std::move(settings),workers,capacity)) {}
AgentService::~AgentService()=default;
Run AgentService::submit(std::string id,std::string session,std::string prompt) {
    auto job=std::make_shared<Impl::Job>();job->id=id;
    std::unique_lock lock(impl_->mutex);
    if(!impl_->accepting || impl_->faulted) throw RunUnavailable("Agent executor is unavailable");
    if(impl_->pending.size()>=impl_->limit) throw RunBusy("Agent queue is full");
    if(impl_->active.contains(id)) throw Conflict("Run already exists");
    impl_->active.emplace(id,job);
    try {impl_->pending.push_back(job);} catch(...) {impl_->active.erase(id);throw;}
    Run result;
    try {result=impl_->runner.start(id,std::move(session),std::move(prompt));}
    catch(...) {impl_->pending.pop_back();impl_->active.erase(id);throw;}
    lock.unlock();impl_->changed.notify_one();return result;
}
void AgentService::cancel(const std::string& id) {
    std::shared_ptr<Impl::Job> job;
    {
        std::lock_guard lock(impl_->mutex);
        if(impl_->faulted) throw RunUnavailable("Agent executor is unavailable");
        const auto found=impl_->active.find(id);
        if(found!=impl_->active.end()) {
            job=found->second;
            const auto queued=std::find(impl_->pending.begin(),impl_->pending.end(),job);
            if(queued!=impl_->pending.end()) {
                // Admission and worker dequeue share this mutex, so the queued
                // job has not started. Persist cancellation and free capacity.
                impl_->persistence.transition(id,RunState::queued,RunState::cancelled,R"({"reason":"cancelled_before_start"})").get();
                impl_->pending.erase(queued);impl_->active.erase(found);job->stop.request_stop();return;
            }
        }
    }
    if(!job) {
        impl_->persistence.run(id).get();throw Conflict("Run is no longer owned by this executor");
    }
    job->stop.request_stop();
}
bool AgentService::healthy() const {std::lock_guard lock(impl_->mutex);return impl_->accepting && !impl_->faulted;}
void AgentService::close() {impl_->close();}
}
