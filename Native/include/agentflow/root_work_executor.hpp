#pragma once
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

namespace agentflow {
// Physical root workers shared across service types. The backend stages work
// before its admission transaction and commits only after durable acceptance.
// Actions own retirement and must check the supplied token before effects.
class RootWorkExecutor final {
    struct Job {
        enum class Phase {staged,ready,running,retired};
        Phase phase=Phase::staged;
        std::function<void(std::stop_token)> action;
        std::stop_source stop;
        std::promise<void> promise;
        std::shared_future<void> result=promise.get_future().share();
    };
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        std::list<std::shared_ptr<Job>> queue,running;
        std::size_t capacity;
        bool accepting=true;
        explicit State(std::size_t value):capacity(value){}
    };
public:
    class Ticket final {
    public:
        Ticket(const Ticket&)=delete;
        Ticket& operator=(const Ticket&)=delete;
        ~Ticket(){
            request_stop();
            bool dispatched=false;
            {
                std::lock_guard lock(state_->mutex);
                if(job_->phase==Job::Phase::staged){
                    state_->queue.remove(job_);job_->phase=Job::Phase::retired;
                    job_->promise.set_exception(std::make_exception_ptr(std::runtime_error("Root admission withdrawn before dispatch")));
                }else dispatched=job_->phase!=Job::Phase::retired;
            }
            state_->changed.notify_all();
            // Owner destruction cannot abandon a committed physical action.
            // Never destroy a ticket from within its own action.
            if(dispatched)job_->result.wait();
        }
        void commit(){
            std::lock_guard lock(state_->mutex);
            if(!state_->accepting||job_->phase!=Job::Phase::staged)
                throw std::logic_error("Root dispatch is closed or already committed");
            job_->phase=Job::Phase::ready;state_->changed.notify_all();
        }
        void request_stop(){job_->stop.request_stop();}
        const std::shared_future<void>& result()const{return job_->result;}
    private:
        friend class RootWorkExecutor;
        Ticket(std::shared_ptr<State> state,std::shared_ptr<Job> job):state_(std::move(state)),job_(std::move(job)){}
        std::shared_ptr<State> state_;
        std::shared_ptr<Job> job_;
    };
    explicit RootWorkExecutor(std::size_t workers,std::size_t capacity):state_(std::make_shared<State>(capacity)){
        if(workers<1||workers>16||capacity<1||capacity>4096)throw std::invalid_argument("Invalid shared root worker capacity");
        try{for(std::size_t i=0;i<workers;++i)workers_.emplace_back([state=state_]{work(state);});}
        catch(...){close();throw;}
    }
    ~RootWorkExecutor(){close();}
    RootWorkExecutor(const RootWorkExecutor&)=delete;
    RootWorkExecutor& operator=(const RootWorkExecutor&)=delete;
    std::unique_ptr<Ticket> stage(std::function<void(std::stop_token)> action){
        if(!action)throw std::invalid_argument("Root dispatch requires an owned retirement action");
        auto job=std::make_shared<Job>();job->action=std::move(action);
        auto ticket=std::unique_ptr<Ticket>(new Ticket(state_,job));
        std::lock_guard lock(state_->mutex);
        if(!state_->accepting)throw std::runtime_error("Shared root executor is closed");
        if(state_->queue.size()>=state_->capacity)throw std::runtime_error("Shared root queue is full");
        state_->queue.push_back(job);return ticket;
    }
    void close(){
        std::lock_guard serial(closing_);
        std::vector<std::shared_ptr<Job>> stopped;
        {
            std::lock_guard lock(state_->mutex);state_->accepting=false;
            stopped.assign(state_->queue.begin(),state_->queue.end());
            stopped.insert(stopped.end(),state_->running.begin(),state_->running.end());
            for(auto item=state_->queue.begin();item!=state_->queue.end();){
                if((*item)->phase==Job::Phase::staged){
                    auto job=*item;item=state_->queue.erase(item);job->phase=Job::Phase::retired;
                    job->promise.set_exception(std::make_exception_ptr(std::runtime_error("Root dispatch closed before admission committed")));
                }else ++item;
            }
        }
        // Stop callbacks can call back into owners; invoke outside queue lock.
        for(const auto& job:stopped)job->stop.request_stop();
        state_->changed.notify_all();
        for(auto& worker:workers_)if(worker.joinable())worker.join();
    }
private:
    static void work(const std::shared_ptr<State>& state){
        for(;;){
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(state->mutex);
                state->changed.wait(lock,[&]{return (!state->queue.empty()&&state->queue.front()->phase==Job::Phase::ready)||(!state->accepting&&state->queue.empty());});
                if(state->queue.empty())return;
                auto item=state->queue.begin();job=*item;job->phase=Job::Phase::running;
                state->running.splice(state->running.end(),state->queue,item);
            }
            std::exception_ptr failure;
            try{job->action(job->stop.get_token());}catch(...){failure=std::current_exception();}
            {
                std::lock_guard lock(state->mutex);state->running.remove(job);job->phase=Job::Phase::retired;
                if(failure)job->promise.set_exception(failure);else job->promise.set_value();
            }
            state->changed.notify_all();
        }
    }
    std::shared_ptr<State> state_;
    std::vector<std::thread> workers_;
    std::mutex closing_;
};
}
