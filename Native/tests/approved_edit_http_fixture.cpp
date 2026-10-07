// Test driver only: submits real native edit proposals without a model. The
// product HTTP server, approval waiter, file adapter and persistence are used.
#include "agentflow/http_server.hpp"
#include "agentflow/edit_executor.hpp"
#include "nlohmann/json.hpp"
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace agentflow;
namespace {
struct Serving {
    HttpServer& server;std::thread worker;
    Serving(HttpServer& value):server(value),worker([&value]{value.listen();}) {}
    ~Serving() {server.stop();worker.join();}
};
struct Editing {
    std::stop_source stop;std::future<void> result;
    Editing(EditExecutor& executor,std::string id,std::string run,WorkspaceEditPlan plan,OperationState expected)
        :result(std::async(std::launch::async,[this,&executor,id,run,plan=std::move(plan),expected]() mutable {
            const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;
            try {executor.execute(id,run,std::move(plan),expiry,stop.get_token());if(expected!=OperationState::succeeded) throw std::runtime_error("Unexpected successful effect");}
            catch(const PermissionDenied&) {if(expected!=OperationState::denied) throw;}
            catch(const ToolContentConflict&) {if(expected!=OperationState::failed) throw;}
            catch(const PermissionCancelled&) {if(!stop.stop_requested()) throw;}
        })) {}
    ~Editing() {stop.request_stop();if(result.valid()) result.wait();}
};
}
int main(int argc,char** argv) {
    if(argc!=5) return 2;
    try {
        const auto* token=std::getenv("XMIND_AUTH_TOKEN");if(!token) return 2;
        PersistenceService store(std::string(argv[1])+"/state.sqlite",{argv[3],argv[4]});
        WorkspaceTools workspace(argv[2]);EditExecutor executor(store,workspace);
        WorkspaceTools recovery_workspace(std::string(argv[1])+"/recovery-workspace");
        EditExecutor inspector(store,recovery_workspace);
        for(const auto* run:{"run-allow","run-deny","run-stale"}) {
            store.create_session(run,"HTTP effect fixture").get();
            store.start_prompt_run(run,run,R"({"content":"native approval HTTP contract, no model"})").get();
            store.transition(run,RunState::queued,RunState::running).get();
        }
        // Explicit fixture journal state, not a simulated product execution.
        // Actual post-effect journal fault/restart is covered separately by
        // edit_executor_contract. This fixture verifies read-only HTTP/CLI access.
        store.create_session("recovery","Inspection fixture").get();
        store.start_prompt_run("run-recovery","recovery",R"({"content":"inspection fixture"})").get();
        store.transition("run-recovery",RunState::queued,RunState::running).get();
        const auto recovery_plan=recovery_workspace.plan_replacement("recovery.txt","original","planned change");
        const auto& before=recovery_plan.before;
        OperationSpec recovery_spec{"run-recovery",recovery_workspace.identity(),"replace_file",nlohmann::json{
            {"path",before.path},{"file_id",before.file_id},{"before_sha256",before.content_sha256},
            {"after_sha256",recovery_plan.after_sha256},{"before_content",before.content},{"after_content",recovery_plan.after_content}}.dump()};
        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;
        store.request_operation("uncertain-edit",recovery_spec,expiry).get();
        store.decide_operation("uncertain-edit",OperationDecision::allow,"fixture-controller").get();
        store.claim_operation("uncertain-edit",recovery_spec).get();
        store.finish_operation("uncertain-edit",OperationState::uncertain,R"({"fixture":"inspection-only"})").get();
        store.transition("run-recovery",RunState::running,RunState::failed).get();
        HttpServer server(store,token,nullptr,&inspector);const auto port=server.bind(0);Serving serving(server);
        Editing allowed(executor,"allow-edit","run-allow",workspace.plan_replacement("allowed.txt","original","approved native"),OperationState::succeeded);
        Editing denied(executor,"deny-edit","run-deny",workspace.plan_replacement("denied.txt","original","must not change"),OperationState::denied);
        Editing stale(executor,"stale-edit","run-stale",workspace.plan_replacement("stale.txt","original","must not overwrite"),OperationState::failed);
        std::cout<<"fixture-port "<<port<<std::endl;
        std::string command;std::getline(std::cin,command);
        if(command!="done") throw std::runtime_error("Fixture interrupted");
        allowed.result.get();denied.result.get();stale.result.get();
        if(store.operation("allow-edit").get().state!=OperationState::succeeded || store.operation("deny-edit").get().state!=OperationState::denied || store.operation("stale-edit").get().state!=OperationState::failed) throw std::runtime_error("Actual durable outcomes differ");
        std::cout<<"Real native approved edits verified through HTTP and CLI; no model was called"<<std::endl;return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
