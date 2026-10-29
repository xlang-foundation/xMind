#pragma once
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace agentflow {
struct NativeWebRequestState {
    std::atomic_bool ready=false;
    std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
    std::exception_ptr failure;
    void check()const {
        if(!ready.load(std::memory_order_acquire))throw std::runtime_error("Native browser response is pending");
        if(failure)std::rethrow_exception(failure);
    }
};

// Domain work must not hold the ASGI runtime's native-call boundary. Each
// admitted request runs once; the event loop only observes its completion.
class NativeWebRequestQueue {
    struct Entry {std::shared_ptr<NativeWebRequestState> state;std::thread worker;};
    std::mutex mutex_;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::size_t limit_;
    bool closed_=false;
public:
    explicit NativeWebRequestQueue(std::size_t limit=64):limit_(limit){if(!limit)throw std::invalid_argument("Invalid browser request capacity");entries_.reserve(limit);}
    NativeWebRequestQueue(const NativeWebRequestQueue&)=delete;
    ~NativeWebRequestQueue(){drain();}
    std::shared_ptr<NativeWebRequestState> start(std::function<void(const std::shared_ptr<NativeWebRequestState>&)> work){
        std::lock_guard lock(mutex_);
        if(closed_)throw std::runtime_error("Native browser requests are closed");
        for(auto it=entries_.begin();it!=entries_.end();)if((*it)->state->ready.load(std::memory_order_acquire)){(*it)->worker.join();it=entries_.erase(it);}else ++it;
        if(entries_.size()>=limit_)throw std::runtime_error("Native browser request capacity is busy");
        auto entry=std::make_unique<Entry>();entry->state=std::make_shared<NativeWebRequestState>();const auto state=entry->state;
        entry->worker=std::thread([state,work=std::move(work)]{try{work(state);}catch(...){state->failure=std::current_exception();}state->ready.store(true,std::memory_order_release);});
        entries_.push_back(std::move(entry));return state;
    }
    void drain(){
        std::vector<std::unique_ptr<Entry>> pending;
        {std::lock_guard lock(mutex_);closed_=true;pending.swap(entries_);}
        for(const auto& entry:pending)entry->state->cancelled->store(true);
        for(const auto& entry:pending)if(entry->worker.joinable())entry->worker.join();
    }
};
}
