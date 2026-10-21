#include "agentflow/agent_service.hpp"
#include "agentflow/agent_definitions.hpp"
namespace worker_service_fixture {
using namespace agentflow;using Json=nlohmann::json;using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
using worker_read_fixture::check;using worker_read_fixture::rejects;using worker_read_fixture::write;
// Controlled backend factory for this service integration fixture. The product
// dispatch port remains separate from general settings/placement configuration.
class Executor final:public AgentRootExecutor {
public:
    Executor(PersistenceService& value,std::shared_ptr<const BoundProcessExecutable> bound,WorkspaceTools& files,
        RepositoryInstructionContext& instructions,std::string endpoint,Clock::time_point until)
        :store(value),image(std::move(bound)),workspace(files),guidance(instructions),endpoint(std::move(endpoint)),deadline(until){}
    void validate(const AgentRootExecutionRequest& request)const override{
        if(request.model_id!="worker-rpc-fixture"||request.agent_instructions!="Captured service agent."||!request.agent_tools||!request.agent_tools->empty()||!request.agent_permissions.empty()||!request.workspace||request.workspace->workspace_id!=workspace.identity()||request.workspace->revision<1||request.workspace->folders.empty()||request.workspace->folders.front().root!=workspace.root_path())throw std::invalid_argument("Unsupported controlled worker service admission");
    }
    Run execute(const AgentRootExecutionRequest& request,std::stop_token cancel)override{
        validate(request);++executions;
        const auto budget=store.root_budget(request.run_id).get();
        if(budget.spec.workspace_identity!=workspace.identity())throw ToolAccessDenied("Controlled service worker workspace differs from root admission");
        const auto identity=budget.spec.provider_identity_json;
        const auto& membership=*request.workspace;captured_revision=membership.revision;
        WorkspaceToolSet members(membership.workspace_id,membership.revision,membership.folders);
        std::vector<std::unique_ptr<RepositoryInstructionContext>> contexts;
        Json folders=Json::array();for(std::size_t index=0;index<membership.folders.size();++index){const auto& folder=membership.folders[index];folders.push_back({{"folder_id",folder.folder_id},{"root",folder.root}});auto& files=members.folder_tools(index);contexts.push_back(std::make_unique<RepositoryInstructionContext>(files,files.repository_instructions(".")));}
        store.append_event(request.run_id,"agent.workspace_membership",Json{{"workspace_id",membership.workspace_id},{"revision",membership.revision},{"primary_folder_id",membership.folders.front().folder_id},{"folders",std::move(folders)},{"snapshot","run_admission"}}.dump()).get();
        AgentWorkerSupervisor supervisor(store,image,{request.run_id,request.run_id,membership.workspace_id,"generation-"+request.run_id,membership.revision},identity,deadline);
        child_pid=supervisor.lease().pid;
        child_birth=std::stoull(supervisor.lease().creation_time);
        const auto settle=[&](RunState intent){
            for(;;){try{if(store.run(request.run_id).get().state==RunState::completed)return supervisor.drain_completed(Clock::now()+5s).run;
                    return supervisor.drain_terminal(intent==RunState::cancelled?RunState::cancelled:RunState::failed,intent==RunState::cancelled?"cancelled":"dispatch_error",Clock::now()+5s).run;}
                catch(...){if(!supervisor.alive())throw;std::this_thread::sleep_for(5ms);}}
        };
        try{
            AgentReadToolOwnerContext owner;owner.run_id=request.run_id;owner.primary_workspace=&members.folder_tools(0);owner.workspace_members=&members;owner.repository_context=[&](std::size_t index){return index<contexts.size()?contexts[index].get():nullptr;};owner.offered_tool=[](const std::string&){return false;};owner.deadline=deadline;owner.permission_rules=request.agent_permissions;
            AgentWorkerReadRequestPolicy policy;policy.instructions="Service captured base.\n\nSelected agent instructions (trusted backend snapshot):\n"+request.agent_instructions;policy.include_usage=true;
            ChatProviderConfig config{endpoint+"/product_service","worker-rpc-fixture",Capability::supported,Capability::supported};config.deadline=10s;config.idle_timeout=10s;
            supervisor.start(owner,policy,config,{},make_persistence_agent_model_gateway(store,CredentialReference{"worker","rpc-model","provider:chat"}),cancel);
            const auto intent=supervisor.execute();
            if(hold_drain&&intent==RunState::completed){canonical_ready=true;while(!release_drain&&!cancel.stop_requested())std::this_thread::sleep_for(5ms);}
            const auto result=settle(intent);drained=!supervisor.alive();return result;
        }catch(...){const auto failure=std::current_exception();(void)settle(cancel.stop_requested()?RunState::cancelled:RunState::failed);std::rethrow_exception(failure);}
    }
    std::atomic<unsigned> executions=0;std::atomic<bool> drained=false;std::atomic<std::uint32_t> child_pid=0;
    std::atomic<std::uint64_t> child_birth=0;
    std::atomic<std::int64_t> captured_revision=0;
    std::atomic<bool> hold_drain=false,canonical_ready=false,release_drain=false;
private:
    PersistenceService& store;std::shared_ptr<const BoundProcessExecutable> image;WorkspaceTools& workspace;RepositoryInstructionContext& guidance;std::string endpoint;Clock::time_point deadline;
};
inline void parent(PersistenceService& store,const std::shared_ptr<const BoundProcessExecutable>& image,const std::string& endpoint,const std::filesystem::path& folder){
    const auto directory=folder/"product_service";check(std::filesystem::create_directory(directory),"Service worker needs a fresh workspace");write(directory/"AGENTS.md","SERVICE_WORKER_GUIDANCE\n");
    WorkspaceTools workspace(directory.string());RepositoryInstructionContext guidance(workspace,workspace.repository_instructions("."));
    AgentSettings settings;settings.provider={endpoint+"/product_service","worker-rpc-fixture",Capability::supported,Capability::supported};settings.workspace=directory.string();settings.a2a_enabled=false;settings.tool_allowlist=std::vector<std::string>{};settings.instructions="Service captured base.";settings.run_timeout=20s;
    std::mutex membership_mutex;std::atomic<unsigned> membership_reads=0;
    WorkspaceMembershipSnapshot current_membership{workspace.identity(),7,{{workspace.identity(),workspace.root_path()}}};
    settings.workspace_membership_snapshot=[&]{std::lock_guard lock(membership_mutex);++membership_reads;return current_membership;};
    const auto executor=std::make_shared<Executor>(store,image,workspace,guidance,endpoint,Clock::now()+20s);
    const auto catalog=AgentDefinitionStore(store).apply("agents:\n  - id: service.worker.good\n    model_id: worker-rpc-fixture\n    instructions: Captured service agent.\n    tools: []\n  - id: service.worker.unsupported\n    model_id: worker-rpc-fixture\n    instructions: Captured service agent.\n    tools: [read_file]\n");
    const std::string session="rpc-service-supervised-session",id="rpc-product_service";store.create_session(session,"Actual service-owned native worker").get();
    const auto pool=std::make_shared<RootAdmissionPool>(3,1,true);
    executor->hold_drain=true;
    AgentService service(store,settings,1,2,pool,executor);const auto metadata=service.agent_definitions();
    struct Release {Executor& owner;~Release(){owner.release_drain=true;}} release{*executor};
    const auto bad=std::find_if(metadata.begin(),metadata.end(),[](const auto& item){return item.id=="service.worker.unsupported";});
    const auto good=std::find_if(metadata.begin(),metadata.end(),[](const auto& item){return item.id=="service.worker.good";});check(bad!=metadata.end()&&good!=metadata.end(),"Service fixture must expose actual registered profiles");
    service.select_session_agent(session,*bad,0);
    rejects<std::invalid_argument>([&]{service.submit("rpc-service-rejected",session,"must not dispatch");});
    check(store.runs(session).get().empty()&&executor->executions==0&&service.healthy()&&service.idle(),"Worker validation must reject unsupported admission before any durable root or execution");
    service.select_session_agent(session,*good,1);
    std::atomic<bool> admission_held=false,release_admission=false;
    const auto blocker=pool->executor()->stage([&](std::stop_token){admission_held=true;while(!release_admission)std::this_thread::sleep_for(5ms);});
    struct AdmissionRelease {std::atomic<bool>& released;~AdmissionRelease(){released=true;}} admission_release{release_admission};
    blocker->commit();const auto blocking_until=Clock::now()+5s;while(Clock::now()<blocking_until&&!admission_held)std::this_thread::sleep_for(5ms);check(admission_held,"Hold the physical pool before service admission");
    const auto reads_before=membership_reads.load();
    service.submit(id,session,"Native child model RPC product_service.");
    check(membership_reads==reads_before+1&&executor->executions==0,"Service must capture membership once before dispatch while its root is queued");
    {std::lock_guard lock(membership_mutex);current_membership.revision=8;current_membership.folders.push_back({"later-folder",folder.string()});}
    release_admission=true;blocker->result().get();
    const auto held_until=Clock::now()+5s;while(Clock::now()<held_until&&!executor->canonical_ready)std::this_thread::sleep_for(5ms);
    check(executor->canonical_ready&&store.run(id).get().state==RunState::completed&&!service.idle()&&pool->executor()->running()==1&&!executor->drained,"Canonical completion must retain the shared physical slot and service ownership before child drain");
    check(executor->captured_revision==7&&membership_reads==reads_before+1,"Dispatch must use immutable admitted membership, never recapture the changed callback");
    const auto captured_events=store.events(id).get();const auto snapshot=std::find_if(captured_events.begin(),captured_events.end(),[](const Event& event){return event.kind=="agent.workspace_membership";});
    check(snapshot!=captured_events.end()&&Json::parse(snapshot->json).at("revision")==7&&Json::parse(snapshot->json).at("folders").size()==1,"Actual worker lease and request context must retain the admitted folder set");
    struct Observation {HANDLE handle=nullptr;~Observation(){if(handle)CloseHandle(handle);}} child{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,executor->child_pid)};
    check(child.handle!=nullptr,"Observe the exact supervised child process");FILETIME created,exited,kernel,user;
    check(GetProcessTimes(child.handle,&created,&exited,&kernel,&user)!=0,"Observe supervised child birth");ULARGE_INTEGER birth;birth.LowPart=created.dwLowDateTime;birth.HighPart=created.dwHighDateTime;
    check(birth.QuadPart==executor->child_birth&&WaitForSingleObject(child.handle,0)==WAIT_TIMEOUT,"Canonical completion must retain the exact PID/birth child alive");
    std::atomic<bool> next_started=false;
    const auto next=pool->executor()->stage([&,handle=child.handle](std::stop_token){check(executor->drained&&WaitForSingleObject(handle,0)==WAIT_OBJECT_0,"Next physical action cannot run before exact child exit");next_started=true;});
    Release release_before_ticket_drain{*executor};next->commit();
    check(next->result().wait_for(0ms)==std::future_status::timeout&&!next_started&&pool->executor()->running()==1&&pool->executor()->waiting()>=1,"Shared executor must keep the next action queued while completed child ownership is held");
    executor->release_drain=true;check(next->result().wait_for(5s)==std::future_status::ready,"Next action must become runnable after exact physical drain");next->result().get();
    const auto until=Clock::now()+5s;while(Clock::now()<until&&(!service.idle()||pool->executor()->running()!=0))std::this_thread::sleep_for(5ms);
    check(service.idle()&&service.healthy()&&executor->executions==1&&executor->drained&&executor->child_pid!=GetCurrentProcessId(),"AgentService must retain execution until its exact supervised child is physically drained");
    const auto history=store.run_history(id).get();check(history.size()==2&&Json::parse(history.back().json).at("content")=="Service worker complete."&&store.root_budget(id).get().model_calls_reserved==1,"Service-owned child must preserve actual canonical history and one model charge");service.close();
    std::cout<<"AgentService supervised child dispatch verified: pre-admission profile validation, captured named-agent settings, actual distinct child, canonical completion and physical drain before service idle."<<std::endl;
    check(next_started&&pool->executor()->running()==0,"Shared executor must retire both physical actions");
    std::cout<<"AgentService shared physical worker verified: canonical completion held exact live child and one occupied slot; next queued action started only after exact child exit and supervisor drain."<<std::endl;
    std::cout<<"AgentService immutable workspace capture verified: one admission snapshot, queued callback changed to revision eight, actual child retained revision seven and its exact original folder set."<<std::endl;
}
}
