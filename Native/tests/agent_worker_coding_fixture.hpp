#include "agentflow/agent_worker_read_executor.hpp"
#include "agentflow/execution_platform.hpp"
#include <fstream>
namespace worker_coding_fixture {
using namespace agentflow;using Json=nlohmann::json;using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
using worker_read_fixture::check;using worker_read_fixture::rejects;using worker_read_fixture::write;
inline std::string bytes(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);check(bool(input),"Coding fixture output must exist");return {std::istreambuf_iterator<char>(input),{}};}
inline void parent(PersistenceService& store,const std::shared_ptr<const BoundProcessExecutable>& image,const std::string& endpoint,const std::filesystem::path& folder,const std::vector<std::string>& imports){
    for(const std::string mode:{"coding_create","coding_denied","coding_cancel","coding_unrecorded","coding_attached"}){
        const bool denied=mode=="coding_denied",cancelled=mode=="coding_cancel",unrecorded=mode=="coding_unrecorded",attached=mode=="coding_attached";
        const auto directory=folder/mode;check(std::filesystem::create_directory(directory),"Coding factory requires a fresh workspace");
        const auto marker="CODING_MARKER_"+std::to_string(std::random_device{}());write(directory/"AGENTS.md","CODING_ROOT_GUIDANCE\n");write(directory/"input.txt",marker+"\n");
        const auto extra=folder/(mode+"-extra");if(attached){check(std::filesystem::create_directory(extra),"Coding attached folder must be fresh");write(extra/"input.txt",marker+"\n");write(directory/"input.txt","Primary decoy bytes\n");}
        AgentSettings settings;settings.provider={endpoint+"/"+mode,"worker-rpc-fixture",Capability::supported,Capability::supported};settings.provider.output_limit=Capability::supported;
        settings.max_output_tokens=64;settings.workspace=directory.string();settings.approved_edits=true;settings.a2a_enabled=false;settings.run_timeout=20s;
        settings.instructions="Native coding worker base.";settings.credential=CredentialReference{"worker","rpc-model","provider:chat"};settings.provider_identity=ProviderExecutionIdentity{"worker-profile","controlled-route","openai",7};
        if(denied)settings.permission_rules={{"create_file","**","deny"}};
        if(attached){WorkspaceTools primary(directory.string()),secondary(extra.string());const WorkspaceMembershipSnapshot snapshot{primary.identity(),17,{{primary.identity(),primary.root_path()},{secondary.identity(),secondary.root_path()}}};settings.workspace_membership_snapshot=[snapshot]{return snapshot;};}
        {auto invalid=settings;invalid.approved_edits=false;rejects<std::invalid_argument>([&]{make_agent_worker_coding_executor(store,invalid,image);});}
        AgentDefinitionStore(store).apply("agents:\n  - id: coding.worker\n    mode: primary\n    placement: process\n    model_id: worker-rpc-fixture\n    instructions: Native coding worker selected instructions.\n    tools: [read_file, create_file]\n    permissions:\n      - action: read_file\n        resource: '**'\n        effect: allow\n      - action: create_file\n        resource: '**'\n        effect: allow\n");
        const std::string session="rpc-session-"+mode,id="rpc-"+mode;store.create_session(session,"Actual coding worker factory").get();
        std::unique_ptr<XlangSqlite> fault;if(unrecorded){fault=std::make_unique<XlangSqlite>((folder/"state.sqlite").string(),imports);fault->execute("CREATE TRIGGER fixture_coding_result_abort BEFORE UPDATE ON information WHEN OLD.category='native-agent-worker-file-claims' AND json_extract(NEW.payload,'$.state')='returned' AND json_extract(NEW.payload,'$.model.owner_run_id')='rpc-coding_unrecorded' BEGIN SELECT RAISE(ABORT,'fixture_coding_result_abort'); END");}
        unsigned factories=0;ExecutionPlatform service(store,settings,1,2,[&](PersistenceService& actual,const AgentSettings& captured){++factories;check(&actual==&store&&captured.approved_edits&&captured.credential&&captured.provider_identity,"Coding factory must retain resolved backend owners");return make_agent_worker_coding_executor(actual,captured,image);});
        service.select_session_agent(session,service.agent_definitions().front(),0);service.submit(id,session,"Native coding worker "+mode+". "+marker);
        const auto target=(attached?extra:directory)/"created.txt";std::optional<Operation> pending;const auto until=Clock::now()+15s;
        if(!denied){while(Clock::now()<until&&!pending){for(const auto& operation:store.operations(id).get())if(operation.state==OperationState::awaiting_approval)pending=operation;if(service.idle())break;if(!pending)std::this_thread::sleep_for(5ms);}
            check(pending.has_value()&&!std::filesystem::exists(target),"Coding child must wait for actual controller approval without mutation");
            if(cancelled)service.cancel(id);else store.decide_operation(pending->id,OperationDecision::allow,"coding-fixture-controller").get();
        }
        while(Clock::now()<until&&(unrecorded?service.healthy():!service.idle()))std::this_thread::sleep_for(5ms);
        check((unrecorded?!service.healthy():(service.idle()&&service.healthy()))&&factories==1,"Coding service must physically drain one worker before idle");
        const auto run=store.run(id).get();const auto events=store.events(id).get();const auto history=store.run_history(id).get();
        const auto admitted=std::find_if(events.begin(),events.end(),[](const auto& event){return event.kind=="worker.lease.admitted";});check(admitted!=events.end(),"Coding execution must admit an actual worker");
        const auto generation=Json::parse(admitted->json).at("generation").get<std::string>();const auto lease=store.agent_worker_lease(generation).get();check(lease.spec.pid!=GetCurrentProcessId()&&lease.state=="retired","Coding execution must retain and retire its distinct worker identity");
        const auto claimed=std::find_if(events.begin(),events.end(),[](const auto& event){return event.kind=="worker.file.claimed";});check(claimed!=events.end(),"Coding runner must use the durable file claim");
        const auto attempt=Json::parse(claimed->json).at("attempt_id").get<std::string>();const auto file=store.agent_worker_file_claim(id,attempt,"coding-file").get();
        if(cancelled||unrecorded){check(history.size()==1&&store.root_budget(id).get().model_calls_reserved==1&&run.state==(cancelled?RunState::cancelled:RunState::failed),"Interrupted coding turn must not invent history, completion or another paid model");
            if(cancelled)check(!std::filesystem::exists(target)&&file.state=="returned"&&!file.success&&file.operation_ids.empty()&&store.operation(pending->id).get().state==OperationState::cancelled,"Cancellation during approval must retain a known no-effect receipt");
            else check(bytes(target)==marker+"\n"&&file.state=="uncertain"&&file.actual_output_json.empty()&&file.operation_ids.size()==1&&store.operation(file.operation_ids[0]).get().state==OperationState::succeeded,"Unrecorded coding result must retain actual bytes and native operation without replay");
        }else{check(run.state==RunState::completed&&history.size()==5&&store.root_budget(id).get().model_calls_reserved==2,"Coding runner must commit a mixed tool turn and exact final model once");
            check(Json::parse(history[2].json).at("tool_call_id")=="coding-read"&&Json::parse(Json::parse(history[2].json).at("content").get<std::string>()).at("content")==marker+"\n"&&Json::parse(history[3].json).at("content")==file.actual_output_json,"Canonical coding history must retain exact ordered native results");
            check(Json::parse(history.back().json).at("content")=="Coding worker complete.","Coding final assistant must come from its actual model response");
            if(denied)check(!std::filesystem::exists(target)&&store.operations(id).get().empty()&&!file.success,"Inherited denial cannot be replaced by selected allow rules");
            else check(bytes(target)==marker+"\n"&&file.success&&file.operation_ids.size()==1&&store.operations(id).get().size()==1,"Approved coding creation must persist one actual effect");
            check(std::count_if(events.begin(),events.end(),[](const auto& event){return event.kind=="conversation.tool_turn";})==1&&std::count_if(events.begin(),events.end(),[](const auto& event){return event.kind=="tool.started";})==2,"Coding runner must publish its actual mixed activity and canonical turn once");
        }
        if(attached)check(!std::filesystem::exists(directory/"created.txt")&&bytes(directory/"input.txt")=="Primary decoy bytes\n","Attached coding must not touch the primary decoy");
        if(fault)fault->execute("DROP TRIGGER fixture_coding_result_abort");
        std::cout<<"Production coding worker "<<mode<<" verified actual AgentRunner, backend guidance and approvals, durable file results, canonical continuation and physical drain without replay."<<std::endl;
    }
}
}
