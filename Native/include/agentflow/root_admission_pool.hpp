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
        std::size_t execution_capacity,running=0,blocked=0;
        std::deque<std::shared_ptr<const int>> waiting;
    };
public:
    // Queue admission is separate from acquiring an execution slot: roots in
    // either service's local backlog must already have their shared FIFO rank.
    class DispatchTicket final {
    public:
        DispatchTicket(const DispatchTicket&)=delete;
        DispatchTicket& operator=(const DispatchTicket&)=delete;
        ~DispatchTicket(){
            std::lock_guard lock(state_->mutex);
            const auto found=std::find(state_->waiting.begin(),state_->waiting.end(),identity_);
            if(found!=state_->waiting.end())state_->waiting.erase(found);
            state_->changed.notify_all();
        }
    private:
        friend class RootAdmissionPool;
        explicit DispatchTicket(std::shared_ptr<State> state):state_(std::move(state)),identity_(std::make_shared<const int>(0)){}
        std::shared_ptr<State> state_;
        std::shared_ptr<const int> identity_;
        bool consumed_=false;
    };
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
    std::unique_ptr<DispatchTicket> queue_execution(){
        auto ticket=std::unique_ptr<DispatchTicket>(new DispatchTicket(state_));
        std::lock_guard lock(state_->mutex);state_->waiting.push_back(ticket->identity_);return ticket;
    }
    // The caller owns the ticket exclusively and holds no service-owner lock
    // while waiting. A cancelled/failed queue entry is removed by its owner.
    std::unique_ptr<Execution> acquire_execution(DispatchTicket& ticket,std::stop_token cancel={}){
        if(ticket.state_!=state_)throw std::invalid_argument("Dispatch ticket belongs to another root pool");
        auto execution=std::unique_ptr<Execution>(new Execution(std::shared_ptr<State>{}));
        std::unique_lock lock(state_->mutex);
        if(ticket.consumed_)throw std::logic_error("Dispatch ticket has already been consumed");
        ticket.consumed_=true;
        auto remove=[&]{const auto found=std::find(state_->waiting.begin(),state_->waiting.end(),ticket.identity_);if(found!=state_->waiting.end())state_->waiting.erase(found);state_->changed.notify_all();};
        try{
            if(cancel.stop_requested()){remove();return {};}
            ++state_->blocked;
            try{state_->changed.wait(lock,cancel,[&]{return !state_->waiting.empty()&&state_->waiting.front()==ticket.identity_&&state_->running<state_->execution_capacity;});}
            catch(...){--state_->blocked;throw;}
            --state_->blocked;
            if(cancel.stop_requested()){remove();return {};}
            state_->waiting.pop_front();++state_->running;execution->state_=state_;
            state_->changed.notify_all();return execution;
        }catch(...){remove();throw;}
    }
    std::unique_ptr<Execution> acquire_execution(std::stop_token cancel={}){
        auto ticket=queue_execution();return acquire_execution(*ticket,cancel);
    }
    std::size_t running()const{std::lock_guard lock(state_->mutex);return state_->running;}
    std::size_t dispatch_waiting()const{std::lock_guard lock(state_->mutex);return state_->waiting.size();}
    std::size_t dispatch_blocked()const{std::lock_guard lock(state_->mutex);return state_->blocked;}
private:
    std::shared_ptr<State> state_;
};
}
