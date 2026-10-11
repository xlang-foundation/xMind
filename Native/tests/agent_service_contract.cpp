#include "agentflow/agent_service.hpp"
#include "agentflow/agent_definitions.hpp"
#include <iostream>
#include <thread>
#include <filesystem>

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
        const auto run=store.run(id).get();if(run.state==RunState::failed){const auto events=store.events(id).get();const auto failure=std::find_if(events.rbegin(),events.rend(),[](const auto& event){return event.kind=="run.failed";});throw std::runtime_error("Actual provider stream failed: "+(failure==events.rend()?std::string("unknown"):failure->json));}
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
        AgentDefinitionStore definitions(store);const auto imported=definitions.apply("agents:\n  - id: service.profile\n    model_id: synthetic-service-protocol\n    instructions: Preserve the exact marker service-profile-instructions in your reasoning.\n    tools:\n      - read_file\n");
        require(imported.entries.size()==1&&imported.entries[0].revision==1&&imported.entries[0].tools&&*imported.entries[0].tools==std::vector<std::string>{"read_file"},"Named agent catalogue must import and persist its tool allowlist");
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
            store.create_session("profile_snapshot","profile snapshot").get();
            const auto catalogue=repeated.agent_definitions();require(catalogue.size()==1&&catalogue[0].id=="service.profile","Agent service must expose safe catalogue metadata");
            const auto detail=repeated.agent_definition("service.profile");require(detail.instructions.find("service-profile-instructions")!=std::string::npos&&detail.model_id=="synthetic-service-protocol"&&detail.tools&&*detail.tools==std::vector<std::string>{"read_file"},"Agent service must return the persisted definition and bounded tool allowlist");
            rejects<NotFound>([&]{repeated.agent_definition("missing-agent");});
            const auto selected=repeated.select_session_agent("profile_snapshot",catalogue[0],0);require(selected.selected&&selected.selected->id=="service.profile"&&selected.revision==1&&selected.editable,"Conversation must pin the selected named agent revision");
            const auto saved_selection=store.session_agent("profile_snapshot").get();require(saved_selection.selected&&saved_selection.selected->instructions.find("service-profile-instructions")!=std::string::npos&&saved_selection.selected->tools&&*saved_selection.selected->tools==std::vector<std::string>{"read_file"},"Selected agent instructions and tool bindings must survive session snapshot persistence");
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
