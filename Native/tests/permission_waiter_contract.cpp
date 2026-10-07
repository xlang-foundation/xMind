#include "agentflow/permission_waiter.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <thread>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Action> void rejects(Action action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected rejection did not occur");
}
std::int64_t expiry() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;}
struct Directory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("xmind-permission-"+std::to_string(std::random_device{}()));
    Directory() {std::filesystem::create_directory(path);}
    ~Directory() {std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
struct WaitingTask {
    std::stop_source stop;
    std::future<Operation> result;
    WaitingTask(PermissionWaiter& waiter,std::string id,OperationSpec spec)
        :result(std::async(std::launch::async,[this,&waiter,id=std::move(id),spec=std::move(spec)]{return waiter.acquire(id,spec,expiry(),stop.get_token());})) {}
    ~WaitingTask() {stop.request_stop();if(result.valid()) result.wait();}
    Operation get() {return result.get();}
    std::future_status wait_for(std::chrono::milliseconds duration) {return result.wait_for(duration);}
};
void proposed(PersistenceService& store,const std::string& id) {
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline) {
        try {require(store.operation(id).get().state==OperationState::awaiting_approval,"Expected a durable pending proposal");return;}
        catch(const NotFound&) {std::this_thread::sleep_for(5ms);}
    }
    throw std::runtime_error("Permission proposal timed out");
}
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        Directory directory;const auto database=(directory.path/"state.sqlite").string();
        const std::vector<std::string> roots{argv[1],argv[2]};
        PersistenceService store(database,roots);PermissionWaiter waiter(store);
        store.create_session("owner","owner").get();store.start_prompt_run("owner","owner",R"({"content":"permission contract"})").get();store.transition("owner",RunState::queued,RunState::running).get();
        OperationSpec spec{"owner",directory.path.string(),"controlled_effect",R"({"path":"fixture.txt"})"};
        std::stop_source already_cancelled;already_cancelled.request_stop();
        rejects<PermissionCancelled>([&]{waiter.acquire("never-proposed",spec,expiry(),already_cancelled.get_token());});
        rejects<NotFound>([&]{store.operation("never-proposed").get();});
        WaitingTask denied(waiter,"denied",spec);
        proposed(store,"denied");require(denied.wait_for(20ms)==std::future_status::timeout,"Waiting must not imply permission");
        store.decide_operation("denied",OperationDecision::deny,"fixture-controller").get();
        rejects<PermissionDenied>([&]{denied.get();});
        require(store.operation("denied").get().state==OperationState::denied,"Denial must survive permission wait");
        WaitingTask allowed(waiter,"allowed",spec);
        proposed(store,"allowed");store.decide_operation("allowed",OperationDecision::allow,"fixture-controller").get();
        require(allowed.get().state==OperationState::executing,"Waiter must return a durably claimed exact approval");
        store.finish_operation("allowed",OperationState::failed,R"({"reason":"fixture_no_effect"})").get();
        WaitingTask cancelled(waiter,"cancelled",spec);
        proposed(store,"cancelled");cancelled.stop.request_stop();
        rejects<PermissionCancelled>([&]{cancelled.get();});
        require(store.operation("cancelled").get().state==OperationState::cancelled,"Wait cancellation must retire the pending permission");
        WaitingTask expired(waiter,"expired",spec);
        proposed(store,"expired");
        {XlangSqlite inject(database,roots);inject.execute("UPDATE operations SET expires_ms=1 WHERE id='expired'");}
        rejects<PermissionExpired>([&]{expired.get();});
        require(store.operation("expired").get().state==OperationState::expired,"Maintenance expiry must retire a pending permission");
        // Claim contention queues authorization only; no fixture tool is invoked.
        store.request_operation("occupant",spec,expiry()).get();store.decide_operation("occupant",OperationDecision::allow,"fixture-controller").get();store.claim_operation("occupant",spec).get();
        WaitingTask queued(waiter,"waiting-workspace",spec);
        proposed(store,"waiting-workspace");store.decide_operation("waiting-workspace",OperationDecision::allow,"fixture-controller").get();
        require(queued.wait_for(150ms)==std::future_status::timeout,"Busy workspace must not return a second effect claim");
        store.finish_operation("occupant",OperationState::failed,R"({"reason":"fixture_no_effect"})").get();
        require(queued.get().state==OperationState::executing,"After verified occupancy ends the granted claim may proceed");
        store.finish_operation("waiting-workspace",OperationState::uncertain,R"({"reason":"fixture_uncertainty"})").get();
        WaitingTask quarantined(waiter,"quarantined",spec);
        proposed(store,"quarantined");store.decide_operation("quarantined",OperationDecision::allow,"fixture-controller").get();
        rejects<WorkspaceEffectUncertain>([&]{quarantined.get();});
        require(store.operation("quarantined").get().state==OperationState::cancelled,"Uncertain workspace must retire a new unused grant");
        store.transition("owner",RunState::running,RunState::failed).get();store.close();
        std::cout<<"Native permission wait contracts passed: persisted decisions, denial, cancellation, expiry, one-shot claims and workspace exclusion. No product effect tool was executed.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
