#include "agentflow/root_work_executor.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void ready(const std::shared_future<void>& value){require(value.wait_for(5s)==std::future_status::ready,"Physical root action did not settle");value.get();}
template<class Action>void rejects(Action action){try{action();}catch(const std::exception&){return;}throw std::runtime_error("Expected rejection");}
void staged_order(){
    RootWorkExecutor pool(1,3);std::atomic<int> calls=0;
    auto older=pool.stage([&](std::stop_token){++calls;});
    auto later=pool.stage([&](std::stop_token){++calls;});later->commit();
    require(later->result().wait_for(50ms)==std::future_status::timeout&&calls==0,"Uncommitted older admission must retain FIFO rank without executing");
    auto withdrawn=older->result();older.reset();rejects([&]{withdrawn.get();});
    ready(later->result());require(calls==1,"Withdrawn admission must never execute and must release later work");
    rejects([&]{later->commit();});
}
void mixed_utilization(){
    RootWorkExecutor pool(2,4);std::promise<void> entered,release;auto hold=release.get_future().share();
    auto agent=pool.stage([&](std::stop_token){entered.set_value();hold.wait();});agent->commit();
    require(entered.get_future().wait_for(5s)==std::future_status::ready,"First agent did not occupy a real worker");
    std::atomic<int> graphs=0;auto graphA=pool.stage([&](std::stop_token){++graphs;});auto graphB=pool.stage([&](std::stop_token){++graphs;});
    graphA->commit();graphB->commit();
    const bool utilized=graphA->result().wait_for(5s)==std::future_status::ready&&graphB->result().wait_for(5s)==std::future_status::ready;
    release.set_value();ready(agent->result());require(utilized&&graphs==2,"Idle shared worker must serve consecutive graph actions while an agent remains occupied");ready(graphA->result());ready(graphB->result());
}
void close_and_cancel(){
    RootWorkExecutor pool(1,3);std::promise<void> entered;std::atomic<int> stopped=0,uncommitted=0;
    auto active=pool.stage([&](std::stop_token stop){entered.set_value();std::mutex mutex;std::condition_variable_any changed;std::unique_lock lock(mutex);changed.wait(lock,stop,[]{return false;});++stopped;});active->commit();
    require(entered.get_future().wait_for(5s)==std::future_status::ready,"Active root must actually start before close");
    auto staged=pool.stage([&](std::stop_token){++uncommitted;});
    auto pending=pool.stage([&](std::stop_token stop){require(stop.stop_requested(),"Close must stop-request queued retirement before launch");++stopped;});pending->commit();
    pool.close();ready(active->result());ready(pending->result());rejects([&]{staged->result().get();});rejects([&]{staged->commit();});
    require(stopped==2&&uncommitted==0,"Close must drain committed actions and withdraw staged admission");rejects([&]{pool.stage([](std::stop_token){});});pool.close();
}
void capacity_and_failure(){
    RootWorkExecutor pool(1,1);auto staged=pool.stage([](std::stop_token){});rejects([&]{pool.stage([](std::stop_token){});});staged.reset();
    auto failed=pool.stage([](std::stop_token){throw std::runtime_error("Actual retirement failure");});failed->commit();
    require(failed->result().wait_for(5s)==std::future_status::ready,"Failed root action must settle");rejects([&]{failed->result().get();});
    auto cancelled=pool.stage([](std::stop_token stop){require(stop.stop_requested(),"Explicit root cancellation must reach actual action");});cancelled->request_stop();cancelled->commit();ready(cancelled->result());
}
}
int main(){try{staged_order();mixed_utilization();close_and_cancel();capacity_and_failure();std::cout<<"Shared physical root worker lifecycle passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
