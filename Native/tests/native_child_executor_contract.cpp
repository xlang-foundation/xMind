// Actual native pool and xlang3 SQLite; no model transport is used. Repository
// task metadata below is explicitly synthetic and does not claim inference.
#include "agentflow/native_child_executor.hpp"
#include "agentflow/persistence_service.hpp"
#include "nlohmann/json.hpp"
#include <atomic>
#include <array>
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
void yielded_slot_retirement(PersistenceService& store){
    const std::string id="pool-yielded";auto budget=root(store,id);require(budget->try_acquire_leaf()&&budget->yield_leaf(),"Nested scheduling must yield the caller's actual leaf slot");
    std::promise<void> slot_ready,release_signal;auto released=release_signal.get_future();std::thread blocker([&]{if(!budget->try_acquire_leaf()){slot_ready.set_exception(std::make_exception_ptr(std::runtime_error("Sibling could not acquire the yielded slot")));return;}slot_ready.set_value();released.wait();budget->release_leaf();});slot_ready.get_future().get();
    std::stop_source cancel;cancel.request_stop();rejects<TransportCancelled>([&]{budget->reacquire_leaf(cancel.get_token());},"A cancelled nested scheduler must not reacquire a leaf slot");release_signal.set_value();blocker.join();
    budget->release_leaf();require(budget->try_acquire_leaf(),"The child executor's retirement must consume a still-yielded lease without decrementing another leaf");budget->release_leaf();
    store.transition(id,RunState::running,RunState::cancelled,R"({"reason":"synthetic_pool_fixture_complete"})").get();
}
void transferred_slot(PersistenceService& store,bool retire_yielded){
    const std::string id=retire_yielded?"pool-transfer-retired":"pool-transfer";auto budget=root(store,id);NativeChildExecutor pool(2,4);
    std::promise<std::shared_ptr<NativeChildSlot>> bound;auto slot_ready=bound.get_future();std::promise<void> outer_entered,inner_entered,release_inner,resume_outer;auto outer_ready=outer_entered.get_future(),inner_ready=inner_entered.get_future(),inner_release=release_inner.get_future(),outer_resume=resume_outer.get_future();
    auto batch=pool.stage({
        NativeChildWork{budget,[&](std::stop_token){store.transition(id+"-child",RunState::queued,RunState::running).get();outer_entered.set_value();require(outer_resume.wait_for(2s)==std::future_status::ready,"Transferred parent slot must resume after descendant drain");return store.transition(id+"-child",RunState::running,RunState::cancelled,R"({"reason":"synthetic_transferred_parent_complete"})").get();},{},[&](auto slot){require(pool.healthy(),"Slot binding callback must re-enter pool observation without a lock deadlock");bound.set_value(std::move(slot));}},
        NativeChildWork{budget,[&](std::stop_token){store.transition(id+"-child2",RunState::queued,RunState::running).get();inner_entered.set_value();require(inner_release.wait_for(2s)==std::future_status::ready,"Transferred descendant must remain owned until released");return store.transition(id+"-child2",RunState::running,RunState::cancelled,R"({"reason":"synthetic_transferred_descendant_complete"})").get();},{}}
    });
    auto spec=admission(store,id);auto arguments=Json::parse(spec.arguments_json);auto second=arguments["tasks"][0];second["id"]="child2";arguments["tasks"].push_back(second);spec.arguments_json=arguments.dump();auto assistant=Json::parse(spec.parent_assistant_json);assistant["tool_calls"][0]["arguments"]=spec.arguments_json;spec.parent_assistant_json=assistant.dump();auto task=spec.tasks[0];task.task_id="child2";task.node_id=id+"-node2";task.child_run_id=id+"-child2";spec.tasks.push_back(task);store.accept_delegation_batch(spec).get();batch->dispatch();
    require(slot_ready.wait_for(2s)==std::future_status::ready&&outer_ready.wait_for(2s)==std::future_status::ready,"Actual pool job must publish its held native slot");auto slot=slot_ready.get();require(!budget->try_acquire_leaf(),"One parallel root must remain occupied before cross-thread yield");require(slot->yield()&&!slot->yield(),"Only one cross-thread yield may release the exact parent slot");require(inner_ready.wait_for(2s)==std::future_status::ready&&!budget->try_acquire_leaf(),"Descendant must use the released slot without oversubscription");std::stop_source stopped;stopped.request_stop();rejects<TransportCancelled>([&]{slot->reacquire(stopped.get_token());},"Cancelled callback cannot reacquire a transferred slot");if(retire_yielded){resume_outer.set_value();require(batch->results()[0].wait_for(2s)==std::future_status::ready&&!budget->try_acquire_leaf(),"Retiring a yielded parent must not release its descendant's occupied slot");release_inner.set_value();require(batch->results()[1].wait_for(2s)==std::future_status::ready,"Descendant must retain its slot until actual retirement");}else{release_inner.set_value();require(batch->results()[1].wait_for(2s)==std::future_status::ready,"Descendant must physically leave the pool before parent reacquisition");slot->reacquire();require(!budget->try_acquire_leaf(),"Reacquired parent must occupy the original root limit");resume_outer.set_value();require(batch->results()[0].wait_for(2s)==std::future_status::ready,"Parent job must retire after its actual resume");}require(!slot->yield(),"Retained native slot must refuse yield after pool retirement");rejects<Conflict>([&]{slot->reacquire();},"Retained slot cannot reacquire after retirement");pool.close();require(budget->try_acquire_leaf(),"Transferred slot retirement must release exactly one root slot");budget->release_leaf();
    for(const auto& result:batch->results())store.settle_delegation_child(result.get().id).get();store.settle_delegation_batch(spec.id).get();store.transition(id,RunState::running,RunState::cancelled,R"({"reason":"synthetic_pool_fixture_complete"})").get();
}
void assisted_saturation(PersistenceService& store){
    // Pool-only closures reproduce eight parked owners. The callback threads
    // stand in for existing SDK receiver threads, never additional pool workers.
    std::vector<std::shared_ptr<RootExecutionBudget>> budgets;
    for(unsigned i=0;i<8;++i)budgets.push_back(root(store,"pool-assist-"+std::to_string(i)));
    NativeChildExecutor pool(8,64),foreign(1,4);
    std::array<std::promise<std::shared_ptr<NativeChildSlot>>,8> bound;
    std::array<std::shared_ptr<NativeChildSlot>,8> slots;
    std::array<std::atomic<unsigned>,8> completed{};
    std::atomic<unsigned> entered=0;std::atomic<bool> release_children=false,release_parents=false,callbacks_ready=false;
    std::array<std::thread::id,8> callback_ids;
    const auto until=std::chrono::steady_clock::now()+5s;
    std::vector<NativeChildWork> owners;
    for(unsigned i=0;i<8;++i)owners.push_back({budgets[i],[&,i](std::stop_token stop){
        while(!release_parents&&!stop.stop_requested()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);
        require(release_parents||stop.stop_requested(),"Parked owner must be explicitly resumed or cancelled");
        Run result;result.id="synthetic-assisted-owner-"+std::to_string(i);result.state=RunState::cancelled;return result;
    },{},[&,i](auto slot){bound[i].set_value(std::move(slot));}});
    auto parents=pool.stage(std::move(owners));parents->dispatch();
    for(unsigned i=0;i<8;++i){auto ready=bound[i].get_future();require(ready.wait_until(until)==std::future_status::ready,"All eight physical workers must hold an exact slot");slots[i]=ready.get();}
    rejects<Conflict>([&]{pool.assist(slots[0]);},"A held slot cannot lend its callback thread");
    for(const auto& slot:slots)require(slot->yield(),"Each parked owner must yield exactly one leaf");
    rejects<Conflict>([&]{foreign.assist(slots[0]);},"Another pool cannot use a captured slot");
    std::vector<std::unique_ptr<NativeChildExecutor::Batch>> batches;
    for(unsigned n=0;n<8;++n){std::vector<NativeChildWork> children;
        for(unsigned i=0;i<8;++i)children.push_back({budgets[i],[&,i,n](std::stop_token stop){
            require(std::this_thread::get_id()==callback_ids[i],"Assistance must execute on its exact root callback thread");
            if(n==0){++entered;while(!release_children&&!stop.stop_requested()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);}
            require(!stop.stop_requested()&&std::chrono::steady_clock::now()<until,"Assisted work must progress before its actual deadline");
            ++completed[i];Run result;result.id="synthetic-assisted-child-"+std::to_string(i)+"-"+std::to_string(n);result.state=RunState::completed;return result;
        },{}});
        batches.push_back(pool.stage(std::move(children)));batches.back()->dispatch();
    }
    std::array<std::exception_ptr,8> failures{};std::vector<std::jthread> callbacks;
    for(unsigned i=0;i<8;++i){callbacks.emplace_back([&,i]{try{while(!callbacks_ready)std::this_thread::yield();while(completed[i]!=8&&std::chrono::steady_clock::now()<until){if(!pool.assist(slots[i]))std::this_thread::sleep_for(1ms);}}catch(...){failures[i]=std::current_exception();}});callback_ids[i]=callbacks.back().get_id();}
    callbacks_ready=true;
    while(entered!=8&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);
    require(entered==8,"All eight parked callbacks must execute descendants without another pool thread");
    rejects<Conflict>([&]{pool.assist(slots[0]);},"One slot cannot lend concurrent callback threads");
    rejects<Conflict>([&]{slots[0]->reacquire();},"A parent cannot reacquire while its callback owns a descendant");
    rejects<NativeChildCapacityUnavailable>([&]{pool.stage({NativeChildWork{budgets[0],[](std::stop_token){return Run{};},{}}});},"Assistance must retain the72-job global bound even after leaving the queue");
    release_children=true;callbacks.clear();
    for(unsigned i=0;i<8;++i){if(failures[i])std::rethrow_exception(failures[i]);require(completed[i]==8,"A callback may help only its exact shared budget");slots[i]->reacquire();require(!budgets[i]->try_acquire_leaf(),"Resumed owner must retain its single root leaf limit");}
    release_parents=true;
    for(const auto& result:parents->results())require(result.wait_until(until)==std::future_status::ready,"Resumed parent must physically retire");
    for(const auto& batch:batches)for(const auto& result:batch->results())require(result.get().state==RunState::completed,"Every assisted result must be observed exactly once");
    pool.close();foreign.close();
    rejects<Conflict>([&]{pool.assist(slots[0]);},"Retained retired slot cannot lend execution");
    for(unsigned i=0;i<8;++i){require(budgets[i]->try_acquire_leaf(),"Assisted retirement must release exactly the original leaf");budgets[i]->release_leaf();store.transition("pool-assist-"+std::to_string(i),RunState::running,RunState::cancelled,R"({"reason":"synthetic_pool_assistance_complete"})").get();}
}
void assisted_stop(PersistenceService& store,bool fault){
    const std::string id=fault?"pool-assist-fault":"pool-assist-close";auto budget=root(store,id);NativeChildExecutor pool(1,4);
    std::promise<std::shared_ptr<NativeChildSlot>> bound;auto slot_ready=bound.get_future();
    std::promise<void> entered,stopped;auto child_entered=entered.get_future(),child_stopped=stopped.get_future();
    std::atomic<bool> release_child=false;const auto until=std::chrono::steady_clock::now()+5s;
    auto parent=pool.stage({NativeChildWork{budget,[&](std::stop_token stop){
        while(!stop.stop_requested()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);
        require(stop.stop_requested(),"Closing pool must stop its parked owner");Run r;r.state=RunState::cancelled;return r;
    },{},[&](auto slot){bound.set_value(std::move(slot));}}});parent->dispatch();
    require(slot_ready.wait_until(until)==std::future_status::ready,"Assisted stop requires an actual parked pool owner");auto slot=slot_ready.get();require(slot->yield(),"Assisted stop must release the parent leaf");
    auto child=pool.stage({NativeChildWork{budget,[&](std::stop_token stop){
        entered.set_value();while(!stop.stop_requested()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);
        require(stop.stop_requested(),"Close/fault must retain cancellation of the assisted job");stopped.set_value();
        while(!release_child&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);
        require(release_child,"Close must wait for physical assisted child drain");Run r;r.state=RunState::cancelled;return r;
    },{}}});child->dispatch();
    std::exception_ptr failure;std::jthread callback([&]{try{require(pool.assist(slot),"Yielded callback must take its already-dispatched child");}catch(...){failure=std::current_exception();}});
    require(child_entered.wait_until(until)==std::future_status::ready,"Assisted child must be physically live before close/fault");
    if(fault)pool.fail();auto closing=std::async(std::launch::async,[&]{pool.close();});
    require(child_stopped.wait_until(until)==std::future_status::ready&&parent->results()[0].wait_until(until)==std::future_status::ready,"Both parked owner and assisted child must observe stop");
    require(closing.wait_for(0ms)!=std::future_status::ready,"Pool close cannot return after only the parent has retired");
    release_child=true;callback.join();closing.get();if(failure)std::rethrow_exception(failure);
    require(child->results()[0].get().state==RunState::cancelled&&!slot->yield(),"Stopped assisted job and parent retain exact physical retirement");
    require(budget->try_acquire_leaf(),"Retired yielded parent cannot release the assisted child's leaf early or twice");budget->release_leaf();
    store.transition(id,RunState::running,RunState::cancelled,R"({"reason":"synthetic_assisted_stop_complete"})").get();
}
void saturated_stop(PersistenceService& store){
    // Pool-only synthetic closures exercise the complete stop snapshot. They
    // create no task admission, inference or effect receipts.
    std::vector<std::shared_ptr<RootExecutionBudget>> budgets;for(int i=0;i<8;++i)budgets.push_back(root(store,"pool-saturated-"+std::to_string(i)));NativeChildExecutor pool(8,64);std::atomic<unsigned> entered=0,retired=0;
    auto work=[&](unsigned index){return NativeChildWork{budgets[index%8],[&,index](std::stop_token stop){if(index<8){++entered;const auto until=std::chrono::steady_clock::now()+5s;while(!stop.stop_requested()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);}require(stop.stop_requested(),"Saturated stop must cancel every executing and queued closure");++retired;Run result;result.id="synthetic-pool-stop-"+std::to_string(index);result.state=RunState::cancelled;return result;},{}};};
    std::vector<std::unique_ptr<NativeChildExecutor::Batch>> batches;std::vector<NativeChildWork> first;for(unsigned i=0;i<8;++i)first.push_back(work(i));batches.push_back(pool.stage(std::move(first)));batches.back()->dispatch();const auto until=std::chrono::steady_clock::now()+2s;while(entered!=8&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);require(entered==8,"All eight workers must be occupied before filling the queue");for(unsigned n=0;n<8;++n){std::vector<NativeChildWork> pending;for(unsigned j=0;j<8;++j)pending.push_back(work(8+n*8+j));batches.push_back(pool.stage(std::move(pending)));batches.back()->dispatch();}pool.fail();pool.close();for(const auto& batch:batches)for(const auto& result:batch->results())require(result.get().state==RunState::cancelled,"Each saturated stop promise must have an actual closure result");require(retired==72,"Stop snapshot must retain all eight executing and64 queued closures");for(int i=0;i<8;++i)store.transition("pool-saturated-"+std::to_string(i),RunState::running,RunState::cancelled,R"({"reason":"synthetic_pool_saturation_complete"})").get();
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
            staged_retirement(store,false);staged_retirement(store,true);callback_reentry(store);stopped_without_slot(store,false);stopped_without_slot(store,true);yielded_slot_retirement(store);transferred_slot(store,false);transferred_slot(store,true);assisted_saturation(store);assisted_stop(store,false);assisted_stop(store,true);saturated_stop(store);recovery=fail_dispatch_race(store);store.close();}
        if(recovery){PersistenceService reopened((folder/"state.sqlite").string(),{argv[1],argv[2]});require(reopened.run("pool-race").get().state==RunState::failed&&reopened.run("pool-race-child").get().state==RunState::failed,"Actual reopen retires undispached accepted ownership without replay");reopened.close();}
        std::filesystem::remove_all(folder);
        std::cout<<"Native child pool contract passed: staged close/fault, synchronous stop callback re-entry, concurrent fail/dispatch ownership, stopped/deadline retirement without a leaf slot, actual SQLite cancellation/recovery. No provider was used.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
