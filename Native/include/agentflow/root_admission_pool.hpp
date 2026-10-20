#pragma once
#include <cstddef>
#include <algorithm>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <stop_token>
#include <stdexcept>
#include <utility>

namespace agentflow {
// Shared bounded ownership for top-level agent and graph roots. A reservation
// remains held while a root is queued, running, or paused for human input.
class RootAdmissionPool final {
    struct State {
        State(std::size_t value,std::size_t slots):capacity(value),execution_capacity(slots){}
        std::mutex mutex;
        std::size_t capacity;
        std::size_t occupied=0;
        std::condition_variable_any changed;
        std::size_t execution_capacity,running=0;
        std::deque<std::shared_ptr<const int>> waiting;
    };
public:
    // Execution slots are independent of durable root reservations. A paused
    // root keeps its Lease but releases this slot until its next runnable wake.
    class Execution final {
    public:
        Execution(const Execution&)=delete;
        Execution& operator=(const Execution&)=delete;
        ~Execution(){if(state_){std::lock_guard lock(state_->mutex);--state_->running;state_->changed.notify_all();}}
    private:
        friend class RootAdmissionPool;
        explicit Execution(std::shared_ptr<State> state):state_(std::move(state)){}
        std::shared_ptr<State> state_;
    };
    class Lease final {
    public:
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        ~Lease(){if(state_){std::lock_guard lock(state_->mutex);if(state_->occupied>0)--state_->occupied;}}
    private:
        friend class RootAdmissionPool;
        explicit Lease(std::shared_ptr<State> state):state_(std::move(state)){}
        std::shared_ptr<State> state_;
    };

    explicit RootAdmissionPool(std::size_t capacity,std::size_t execution_capacity=0)
        :state_(std::make_shared<State>(capacity,execution_capacity?execution_capacity:capacity)){
        if(capacity==0||state_->execution_capacity>capacity)throw std::invalid_argument("Invalid root admission or execution capacity");
    }
    std::unique_ptr<Lease> try_reserve(){
        {
            std::lock_guard lock(state_->mutex);
            if(state_->occupied>=state_->capacity)return {};
            ++state_->occupied;
        }
        try{return std::unique_ptr<Lease>(new Lease(state_));}
        catch(...){std::lock_guard lock(state_->mutex);--state_->occupied;throw;}
    }
    std::size_t capacity()const noexcept{return state_->capacity;}
    std::size_t occupied()const{std::lock_guard lock(state_->mutex);return state_->occupied;}
    // Call outside service-owner locks. Only actual worker-ready roots join
    // this FIFO; it does not claim ordering for still-local service backlogs.
    std::unique_ptr<Execution> acquire_execution(std::stop_token cancel={}){
        auto execution=std::unique_ptr<Execution>(new Execution({}));
        auto ticket=std::make_shared<const int>(0);
        std::unique_lock lock(state_->mutex);
        if(cancel.stop_requested())return {};
        state_->waiting.push_back(ticket);
        auto remove=[&]{const auto found=std::find(state_->waiting.begin(),state_->waiting.end(),ticket);if(found!=state_->waiting.end())state_->waiting.erase(found);state_->changed.notify_all();};
        try{
            state_->changed.wait(lock,cancel,[&]{return state_->waiting.front()==ticket&&state_->running<state_->execution_capacity;});
            if(cancel.stop_requested()){remove();return {};}
            state_->waiting.pop_front();++state_->running;execution->state_=state_;
            state_->changed.notify_all();return execution;
        }catch(...){remove();throw;}
    }
    std::size_t running()const{std::lock_guard lock(state_->mutex);return state_->running;}
    std::size_t dispatch_waiting()const{std::lock_guard lock(state_->mutex);return state_->waiting.size();}
private:
    std::shared_ptr<State> state_;
};
}
