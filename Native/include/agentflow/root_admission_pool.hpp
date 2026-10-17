#pragma once
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace agentflow {
// Shared bounded ownership for top-level agent and graph roots. A reservation
// remains held while a root is queued, running, or paused for human input.
class RootAdmissionPool final {
    struct State {
        explicit State(std::size_t value):capacity(value){}
        std::mutex mutex;
        std::size_t capacity;
        std::size_t occupied=0;
    };
public:
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

    explicit RootAdmissionPool(std::size_t capacity):state_(std::make_shared<State>(capacity)){
        if(capacity==0)throw std::invalid_argument("Root admission capacity must be positive");
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
private:
    std::shared_ptr<State> state_;
};
}
