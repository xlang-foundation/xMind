#include "agentflow/agent_worker_supervisor.hpp"
namespace worker_supervisor_fixture {
using namespace agentflow;using Json=nlohmann::json;using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
using worker_read_fixture::check;using worker_read_fixture::rejects;using worker_read_fixture::write;
inline void parent(PersistenceService& store,const std::shared_ptr<const BoundProcessExecutable>& image,const std::string& endpoint,const std::filesystem::path& folder){
    const auto directory=folder/"product_supervised";check(std::filesystem::create_directory(directory),"Supervisor needs a fresh workspace");
    write(directory/"AGENTS.md","SUPERVISOR_GUIDANCE\n");WorkspaceTools workspace(directory.string());
    RepositoryInstructionContext guidance(workspace,workspace.repository_instructions("."));
    const std::string identity=R"({"wire":"chat-completions","model_id":"worker-rpc-fixture"})";
    const auto gateway=make_persistence_agent_model_gateway(store,CredentialReference{"worker","rpc-model","provider:chat"});
    for(const bool startup_failure:{false,true}){
        const std::string id=startup_failure?"rpc-supervisor-startup-failure":"rpc-product_supervised";
        const auto deadline=Clock::now()+20s;
        store.create_session(id,"Native production worker supervisor").get();
        store.start_prompt_run(id,id,Json{{"content","Native child model RPC product_supervised."}}.dump(),RootBudgetSpec{"native.direct-agent",1,workspace.identity(),identity,0,0,8,600000}).get();
        store.append_event(id,"agent.workspace_membership",Json{{"workspace_id",workspace.identity()},{"revision",1}}.dump()).get();
        AgentWorkerSupervisor supervisor(store,image,{id,id,workspace.identity(),"generation-"+id,1},identity,deadline);
        check(supervisor.alive()&&store.agent_worker_lease(supervisor.lease().generation).get().state=="active","Supervisor must retain the exact live admitted child before startup");
        rejects<Conflict>([&]{supervisor.execute();});rejects<Conflict>([&]{supervisor.drain_completed(Clock::now()+5s);});
        AgentReadToolOwnerContext owner;owner.run_id=id;owner.primary_workspace=&workspace;owner.repository_context=[&](std::size_t index){return index?nullptr:&guidance;};owner.offered_tool=[](const std::string&){return false;};owner.deadline=deadline;
        AgentWorkerReadRequestPolicy policy;policy.instructions="Supervisor captured instructions.";policy.include_usage=true;
        ChatProviderConfig config{endpoint+"/product_supervised","worker-rpc-fixture",Capability::supported,Capability::supported};config.deadline=10s;config.idle_timeout=10s;
        if(startup_failure){
            auto invalid=owner;invalid.deadline-=1ms;
            rejects<std::invalid_argument>([&]{supervisor.start(invalid,policy,config,{},gateway);});
            rejects<Conflict>([&]{supervisor.start(owner,policy,config,{},gateway);});rejects<Conflict>([&]{supervisor.execute();});
            check(supervisor.alive()&&store.run(id).get().state==RunState::queued&&store.root_budget(id).get().model_calls_reserved==0,"Failed startup must retain physical ownership and unfunded queued history");
            const auto terminal=supervisor.drain_terminal(RunState::failed,"dispatch_error",Clock::now()+5s);
            check(!supervisor.alive()&&terminal.run.state==RunState::failed&&store.run_history(id).get().size()==1&&store.root_budget(id).get().model_calls_reserved==0,"Failed startup must retire only after exact exit without a model or task replay");
            std::cout<<"Native supervisor startup failure verified: retained child and lease, no restart or model, exact physical exit before durable dispatch_error."<<std::endl;continue;
        }
        supervisor.start(owner,policy,config,{},gateway);
        rejects<Conflict>([&]{supervisor.start(owner,policy,config,{},gateway);});
        rejects<Conflict>([&]{supervisor.drain_completed(Clock::now()+5s);});
        check(supervisor.execute()==RunState::completed&&supervisor.alive()&&store.run(id).get().state==RunState::completed,"Supervisor completion intent must accompany actual canonical completion while physical child is retained");
        rejects<Conflict>([&]{supervisor.execute();});
        const auto history=store.run_history(id).get();check(history.size()==2&&Json::parse(history.back().json).at("content")=="Supervisor complete."&&store.root_budget(id).get().model_calls_reserved==1,"Supervised child must retain one actual model and exact canonical history");
        const auto terminal=supervisor.drain_completed(Clock::now()+5s);check(terminal.exit_code==0&&!supervisor.alive()&&terminal.run.state==RunState::completed,"Supervisor must drain its exact completed child");
        const auto count=store.events(id).get().size();supervisor.drain_completed(Clock::now()+5s);check(store.events(id).get().size()==count,"Repeated completed observation must not rewrite facts");
        rejects<Conflict>([&]{supervisor.cancel();});
        std::cout<<"Native supervisor completion verified: actual xmind child, captured backend policy, one execution, canonical facts, exact physical drain and read-only reobservation."<<std::endl;
    }
    {
        const std::string id="rpc-product_supervised_cancel";const auto deadline=Clock::now()+20s;
        store.create_session(id,"Native supervisor concurrent cancellation").get();
        store.start_prompt_run(id,id,Json{{"content","Native child model RPC product_supervised_cancel."}}.dump(),RootBudgetSpec{"native.direct-agent",1,workspace.identity(),identity,0,0,8,600000}).get();
        store.append_event(id,"agent.workspace_membership",Json{{"workspace_id",workspace.identity()},{"revision",1}}.dump()).get();
        AgentWorkerSupervisor supervisor(store,image,{id,id,workspace.identity(),"generation-"+id,1},identity,deadline);
        AgentReadToolOwnerContext owner;owner.run_id=id;owner.primary_workspace=&workspace;owner.repository_context=[&](std::size_t index){return index?nullptr:&guidance;};owner.offered_tool=[](const std::string&){return false;};owner.deadline=deadline;
        AgentWorkerReadRequestPolicy policy;policy.instructions="Supervisor captured instructions.";policy.include_usage=true;
        ChatProviderConfig config{endpoint+"/product_supervised_cancel","worker-rpc-fixture",Capability::supported,Capability::supported};config.deadline=10s;config.idle_timeout=10s;
        supervisor.start(owner,policy,config,{},gateway);
        auto running=std::async(std::launch::async,[&]{return supervisor.execute();});
        bool streamed=false;const auto until=Clock::now()+5s;
        while(Clock::now()<until&&!streamed){const auto events=store.events(id).get();streamed=std::any_of(events.begin(),events.end(),[](const Event& event){return event.kind=="model.text";});if(!streamed)std::this_thread::sleep_for(5ms);}
        check(streamed&&running.wait_for(0ms)==std::future_status::timeout,"Supervised execution must be live inside its actual streaming provider call");
        rejects<Conflict>([&]{supervisor.drain_completed(Clock::now()+5s);});
        rejects<Conflict>([&]{supervisor.drain_terminal(RunState::cancelled,"cancelled",Clock::now()+5s);});
        rejects<Conflict>([&]{supervisor.execute();});
        check(supervisor.alive()&&store.run(id).get().state==RunState::running&&store.agent_worker_lease(supervisor.lease().generation).get().state=="active","Rejected concurrent drains must retain exact physical and durable ownership");
        supervisor.cancel();check(running.get()==RunState::cancelled,"Supervisor cancellation must reach the actual executing child and nested provider call");
        check(supervisor.alive()&&store.run(id).get().state==RunState::running&&store.run_history(id).get().size()==1&&store.root_budget(id).get().model_calls_reserved==1,"Cancelled intent must retain live child, prompt, charge and unfinished durable ownership");
        const auto events=store.events(id).get();const auto uncertain=std::find_if(events.begin(),events.end(),[](const Event& event){return event.kind=="worker.model.uncertain";});
        check(uncertain!=events.end()&&Json::parse(uncertain->json).at("failure_code")=="cancelled","Backend-observed cancellation must persist a fixed cause without a fabricated final assistant");
        const auto terminal=supervisor.drain_terminal(RunState::cancelled,"cancelled",Clock::now()+5s);
        check(terminal.exit_code==0&&!supervisor.alive()&&terminal.run.state==RunState::cancelled&&store.run_history(id).get().size()==1&&store.root_budget(id).get().model_calls_reserved==1,"Exact child exit must precede cancellation commit without refund, replay or fabricated history");
        std::cout<<"Native supervisor concurrent cancellation verified: live provider stream, joined-caller drain guard, exact ownership retained, HTTP abort, durable cancelled cause and post-exit retirement."<<std::endl;
    }
}
}
