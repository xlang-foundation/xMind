#include "agentflow/agent_service.hpp"
#include <iostream>
#include <thread>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Action> void rejects(Action action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected rejection did not occur");
}
void streaming(PersistenceService& store,const std::string& id) {
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline) {
        for(const auto& event:store.events(id).get()) if(event.kind=="model.text") return;
        std::this_thread::sleep_for(10ms);
    }
    throw std::runtime_error("Actual provider stream did not start");
}
void cancelled(PersistenceService& store,const std::string& id) {
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline) {
        if(store.run(id).get().state==RunState::cancelled)return;
        std::this_thread::sleep_for(10ms);
    }
    throw std::runtime_error("Actual held provider stream did not retire after controller cancellation");
}
}
int main(int argc,char** argv) {
    if(argc!=5) return 2;
    try {
        PersistenceService store(argv[1],{argv[2],argv[3]});
        AgentSettings settings;settings.provider.endpoint=argv[4];settings.provider.model="synthetic-service-protocol";
        for(const auto* id:{"a","b","queued","rejected"}) store.create_session(id,id).get();
        AgentService service(store,settings,2,1);
        require(service.healthy(),"New executor must accept work");
        service.submit("a","a","hold-stream");streaming(store,"a");
        service.submit("b","b","hold-stream");streaming(store,"b");
        require(store.run("a").get().state==RunState::running && store.run("b").get().state==RunState::running,"Independent sessions must execute concurrently");
        service.submit("queued","queued","hold-stream");
        rejects<RunBusy>([&]{service.submit("rejected","rejected","must not persist");});
        rejects<NotFound>([&]{store.run("rejected").get();});
        require(store.history("rejected").get().empty(),"Capacity rejection must not persist prompt");
        service.close();
        require(!service.healthy(),"Closed executor must stop admission");
        for(const auto* id:{"a","b","queued"}) require(store.run(id).get().state==RunState::cancelled,"Shutdown must cancel actual workers and queued jobs before returning");
        rejects<RunUnavailable>([&]{service.submit("rejected","rejected","must not persist");});
        service.close(); // Idempotent joined shutdown, before persistence teardown.
        {
            AgentService repeated(store,settings,1,1);
            for(int cycle=0;cycle<4;++cycle) {
                const auto id="notifier_"+std::to_string(cycle);store.create_session(id,id).get();
                // Exercise ordinary submissions while the independent expiry
                // loop has crossed a tick. Actual model.text and cancellation,
                // rather than elapsed time alone, establish dispatch/retirement.
                std::this_thread::sleep_for(120ms);
                repeated.submit(id,id,"hold-stream");streaming(store,id);
                require(store.run(id).get().state==RunState::running,"Repeated notifier wake must start its actual provider stream");
                repeated.cancel(id);cancelled(store,id);
                const auto rows=store.run_history(id).get();
                require(rows.size()==1&&rows[0].role=="user","Cancelled held stream must not acquire a fabricated final response");
            }
            repeated.close();require(!repeated.healthy(),"Repeated notifier service must join both worker and expiry waiters");
        }
        store.close();
        std::cout<<"Native agent service contract passed: parallel native streams, bounded admission, four repeated actual dispatch/cancel cycles across expiry ticks and joined cancellation. Inference peer is synthetic.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
