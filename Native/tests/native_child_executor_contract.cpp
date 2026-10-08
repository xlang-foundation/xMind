// Actual native pool and xlang3 SQLite; no model transport is used. Repository
// task metadata below is explicitly synthetic and does not claim inference.
#include "agentflow/native_child_executor.hpp"
#include "nlohmann/json.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>
#include <barrier>
#include <thread>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action>void rejects(Action action,const char* reason){try{action();}catch(const Error&){return;}throw std::runtime_error(reason);}
std::shared_ptr<RootExecutionBudget> root(PersistenceService& store,const std::string& id){
    RootBudgetSpec spec;spec.policy_id="native.delegation";spec.workspace_identity="synthetic-pool-fixture-workspace";
    spec.provider_identity_json=R"({"wire":"chat-completions","model_id":"synthetic-pool-no-provider"})";spec.wall_limit_ms=20000;spec.max_parallel=1;
    store.create_session(id+"-session","Native pool lifecycle; no provider").get();
    store.start_prompt_run(id,id+"-session",R"({"content":"Explicit synthetic repository lifecycle input"})",spec).get();
    store.transition(id,RunState::queued,RunState::running).get();
    return std::make_shared<RootExecutionBudget>(store,store.root_budget(id).get(),std::chrono::steady_clock::now()+20s,std::stop_token{});
}
DelegationBatchSpec admission(PersistenceService& store,const std::string& id){
    const std::string objective="Explicit synthetic native pool lifecycle task";DelegationBatchSpec spec;spec.id=id+"-batch";spec.parent_run_id=id;spec.provider_tool_call_id=id+"-synthetic-call";
    spec.expected_budget_revision=store.root_budget(id).get().revision;
    spec.arguments_json=Json{{"tasks",Json::array({{{"id","child"},{"objective",objective},{"preset","workspace.inspect"}}})}}.dump();
    spec.parent_assistant_json=Json{{"content","Explicit synthetic repository metadata; no model call"},{"tool_calls",Json::array({{{"id",spec.provider_tool_call_id},{"name","delegate_tasks"},{"arguments",spec.arguments_json}}})}}.dump();
    spec.tasks.push_back({"child",id+"-node",id+"-child",Json{{"content",objective}}.dump(),objective});return spec;
}
void stopped_without_slot(PersistenceService& store,bool elapsed){
    const std::string id=elapsed?"pool-deadline":"pool-stopped";auto budget=root(store,id);
    if(elapsed)budget=std::make_shared<RootExecutionBudget>(store,store.root_budget(id).get(),std::chrono::steady_clock::now()-1ms,std::stop_token{});
    require(budget->try_acquire_leaf(),"Fixture must occupy the one actual owner slot");NativeChildExecutor pool(1,4);std::stop_source cancel;if(!elapsed)cancel.request_stop();
    auto batch=pool.stage({NativeChildWork{budget,[&](std::stop_token stop){
        require(stop.stop_requested()||std::chrono::steady_clock::now()>=budget->deadline(),"Retirement closure must observe actual stop/deadline before any launch");
        return store.transition(id+"-child",RunState::queued,elapsed?RunState::failed:RunState::cancelled,R"({"reason":"native_pool_before_dispatch_retirement"})").get();
    },cancel.get_token()}});
    const auto spec=admission(store,id);store.accept_delegation_batch(spec).get();batch->dispatch();
    const bool ready=batch->results()[0].wait_for(2s)==std::future_status::ready;budget->release_leaf();
    require(ready,"Stopped/deadline jobs must retire without acquiring an unavailable leaf slot");
    const auto actual=batch->results()[0].get();require(actual.id==id+"-child"&&actual.state==(elapsed?RunState::failed:RunState::cancelled),"Before-launch retirement must be the actual owned child");
    pool.close();store.settle_delegation_child(actual.id).get();store.settle_delegation_batch(spec.id).get();store.transition(id,RunState::running,RunState::cancelled,R"({"reason":"synthetic_pool_fixture_complete"})").get();
}
bool fail_dispatch_race(PersistenceService& store){
    const std::string id="pool-race";auto budget=root(store,id);NativeChildExecutor pool(1,4);
    auto batch=pool.stage({NativeChildWork{budget,[&](std::stop_token stop){
        if(stop.stop_requested())return store.transition(id+"-child",RunState::queued,RunState::cancelled,R"({"reason":"native_pool_race_before_dispatch"})").get();
        store.transition(id+"-child",RunState::queued,RunState::running).get();
        const auto until=std::chrono::steady_clock::now()+5s;while(!stop.stop_requested()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);
        require(stop.stop_requested(),"Concurrent fault must stop actual launched work");return store.transition(id+"-child",RunState::running,RunState::cancelled,R"({"reason":"native_pool_race_drained"})").get();
    },{}}});
    const auto spec=admission(store,id);store.accept_delegation_batch(spec).get();std::barrier gate(3);bool dispatched=false;std::exception_ptr failure;
    std::thread dispatch([&]{gate.arrive_and_wait();try{batch->dispatch();dispatched=true;}catch(const NativeChildOutcomeUnrecorded&){}catch(...){failure=std::current_exception();}});
    std::thread fault([&]{gate.arrive_and_wait();pool.fail();});gate.arrive_and_wait();dispatch.join();fault.join();pool.close();if(failure)std::rethrow_exception(failure);
    if(!dispatched){rejects<NativeChildOutcomeUnrecorded>([&]{batch->results()[0].get();},"Fault-before-dispatch cannot invent a child result");require(store.run(id+"-child").get().state==RunState::queued,"Undispatched admitted child remains owned for recovery");return true;}
    const auto actual=batch->results()[0].get();require(actual.state==RunState::cancelled,"Dispatch-before-fault must drain its actual child");store.settle_delegation_child(actual.id).get();store.settle_delegation_batch(spec.id).get();store.transition(id,RunState::running,RunState::cancelled,R"({"reason":"synthetic_pool_fixture_complete"})").get();return false;
}
void staged_retirement(PersistenceService& store,bool fault){
    const std::string id=fault?"pool-staged-fault":"pool-staged-close";auto budget=root(store,id);
    NativeChildExecutor pool(1,4);std::atomic<int> calls=0;
    auto batch=pool.stage({NativeChildWork{budget,[&](std::stop_token)->Run{++calls;throw std::runtime_error("An unadmitted callback must never launch");},{}}});
    if(fault)pool.fail();pool.close(); // Batch is deliberately still alive.
    require(batch->results()[0].wait_for(0ms)==std::future_status::ready,"Close must retire held staged futures without waiting for Batch destruction");
    rejects<NativeChildOutcomeUnrecorded>([&]{batch->results()[0].get();},"Staged retirement must not fabricate a Run outcome");
    rejects<NativeChildOutcomeUnrecorded>([&]{batch->dispatch();},"A retired reservation must reject before native dispatch");
    require(calls==0&&store.owned_children(id).get().empty()&&store.operations(id).get().empty(),"Undispatched close/fault must not create a child or effect");
    store.transition(id,RunState::running,RunState::cancelled,R"({"reason":"synthetic_pool_fixture_complete"})").get();
}
void callback_reentry(PersistenceService& store){
    const std::string id="pool-callback";auto budget=root(store,id);NativeChildExecutor pool(1,4);
    const std::string objective="Explicit synthetic pool cancellation lifecycle task";
    DelegationBatchSpec spec;spec.id="pool-callback-batch";spec.parent_run_id=id;spec.provider_tool_call_id="synthetic-call";
    spec.expected_budget_revision=store.root_budget(id).get().revision;
    spec.arguments_json=Json{{"tasks",Json::array({{{"id","child"},{"objective",objective},{"preset","workspace.inspect"}}})}}.dump();
    spec.parent_assistant_json=Json{{"content","Explicit synthetic repository call metadata"},{"tool_calls",Json::array({{{"id",spec.provider_tool_call_id},{"name","delegate_tasks"},{"arguments",spec.arguments_json}}})}}.dump();
    spec.tasks.push_back({"child","pool-callback-node","pool-callback-child",Json{{"content",objective}}.dump(),objective});
    std::mutex mutex;std::condition_variable changed;bool entered=false;std::atomic<bool> callback_completed=false;
    auto batch=pool.stage({NativeChildWork{budget,[&](std::stop_token stop){
        store.transition("pool-callback-child",RunState::queued,RunState::running).get();
        std::stop_callback callback(stop,[&]{
            require(!pool.healthy(),"Close must revoke admission before requesting stop");
            pool.fail();callback_completed=true;changed.notify_all();
        });
        std::unique_lock lock(mutex);entered=true;changed.notify_all();
        require(changed.wait_for(lock,5s,[&]{return stop.stop_requested();}),"Native close must stop the actual dispatched closure");
        lock.unlock();
        return store.transition("pool-callback-child",RunState::running,RunState::cancelled,R"({"reason":"native_pool_stop_observed"})").get();
    },{}}});
    store.accept_delegation_batch(spec).get();batch->dispatch();
    {std::unique_lock lock(mutex);require(changed.wait_for(lock,5s,[&]{return entered;}),"Actual owned child closure must enter before close");}
    pool.close();
    const auto actual=batch->results()[0].get();
    require(callback_completed&&actual.id=="pool-callback-child"&&actual.state==RunState::cancelled,"A synchronous callback must re-enter healthy/fail without a pool mutex deadlock");
    store.settle_delegation_child(actual.id).get();store.settle_delegation_batch(spec.id).get();
    require(store.run_history(actual.id).get().size()==1&&store.operations(actual.id).get().empty(),"Native cancellation retains only the admitted prompt, with no provider or effect");
    store.transition(id,RunState::running,RunState::cancelled,R"({"reason":"synthetic_pool_fixture_complete"})").get();
}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    std::filesystem::path folder;
    try{
        std::random_device random;std::ostringstream suffix;suffix<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)suffix<<std::setw(8)<<random();
        folder=std::filesystem::temp_directory_path()/("xmind-child-pool-"+suffix.str());
        if(!std::filesystem::create_directory(folder))throw std::runtime_error("Native pool fixture directory already exists");
        bool recovery=false;{PersistenceService store((folder/"state.sqlite").string(),{argv[1],argv[2]});
            staged_retirement(store,false);staged_retirement(store,true);callback_reentry(store);stopped_without_slot(store,false);stopped_without_slot(store,true);recovery=fail_dispatch_race(store);store.close();}
        if(recovery){PersistenceService reopened((folder/"state.sqlite").string(),{argv[1],argv[2]});require(reopened.run("pool-race").get().state==RunState::failed&&reopened.run("pool-race-child").get().state==RunState::failed,"Actual reopen retires undispached accepted ownership without replay");reopened.close();}
        std::filesystem::remove_all(folder);
        std::cout<<"Native child pool contract passed: staged close/fault, synchronous stop callback re-entry, concurrent fail/dispatch ownership, stopped/deadline retirement without a leaf slot, actual SQLite cancellation/recovery. No provider was used.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
