#include "native_web_request_queue.hpp"
#include <chrono>
#include <future>
#include <iostream>

namespace {
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void completed(const std::shared_ptr<agentflow::NativeWebRequestState>& state){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!state->ready.load(std::memory_order_acquire)){check(std::chrono::steady_clock::now()<end,"Native job completion deadline");std::this_thread::yield();}
}
}
int main(){try{
    {
        agentflow::NativeWebRequestQueue queue;
        std::promise<void> entered,release;const auto released=release.get_future().share();std::atomic_int calls=0;
        const auto blocked=queue.start([&](const auto&){++calls;entered.set_value();released.wait();});
        entered.get_future().wait();check(!blocked->ready,"Submission waited for domain work");
        bool pending=false;try{blocked->check();}catch(const std::runtime_error&){pending=true;}check(pending,"Pending response was readable");
        const auto independent=queue.start([&](const auto&){++calls;});completed(independent);independent->check();
        check(!blocked->ready,"Independent request required the blocked request to finish");
        release.set_value();completed(blocked);blocked->check();queue.drain();check(calls==2,"An admitted request was repeated");
    }
    {
        agentflow::NativeWebRequestQueue queue(1);
        const auto active=queue.start([](const auto& state){while(!state->cancelled->load())std::this_thread::yield();});
        bool limited=false;try{queue.start([](const auto&){});}catch(const std::runtime_error&){limited=true;}check(limited,"Request capacity was unbounded");
        queue.drain();check(active->ready&&*active->cancelled,"Shutdown did not cancel and join the admitted job");
        bool closed=false;try{queue.start([](const auto&){});}catch(const std::runtime_error&){closed=true;}check(closed,"Shutdown admitted another request");
    }
    {
        agentflow::NativeWebRequestQueue queue(1);std::atomic_int failures=0;
        const auto failed=queue.start([&](const auto&){++failures;throw std::runtime_error("Original native failure");});completed(failed);
        bool original=false;try{failed->check();}catch(const std::runtime_error& error){original=std::string(error.what())=="Original native failure";}check(original&&failures==1,"Failure was lost or work replayed");
        const auto next=queue.start([](const auto&){});completed(next);next->check();
    }
    std::cout<<"Native web jobs: concurrent observation, bounded admission, failure, cancellation and joined shutdown passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
