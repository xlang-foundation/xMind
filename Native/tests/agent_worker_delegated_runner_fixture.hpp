#pragma once
#include "agentflow/agent_worker_supervisor.hpp"
#include <atomic>
#include <future>
namespace worker_delegated_runner_fixture {
using namespace agentflow;using Json=nlohmann::json;using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
using worker_read_fixture::check;using worker_read_fixture::write;
class Gateway final:public AgentModelGateway {
    std::atomic<unsigned>& entered_;std::string marker_,model_;unsigned calls_=0;
public:
    Gateway(std::atomic<unsigned>& entered,std::string marker,std::string model):entered_(entered),marker_(std::move(marker)),model_(std::move(model)){}
    ModelCompletion complete(const ChatProviderConfig& config,const ModelRequest& request,ChatCompletionStream::Sink,std::stop_token stop)override{
        check(config.model==model_&&request.tools.size()==1&&request.tools.front().name=="read_file","Actual child must use its exact captured model and read-only catalogue");
        check(!request.messages.empty()&&request.messages.front().content.find("DELEGATED_RUNNER_GUIDANCE")!=std::string::npos,"Actual child must receive backend-owned native guidance");
        if(++calls_==1){++entered_;const auto until=Clock::now()+5s;while(entered_.load()!=2&&!stop.stop_requested()&&Clock::now()<until)std::this_thread::sleep_for(1ms);check(entered_.load()==2&&!stop.stop_requested(),"Both actual delegated workers must concurrently enter before either returns");return {"","","tool_calls",R"({"total_tokens":11})",{{"actual-child-read","read_file",R"({"path":"actual.txt"})"}},"[]"};}
        check(calls_==2&&request.messages.back().role==MessageRole::tool&&request.messages.back().content.find(marker_)!=std::string::npos,"Final child request must consume its actual committed native read result");
        return {"Observed delegated read "+model_,"","stop",R"({"total_tokens":13})",{},"[]"};
    }
    ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::logic_error("Unexpected delegated count");}
    ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::logic_error("Unexpected delegated compaction");}
    unsigned calls()const{return calls_;}
};
inline void parent(PersistenceService& store,const std::shared_ptr<const BoundProcessExecutable>& image,const std::filesystem::path& folder){
    const auto directory=folder/"delegated-native-runner";check(std::filesystem::create_directory(directory),"Delegated runner needs a fresh workspace");const std::string marker="delegated-file-bytes-"+std::to_string(std::random_device{}());write(directory/"AGENTS.md","DELEGATED_RUNNER_GUIDANCE\n");write(directory/"actual.txt",marker);WorkspaceTools workspace(directory.string());RepositoryInstructionContext guidance(workspace,workspace.repository_instructions("."));
    const std::string root="actual-delegated-root",identity=R"({"wire":"chat-completions","model_id":"worker-rpc-fixture"})";const auto membership=Json{{"workspace_id",workspace.identity()},{"revision",1}}.dump();
    store.create_session(root,"Actual two-process delegated native runner").get();store.start_prompt_run(root,root,R"({"content":"Inspect the actual file through two delegated workers"})",RootBudgetSpec{"native.delegation",1,workspace.identity(),identity,8,2,8,600000}).get();store.transition(root,RunState::queued,RunState::running).get();store.append_event(root,"agent.workspace_membership",membership).get();
    DelegationBatchSpec batch;batch.id="actual-delegated-batch";batch.parent_run_id=root;batch.provider_tool_call_id="actual-delegated-call";Json tasks=Json::array();
    for(const auto* id:{"actual-delegated-one","actual-delegated-two"}){Json requested={{"id",id},{"objective","Inspect actual.txt and report its result"},{"preset","workspace.inspect"}},prompt={{"content","Inspect actual.txt and report its result"}};if(std::string(id)=="actual-delegated-two"){requested["agent_id"]="named-reader";prompt["agent_profile"]={{"id","named-reader"},{"revision",1},{"model_id","named-reader-model"},{"instructions","Inspect only actual.txt"},{"mode","subagent"}};}tasks.push_back(requested);batch.tasks.push_back({id,std::string(id)+"-node",id,prompt.dump(),"Inspect actual.txt and report its result"});}
    batch.arguments_json=Json{{"tasks",tasks}}.dump();batch.parent_assistant_json=Json{{"content",""},{"tool_calls",Json::array({{{"id",batch.provider_tool_call_id},{"name","delegate_tasks"},{"arguments",batch.arguments_json}}})}}.dump();batch.expected_budget_revision=store.root_budget(root).get().revision;store.accept_delegation_batch(batch).get();
    std::atomic<unsigned> entered=0;std::vector<std::unique_ptr<AgentWorkerSupervisor>> workers;std::vector<std::shared_ptr<Gateway>> models;
    // The transport budget is intentionally much shorter than the root policy;
    // the child must consume the remaining original owner deadline through IPC.
    const auto deadline=Clock::now()+20s;
    for(std::size_t i=0;i<2;++i){const auto& id=batch.tasks[i].child_run_id;store.append_event(id,"agent.workspace_membership",membership).get();auto provider=Json::parse(identity);const std::string model=i?"named-reader-model":"worker-rpc-fixture";provider["model_id"]=model;auto owner=std::make_unique<AgentWorkerSupervisor>(store,image,AgentWorkerScope{root,id,workspace.identity(),id+"-generation",1},provider.dump(),deadline);
        AgentReadToolOwnerContext context;context.run_id=id;context.primary_workspace=&workspace;context.repository_context=[&](std::size_t n){return n?nullptr:&guidance;};context.offered_tool=[](const std::string& name){return name=="read_file";};context.deadline=deadline;
        AgentWorkerReadRequestPolicy policy;policy.instructions=i?"Pinned named subagent: Inspect only actual.txt":"Inspect the actual file";policy.include_usage=true;for(const auto& tool:workspace.definitions())if(tool.name=="read_file")policy.tools.push_back(tool);
        auto gateway=std::make_shared<Gateway>(entered,marker,model);ChatProviderConfig config{"https://synthetic.invalid/not-dispatched",model,Capability::supported,Capability::supported};config.deadline=10s;config.idle_timeout=10s;owner->start(context,policy,config,{},gateway);std::cout<<"Actual delegated xmind PID "<<owner->lease().pid<<" birth "<<owner->lease().creation_time<<" owner "<<id<<std::endl;models.push_back(gateway);workers.push_back(std::move(owner));}
    auto first=std::async(std::launch::async,[&]{return workers[0]->execute();});auto second=std::async(std::launch::async,[&]{return workers[1]->execute();});check(first.get()==RunState::completed&&second.get()==RunState::completed,"Both actual delegated xmind runners must complete through SDK IPC");
    check(workers[0]->lease().pid!=workers[1]->lease().pid&&models[0]->calls()==2&&models[1]->calls()==2&&entered.load()==2,"Distinct child processes must each run exactly two model calls concurrently");
    for(std::size_t i=0;i<2;++i){const auto observed=workers[i]->drain_completed(Clock::now()+5s);check(observed.run.state==RunState::completed&&observed.exit_code==0&&!workers[i]->alive(),"Completed delegated child must physically retire before parent join");const auto history=store.run_history(batch.tasks[i].child_run_id).get();check(history.size()==4&&history[2].role=="tool"&&history[2].json.find(marker)!=std::string::npos,"Actual read and final child result must remain canonical in child history");store.settle_delegation_child(batch.tasks[i].child_run_id).get();}
    const auto joined=store.settle_delegation_batch(batch.id).get();check(!joined.result_json.empty()&&joined.result_json.find("Observed delegated read")!=std::string::npos&&store.root_budget(root).get().model_calls_reserved==4&&store.root_budget(root).get().parent_calls_held==1&&store.run(root).get().state==RunState::running&&store.run_history(root).get().size()==1,"Joined children preserve four shared charges and held parent continuation without replay");
    std::cout<<"Actual concurrent delegated xmind runners passed SDK IPC journal/model/read calls, named model binding, two canonical native file reads, four shared charges and physical drain before join; synthetic native model gateway, production scheduler placement remains open."<<std::endl;
}
}
