#include "agentflow/agent_service.hpp"
#include "agentflow/agent_definitions.hpp"
#include "agentflow/graph_service.hpp"
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
        AgentDefinitionStore definitions(store);const auto imported=definitions.apply("agents:\n  - id: service.profile\n    model_id: synthetic-service-protocol\n    instructions: Preserve the exact marker service-profile-instructions in your reasoning.\n    tools:\n      - read_file\n    permissions:\n      - action: read_file\n        resource: README.md\n        effect: deny\n  - id: service.default\n    default: true\n    model_id: synthetic-service-protocol\n    instructions: Preserve the exact marker service-default-instructions in your reasoning.\n    tools: []\n  - id: service.subagent\n    mode: subagent\n    instructions: Run only as a delegated child.\n");
        require(imported.entries.size()==3&&imported.entries[0].revision==1&&imported.entries[0].tools&&*imported.entries[0].tools==std::vector<std::string>{"read_file"}&&imported.entries[0].permissions==std::vector<AgentPermissionRule>{{"read_file","README.md","deny"}}&&imported.entries[1].tools&&imported.entries[1].tools->empty()&&imported.entries[1].is_default,"Named agent catalogue must persist bounded tools, permission rules and default profile");
        rejects<std::invalid_argument>([&]{definitions.apply("agents:\n  - id: first\n    default: true\n    instructions: First profile.\n  - id: second\n    default: true\n    instructions: Second profile.\n");});
        rejects<std::invalid_argument>([&]{definitions.apply("agents:\n  - id: hidden.default\n    default: true\n    hidden: true\n    instructions: A default cannot be hidden.\n");});
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
            const auto catalogue=repeated.agent_definitions();require(catalogue.size()==3&&catalogue[0].id=="service.profile"&&catalogue[0].mode=="all"&&!catalogue[0].hidden&&!catalogue[0].is_default&&catalogue[0].permissions==std::vector<AgentPermissionRule>{{"read_file","README.md","deny"}}&&catalogue[1].id=="service.default"&&catalogue[1].is_default&&catalogue[2].mode=="subagent","Agent service must expose ordered permissions alongside mode, visibility and default metadata");
            const auto detail=repeated.agent_definition("service.profile");require(detail.instructions.find("service-profile-instructions")!=std::string::npos&&detail.model_id=="synthetic-service-protocol"&&detail.tools&&*detail.tools==std::vector<std::string>{"read_file"},"Agent service must return the persisted definition and bounded tool allowlist");
            rejects<NotFound>([&]{repeated.agent_definition("missing-agent");});
            store.create_session("default_profile","default profile selection").get();
            repeated.submit("default_profile","default_profile","hold-stream");streaming(store,"default_profile");
            const auto automatic=store.session_agent("default_profile").get();require(automatic.selected&&automatic.selected->id=="service.default"&&automatic.selected->is_default&&automatic.revision==1,"A new conversation must durably pin the configured default agent before inference");
            repeated.cancel("default_profile");cancelled(store,"default_profile");
            const auto selected=repeated.select_session_agent("profile_snapshot",catalogue[0],0);require(selected.selected&&selected.selected->id=="service.profile"&&selected.revision==1&&selected.editable,"Conversation must pin the selected named agent revision");
            const auto saved_selection=store.session_agent("profile_snapshot").get();require(saved_selection.selected&&saved_selection.selected->instructions.find("service-profile-instructions")!=std::string::npos&&saved_selection.selected->tools&&*saved_selection.selected->tools==std::vector<std::string>{"read_file"}&&saved_selection.selected->permissions==std::vector<AgentPermissionRule>{{"read_file","README.md","deny"}}&&saved_selection.selected->mode=="all"&&!saved_selection.selected->hidden&&!saved_selection.selected->is_default,"Selected agent instructions, permissions, mode, visibility, default flag and tool bindings must survive session snapshot persistence");
            rejects<std::invalid_argument>([&]{repeated.select_session_agent("profile_snapshot",catalogue[2],1);});
            const auto mode_revision=definitions.apply("agents:\n  - id: service.profile\n    mode: primary\n    model_id: synthetic-service-protocol\n    instructions: Preserve the exact marker service-profile-instructions in your reasoning.\n    tools: [read_file]\n  - id: service.subagent\n    mode: subagent\n    instructions: Run only as a delegated child.\n");require(mode_revision.entries[0].revision==2&&store.session_agent("profile_snapshot").get().selected->revision==1,"Changing an agent mode revises the catalogue entry while existing session snapshots retain their original mode and revision");
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
        {
            const auto budget=std::make_shared<RootAdmissionPool>(1);
            const auto graphs=GraphCatalogStore(store).apply(R"({"graphs":[{"id":"shared.wait","spec":{"nodes":[{"id":"human","type":"human","prompt":"Wait for a controller answer"}]}}]})");
            store.create_session("shared-agent","Agent sharing the root budget").get();
            store.create_session("shared-graph","Graph sharing the root budget").get();
            AgentService shared_agent(store,settings,1,1,budget);
            GraphService shared_graph(store,settings,1,1,2,budget);
            shared_graph.submit_graph("shared-graph-root","shared-graph","shared.wait",graphs.entries.at(0).revision,"Pause for controller input");
            const auto pause_deadline=std::chrono::steady_clock::now()+5s;
            while(store.run("shared-graph-root").get().state!=RunState::paused&&std::chrono::steady_clock::now()<pause_deadline)std::this_thread::sleep_for(10ms);
            require(store.run("shared-graph-root").get().state==RunState::paused&&budget->occupied()==1,"A waiting graph must retain one backend-wide root reservation");
            rejects<RunBusy>([&]{shared_agent.submit("agent-blocked-by-graph","shared-agent","hold-stream");});
            require(store.history("shared-agent").get().empty(),"A graph-owned root slot must reject an agent before storing its prompt");
            auto paused=store.graph_run("shared-graph-root").get();
            shared_graph.human_input("shared-graph-root","human",R"({"answer":"ready"})","fixture-controller",paused.checkpoint_revision);
            const auto finish_deadline=std::chrono::steady_clock::now()+5s;
            while((store.run("shared-graph-root").get().state!=RunState::completed||!shared_graph.idle())&&std::chrono::steady_clock::now()<finish_deadline)std::this_thread::sleep_for(10ms);
            require(store.run("shared-graph-root").get().state==RunState::completed&&budget->occupied()==0,"A terminal graph must release its shared root reservation");
            shared_agent.submit("shared-agent-root","shared-agent","hold-stream");streaming(store,"shared-agent-root");
            const auto graph_history_before_rejection=store.history("shared-graph").get().size();
            rejects<RunBusy>([&]{shared_graph.submit_graph("graph-blocked-by-agent","shared-graph","shared.wait",graphs.entries.at(0).revision,"Must not persist while the agent owns capacity");});
            rejects<NotFound>([&]{store.run("graph-blocked-by-agent").get();});
            require(store.history("shared-graph").get().size()==graph_history_before_rejection,"An agent-owned root slot must reject a graph before storing its prompt");
            shared_agent.cancel("shared-agent-root");cancelled(store,"shared-agent-root");
            const auto release_deadline=std::chrono::steady_clock::now()+5s;
            while((!shared_agent.idle()||budget->occupied()!=0)&&std::chrono::steady_clock::now()<release_deadline)std::this_thread::sleep_for(10ms);
            require(shared_agent.idle()&&budget->occupied()==0,"A cancelled agent must release the shared root reservation");
            shared_graph.close();shared_agent.close();
        }
        store.close();
        std::cout<<"Native agent service contract passed: parallel native streams, cross-service agent/graph capacity isolation across queued, active and paused roots, four repeated actual dispatch/cancel cycles across expiry ticks and joined cancellation. Inference peer is synthetic.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
