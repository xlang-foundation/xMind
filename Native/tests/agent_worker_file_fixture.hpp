#include "agentflow/agent_worker_file_rpc.hpp"
#include "agentflow/create_executor.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include <fstream>
#include <thread>
namespace worker_file_fixture {
using namespace agentflow;using Json=nlohmann::json;using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
inline void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected actual child file RPC rejection");}
inline std::string read(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);check(bool(input),"Actual native RPC file missing");return {std::istreambuf_iterator<char>(input),{}};}
inline AgentFileToolInvocation invocation(const AgentWorkerScope& scope,const ModelToolCall& tool){AgentFileToolInvocation call;call.run_id=scope.run_id;call.attempt_id="file-attempt";call.call_id=tool.id;call.workspace_id=scope.workspace_id;call.workspace_revision=scope.workspace_revision;call.folder_id=Json::parse(tool.arguments_json).value("folder_id",scope.workspace_id);call.tool_name=tool.name;call.arguments_json=tool.arguments_json;return call;}
inline std::string child(AgentWorkerRpcPeer& peer,const std::string& mode,const std::string& method){
    AgentWorkerFileClient files(peer);AgentWorkerJournalClient journal(peer);
    if(method=="fixture.stale"){AgentFileToolInvocation stale;stale.run_id=peer.scope().run_id;stale.attempt_id="file-attempt";stale.call_id="actual-file";stale.workspace_id=peer.scope().workspace_id;stale.folder_id=stale.workspace_id;stale.workspace_revision=peer.scope().workspace_revision;stale.tool_name="create_file";stale.arguments_json=R"({"path":"actual.txt","content":"never replay"})";rejects<Conflict>([&]{files.invoke(stale);});return "stale-file-rejected";}
    const auto input=decode_agent_model_request(peer.call("fixture.file_input",""));journal.reserve_model_call("file-attempt");journal.start_model_call("file-attempt");AgentWorkerModelClient model(peer);const auto response=model.complete("file-attempt",input,[](const ModelEvent&){});
    check(response.finish_reason=="tool_calls"&&response.tool_calls.size()==1&&response.tool_calls[0].id=="actual-file"&&response.tool_calls[0].name=="create_file","File RPC must use the independent provider's actual tool call");const auto call=invocation(peer.scope(),response.tool_calls[0]);
    rejects<Conflict>([&]{files.invoke(call);});journal.finish_model_call("file-attempt");
    for(unsigned field=0;field<7;++field){auto invalid=call;if(field==0)invalid.call_id="forged";if(field==1)invalid.arguments_json=R"({"path":"forged.txt","content":"must not write"})";if(field==2)invalid.folder_id="foreign-folder";if(field==3)++invalid.workspace_revision;if(field==4)invalid.run_id="foreign-run";if(field==5)invalid.workspace_id="foreign-workspace";if(field==6)invalid.tool_name="edit_file";rejects<Conflict>([&]{files.invoke(invalid);});}
    for(const auto* field:{"approval_id","operation_id","root","owner_policy"}){auto malformed=Json::parse(call.serialize());malformed[field]="forged";rejects<AgentWorkerRpcRemoteError>([&]{peer.call("file.invoke",malformed.dump());});}
    const auto wire=call.serialize();rejects<AgentWorkerRpcRemoteError>([&]{peer.call("file.invoke",wire.substr(0,wire.size()-1)+",\"call_id\":\"actual-file\"}");});rejects<AgentWorkerRpcRemoteError>([&]{peer.call("file.prepare",wire);});
    if(mode=="file_cancel"){
        std::stop_source stop;auto task=std::async(std::launch::async,[&]{return files.invoke(call,stop.get_token());});struct Stop{std::stop_source& stop;~Stop(){stop.request_stop();}} guard{stop};
        check(peer.call("fixture.await_file_pending","")=="pending","Actual file proposal must precede cancellation");stop.request_stop();rejects<ToolCancelled>([&]{task.get();});check(peer.call("fixture.await_file_cancelled","")=="cancelled","Backend must retain actual cancellation before the child returns");return Json{{"pid",GetCurrentProcessId()},{"cancelled",true}}.dump();
    }
    if(mode=="file_unrecorded"){rejects<AgentWorkerFileOutcomeUnrecorded>([&]{files.invoke(call);});return Json{{"pid",GetCurrentProcessId()},{"unrecorded",true}}.dump();}
    const auto result=files.invoke(call);
    rejects<Conflict>([&]{journal.commit_read_turn("file-attempt");});rejects<Conflict>([&]{journal.reserve_model_call("premature-file-next");});
    if(mode=="file_denied")check(!result.success&&Json::parse(result.output_json).at("error").at("code")=="agent_permission_denied","Actual native policy denial must cross file RPC unchanged");
    else check(result.success,"Actual approved native file RPC must succeed");
    if(mode=="file_create"){check(peer.call("fixture.rebuild_file_host","")=="rebuilt","Backend fixture must rebuild its durable file host");rejects<Conflict>([&]{files.invoke(call);});}
    return Json{{"pid",GetCurrentProcessId()},{"success",result.success},{"actual_output_json",result.output_json}}.dump();
}
inline void parent(PersistenceService& store,const std::shared_ptr<const BoundProcessExecutable>& image,const std::string& endpoint,const std::filesystem::path& folder,const std::string& mode,const std::vector<std::string>& imports){
    const auto directory=folder/mode;check(std::filesystem::create_directory(directory),"Actual file RPC requires a fresh owned workspace");WorkspaceTools workspace(directory.string());RepositoryInstructionContext context(workspace,workspace.repository_instructions("."));context.prepare();
    const std::string id="rpc-"+mode,generation="generation-"+mode,identity=R"({"wire":"chat-completions","model_id":"worker-rpc-fixture"})",marker="FILE_RPC_MARKER_"+std::to_string(std::random_device{}());const auto deadline=Clock::now()+20s;
    store.create_session(id,"Actual child file RPC").get();store.start_prompt_run(id,id,Json{{"content","Native child file RPC "+mode+". "+marker}}.dump(),RootBudgetSpec{"native.direct-agent",1,workspace.identity(),identity,0,0,8,600000}).get();store.transition(id,RunState::queued,RunState::running).get();store.append_event(id,"agent.workspace_membership",Json{{"workspace_id",workspace.identity()},{"revision",1}}.dump()).get();
    auto worker=AgentWorkerProcess::launch(image,{id,id,workspace.identity(),generation,1},deadline);check(worker->pid()!=GetCurrentProcessId(),"File proxy must execute in an actual distinct child");const auto lease=store.admit_agent_worker({generation,id,id,workspace.identity(),1,worker->pid(),std::to_string(worker->creation_time()),worker->executable_identity(),identity}).get().spec;
    std::cout<<"Actual child file RPC "<<mode<<" child_pid="<<worker->pid()<<" creation_time="<<worker->creation_time()<<std::endl;
    AgentFileToolOwnerContext owner;owner.run_id=id;owner.primary_workspace=&workspace;owner.repository_context=[&](std::size_t index){return index==0?&context:nullptr;};owner.allowed_tools={"create_file"};if(mode=="file_denied")owner.permission_layers={{{"create_file","**","deny"}}};
    auto host=std::make_unique<AgentWorkerFileHost>(store,lease,owner,deadline);auto dispatch=bind_agent_worker_file_rpc(*host,worker->channel().scope());auto foreign=worker->channel().scope();foreign.generation="foreign";rejects<Conflict>([&]{bind_agent_worker_file_rpc(*host,foreign);});
    ModelRequest input;input.include_usage=true;input.messages.push_back({MessageRole::user,"Native child file RPC "+mode+". "+marker});input.tools={CreateExecutor::definition()};ChatProviderConfig config{endpoint+"/"+mode,"worker-rpc-fixture",Capability::supported,Capability::supported};config.deadline=10s;config.idle_timeout=10s;
    AgentWorkerModelHost model(store,lease,config,input.tools,AgentWorkerModelPolicy{true,{}},make_persistence_agent_model_gateway(store,CredentialReference{"worker","rpc-model","provider:chat"}),deadline);auto model_rpc=bind_agent_worker_model_rpc(model,worker->channel().scope());auto journal_rpc=bind_agent_worker_journal_rpc(store,lease,worker->channel().scope(),deadline);
    const auto claim=[&]{return store.agent_worker_file_claim(id,"file-attempt","actual-file").get();};std::promise<void> captured,release;auto ready=captured.get_future();auto released=release.get_future().share();
    AgentWorkerRpcPeer peer(worker->channel(),[&](const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel)->std::string{
        if(method=="fixture.file_input")return encode_agent_model_request(input);
        if(method=="fixture.rebuild_file_host"){host=std::make_unique<AgentWorkerFileHost>(store,lease,owner,deadline);dispatch=bind_agent_worker_file_rpc(*host,worker->channel().scope());return "rebuilt";}
        if(method=="fixture.await_file_pending"||method=="fixture.await_file_cancelled"){const auto until=Clock::now()+5s;while(Clock::now()<until&&!cancel.stop_requested()){const auto operations=store.operations(id).get();if(method=="fixture.await_file_pending"&&operations.size()==1&&operations[0].state==OperationState::awaiting_approval)return "pending";if(method=="fixture.await_file_cancelled"&&operations.size()==1&&operations[0].state==OperationState::cancelled&&claim().state=="returned")return "cancelled";std::this_thread::sleep_for(5ms);}throw std::runtime_error("Actual file RPC approval/cancellation deadline");}
        if(method.starts_with("model."))return model_rpc(method,payload,std::move(sink),cancel);
        if(method.starts_with("journal."))return journal_rpc(method,payload,std::move(sink),cancel);
        if(!method.starts_with("file."))throw AgentWorkerRpcRemoteError("unsupported");const auto result=dispatch(method,payload,std::move(sink),cancel);
        if(mode=="file_delivery"){captured.set_value();while(released.wait_for(10ms)!=std::future_status::ready)if(cancel.stop_requested())throw AgentWorkerRpcRemoteError("cancelled");}return result;
    },deadline);
    std::unique_ptr<XlangSqlite> sql;if(mode=="file_unrecorded"){sql=std::make_unique<XlangSqlite>((folder/"state.sqlite").string(),imports);sql->execute("CREATE TRIGGER fixture_file_rpc_result_abort BEFORE UPDATE ON information WHEN OLD.category='native-agent-worker-file-claims' AND json_extract(NEW.payload,'$.state')='returned' AND json_extract(NEW.payload,'$.model.owner_run_id')='rpc-file_unrecorded' BEGIN SELECT RAISE(ABORT,'fixture_file_rpc_result_abort'); END");}
    worker->channel().send({"fixture.boot",mode},deadline);peer.start();auto task=std::async(std::launch::async,[&]{return peer.call("fixture.run","{}");});
    struct Cleanup{AgentWorkerRpcPeer& peer;std::promise<void>& release;~Cleanup(){try{release.set_value();}catch(...){}peer.stop();}} cleanup{peer,release};
    if(mode!="file_denied"&&mode!="file_cancel"){
        const auto until=Clock::now()+5s;std::optional<Operation> pending;
        while(Clock::now()<until){for(const auto& op:store.operations(id).get())if(op.state==OperationState::awaiting_approval)pending=op;if(pending)break;if(task.wait_for(0ms)==std::future_status::ready)throw std::runtime_error("Actual file child returned before approval: "+task.get());std::this_thread::sleep_for(5ms);}
        check(pending.has_value()&&claim().operation_ids.empty()&&!std::filesystem::exists(directory/"actual.txt"),"Actual child must await controller approval without claiming a mutation");store.decide_operation(pending->id,OperationDecision::allow,"fixture-controller").get();
    }
    if(mode=="file_delivery"){
        check(ready.wait_for(5s)==std::future_status::ready,"Actual file receipt must precede lost delivery");const auto saved=claim();check(saved.state=="returned"&&saved.success&&saved.operation_ids.size()==1&&read(directory/"actual.txt")==marker+"\n","Native file bytes and exact receipt must be durable before reply");const auto actual=decode_agent_model_completion(store.agent_worker_model_claim(id,"file-attempt").get().actual_response_json);AgentWorkerFileHost rebuilt(store,lease,owner,deadline);rejects<Conflict>([&]{rebuilt.invoke(invocation(worker->channel().scope(),actual.tool_calls.at(0)));});
        worker->terminate();release.set_value();rejects<AgentWorkerRpcClosed>([&]{task.get();});peer.stop();check(worker->wait(Clock::now()+5s)==1,"Lost file delivery must drain its exact owned child");check(claim().actual_output_json==saved.actual_output_json&&claim().output_binding==saved.output_binding&&store.operations(id).get().size()==1,"Worker death cannot erase the durable file result or replay its effect");store.retire_agent_worker(generation,"worker_exit").get();
    }else{
        const auto result=Json::parse(task.get());const auto saved=claim();check(result.at("pid")==worker->pid(),"File receipt must come from the actual admitted child");
        if(mode=="file_cancel")check(result.at("cancelled")==true&&saved.state=="returned"&&!saved.success&&saved.operation_ids.empty()&&!std::filesystem::exists(directory/"actual.txt"),"Actual child cancellation must leave a known no-effect result");
        else if(mode=="file_unrecorded")check(result.at("unrecorded")==true&&saved.state=="executing"&&saved.actual_output_json.empty()&&saved.operation_ids.size()==1&&store.operation(saved.operation_ids.at(0)).get().state==OperationState::succeeded&&read(directory/"actual.txt")==marker+"\n","Actual child must receive fail-stop for an unrecorded acknowledgement, retaining native bytes");
        else check(saved.state=="returned"&&saved.success==(mode!="file_denied")&&result.at("actual_output_json")==saved.actual_output_json,"Actual child must receive the exact durable native result");
        if(mode=="file_create")check(read(directory/"actual.txt")==marker+"\n"&&store.operations(id).get().size()==1,"Rebuilt file RPC host must retain one actual mutation");if(mode=="file_denied")check(!std::filesystem::exists(directory/"actual.txt")&&store.operations(id).get().empty(),"Policy denial must precede a native proposal");
        if(sql)sql->execute("DROP TRIGGER fixture_file_rpc_result_abort");store.retire_agent_worker(generation,"cancelled").get();check(worker->alive()&&peer.call("fixture.stale","{}")=="stale-file-rejected","Still-live child must lose file authority after revocation");peer.stop();check(worker->wait(Clock::now()+5s)==0,"Actual file worker must drain on channel closure");
    }
    check(store.run_history(id).get().size()==1&&store.root_budget(id).get().model_calls_reserved==1,"File RPC alone must not fabricate a canonical turn or another model call");store.transition(id,RunState::running,RunState::cancelled).get();
    std::cout<<"Actual child file RPC "<<mode<<" verified backend-owned native effects and durable receipts without replay."<<std::endl;
}
}
