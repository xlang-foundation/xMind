#include "agentflow/edit_executor.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>
using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Action> void rejects(Action action) {try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected rejection did not occur");}
struct Directory {
    std::filesystem::path parent=std::filesystem::temp_directory_path();
    std::filesystem::path path=parent/("xmind-edit-"+std::to_string(std::random_device{}()));
    Directory() {require(std::filesystem::create_directory(path),"Private fixture directory must be newly created");}
    ~Directory() {if(path.parent_path()==parent && path.filename().string().starts_with("xmind-edit-")) {std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
std::int64_t expiry() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;}
struct Task {
    std::stop_source stop;
    std::future<WorkspaceSnapshot> result;
    Task(EditExecutor& executor,const std::string& id,WorkspaceEditPlan plan,std::string run="run"):result(std::async(std::launch::async,[this,&executor,id,run=std::move(run),plan=std::move(plan)]() mutable {return executor.execute(id,run,std::move(plan),expiry(),stop.get_token());})) {}
    ~Task() {stop.request_stop();if(result.valid()) result.wait();}
};
Operation proposed(PersistenceService& store,const std::string& id) {
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline) {
        try {auto value=store.operation(id).get();require(value.state==OperationState::awaiting_approval,"Proposal must wait for controller decision");return value;}
        catch(const NotFound&) {std::this_thread::sleep_for(5ms);}
    }
    throw std::runtime_error("Proposal timed out");
}
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        Directory directory;const auto root=directory.path/"workspace";std::filesystem::create_directory(root);
        const auto file=root/"actual.txt";
        auto write=[&](const std::string& bytes) {std::ofstream output(file,std::ios::binary|std::ios::trunc);output<<bytes;output.close();require(bool(output),"Actual fixture write failed");};
        write("original\n");
        const auto database=(directory.path/"state.sqlite").string();const std::vector<std::string> roots{argv[1],argv[2]};
        WorkspaceTools workspace(root.string());
        {
            PersistenceService store(database,roots);EditExecutor executor(store,workspace);
            store.create_session("session","edit contract").get();store.start_prompt_run("run","session",R"({"content":"edit contract"})").get();store.transition("run",RunState::queued,RunState::running).get();
            const auto plan=workspace.plan_replacement("actual.txt","original","native change");
            Task denied(executor,"denied",plan);proposed(store,"denied");
            require(workspace.read_file("actual.txt").content=="original\n","Proposal must not apply edits");
            store.decide_operation("denied",OperationDecision::deny,"test-controller").get();
            rejects<PermissionDenied>([&]{denied.result.get();});
            Task stale(executor,"stale",plan);proposed(store,"stale");write("outside writer\n");
            store.decide_operation("stale",OperationDecision::allow,"test-controller").get();
            rejects<ToolContentConflict>([&]{stale.result.get();});
            require(store.operation("stale").get().state==OperationState::failed && workspace.read_file("actual.txt").content=="outside writer\n","Approved stale edit must record no-effect failure and preserve external bytes");
            write("original\n");
            Task cancelled(executor,"cancelled",plan);proposed(store,"cancelled");cancelled.stop.request_stop();
            rejects<PermissionCancelled>([&]{cancelled.result.get();});
            Task allowed(executor,"allowed",plan);const auto proposal=proposed(store,"allowed");
            const auto payload=nlohmann::json::parse(proposal.spec.arguments_json);
            require(payload["before_content"]==plan.before.content && payload["after_content"]==plan.after_content && payload["file_id"]==plan.before.file_id,"Durable review must contain exact actual before/after plan");
            require(allowed.result.wait_for(20ms)==std::future_status::timeout,"Waiting cannot imply approval");
            store.decide_operation("allowed",OperationDecision::allow,"test-controller").get();
            const auto actual=allowed.result.get();const auto outcome=store.operation("allowed").get();
            require(actual.content=="native change\n" && workspace.read_file("actual.txt").content==actual.content,"Approved executor must actually change file bytes");
            require(outcome.state==OperationState::succeeded && outcome.decision_actor=="test-controller","Actual effect must be durably attributed and completed");
            require(nlohmann::json::parse(outcome.result_json)["after_sha256"]==workspace.snapshot_file("actual.txt").content_sha256,"Outcome must match actual filesystem hash");
            rejects<Conflict>([&]{executor.execute("allowed","run",plan,expiry());});
            require(workspace.read_file("actual.txt").content==actual.content,"Duplicate operation must not repeat effect");
            store.transition("run",RunState::running,RunState::completed).get();
            store.start_prompt_run("journal-fault-run","session",R"({"content":"journal fault contract"})").get();store.transition("journal-fault-run",RunState::queued,RunState::running).get();
            // Fixture-only storage fault at the real durable outcome boundary.
            // The filesystem action remains the actual product implementation.
            {XlangSqlite inject(database,roots);inject.execute("CREATE TRIGGER reject_edit_success BEFORE UPDATE OF state ON operations WHEN NEW.id='journal-fault' AND NEW.state='succeeded' BEGIN SELECT RAISE(ABORT,'fixture journal fault'); END");}
            const auto fault_plan=workspace.plan_replacement("actual.txt","native change","actual effect before journal fault");
            Task fault(executor,"journal-fault",fault_plan,"journal-fault-run");proposed(store,"journal-fault");
            store.decide_operation("journal-fault",OperationDecision::allow,"test-controller").get();
            rejects<EditOutcomeUnrecorded>([&]{fault.result.get();});
            require(workspace.read_file("actual.txt").content==fault_plan.after_content,"Journal fault must not conceal an actual file effect");
            require(store.operation("journal-fault").get().state==OperationState::executing,"Failed outcome persistence must preserve the claimed operation for recovery");
            store.close();
        }
        {
            PersistenceService reopened(database,roots);
            require(reopened.operation("allowed").get().state==OperationState::succeeded,"Actual outcome must survive backend reopen");
            require(reopened.operation("cancelled").get().state==OperationState::cancelled,"Unused cancelled proposal must remain retired");
            require(workspace.read_file("actual.txt").content=="actual effect before journal fault\n","Reopening must not replay an effect");
            require(reopened.operation("journal-fault").get().state==OperationState::uncertain,"Restart must quarantine an effect whose outcome was not durably recorded");
            require(reopened.run("journal-fault-run").get().state==RunState::failed,"Interrupted owner must be recovered without inventing success");
            reopened.close();
        }
        std::cout<<"Approved native edit executor passed with actual files and embedded xlang3 persistence; no model or public approval route was used\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
