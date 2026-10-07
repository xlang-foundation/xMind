// Test driver only: submits real native edit proposals without a model. The
// product HTTP server, approval waiter, file adapter and persistence are used.
#include "agentflow/http_server.hpp"
#include "agentflow/edit_executor.hpp"
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
        for(const auto* run:{"run-allow","run-deny","run-stale"}) {
            store.create_session(run,"HTTP effect fixture").get();
            store.start_prompt_run(run,run,R"({"content":"native approval HTTP contract, no model"})").get();
            store.transition(run,RunState::queued,RunState::running).get();
        }
        HttpServer server(store,token);const auto port=server.bind(0);Serving serving(server);
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
