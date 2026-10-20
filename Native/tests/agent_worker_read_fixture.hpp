// Controlled actual child read RPC. The backend owns real workspace/guidance
// handles and obtains the tool call from the independent native HTTP peer.
#include "agentflow/agent_worker_read_rpc.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include <fstream>
#include <thread>
namespace worker_read_fixture {
using namespace agentflow;using Json=nlohmann::json;using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
inline void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected child read rejection");}
inline void write(const std::filesystem::path& path,const std::string& text){std::ofstream output(path,std::ios::binary);output<<text;output.close();check(bool(output),"Cannot write owned read fixture");}
inline std::vector<Message> commit(AgentWorkerRpcPeer& peer,AgentWorkerJournalClient& journal,const std::string& attempt){
    for(const auto* field:{"assistant_json","tool_json"})rejects<AgentWorkerRpcRemoteError>([&]{peer.call("journal.commit_read_turn",Json{{"attempt_id",attempt},{field,"forged history"}}.dump());});
    const auto receipt=journal.commit_read_turn(attempt);const auto count=journal.events(peer.scope().run_id).size();const auto replay=journal.commit_read_turn(attempt);const auto history=journal.run_history(peer.scope().run_id);
    check(receipt.message_sequences==replay.message_sequences&&receipt.messages_binding==replay.messages_binding&&journal.events(peer.scope().run_id).size()==count&&history.size()==3&&history[1].sequence==receipt.message_sequences.at(0)&&history[2].sequence==receipt.message_sequences.at(1)&&history[1].role=="assistant"&&history[2].role=="tool","Read turn acknowledgement must bind one canonical persisted turn without duplicate history/events");return history;
}
inline std::string child(AgentWorkerRpcPeer& peer,const std::string& mode,const std::string& method){
    const auto scope=peer.scope();AgentWorkerReadClient reads(peer);AgentWorkerJournalClient journal(peer);
    AgentWorkspaceInvocation call;call.run_id=scope.run_id;call.attempt_id="read-attempt";call.call_id="actual-read";call.workspace_id=scope.workspace_id;call.workspace_revision=scope.workspace_revision;call.folder_id=scope.workspace_id;call.tool_name="read_file";call.arguments_json=R"({"path":"same.txt"})";
    if(method=="fixture.stale"){rejects<Conflict>([&]{reads.invoke(call);});return "stale-read-rejected";}
    const auto input=decode_agent_model_request(peer.call("fixture.read_input",""));journal.reserve_model_call(call.attempt_id);journal.start_model_call(call.attempt_id);
    AgentWorkerModelClient model(peer);const auto response=model.complete(call.attempt_id,input,[](const ModelEvent&){});
    check(response.finish_reason=="tool_calls"&&response.tool_calls.size()==1&&response.tool_calls[0].id==call.call_id&&response.tool_calls[0].name==call.tool_name&&response.tool_calls[0].arguments_json==call.arguments_json,"Read must use the independent provider's actual tool call");
    rejects<Conflict>([&]{reads.invoke(call);});journal.finish_model_call(call.attempt_id);
    rejects<Conflict>([&]{journal.commit_read_turn(call.attempt_id);});rejects<Conflict>([&]{journal.reserve_model_call("premature-next");});
    if(mode=="read_denied"){rejects<Conflict>([&]{reads.invoke(call);});commit(peer,journal,call.attempt_id);return Json{{"pid",GetCurrentProcessId()},{"denied",true}}.dump();}
    if(mode=="read_cancel"){
        std::stop_source stop;auto task=std::async(std::launch::async,[&]{return reads.invoke(call,stop.get_token());});
        struct Cancel{std::stop_source& stop;~Cancel(){stop.request_stop();}} cancel{stop};
        check(peer.call("fixture.await_pending","")=="pending","Native approval must be observed before caller cancellation");stop.request_stop();rejects<ToolCancelled>([&]{task.get();});commit(peer,journal,call.attempt_id);return Json{{"pid",GetCurrentProcessId()},{"cancelled",true}}.dump();
    }
    for(unsigned field=0;field<6;++field){auto invalid=call;if(field==0)invalid.call_id="forged";if(field==1)invalid.arguments_json=R"({"path":"secret.txt"})";if(field==2)invalid.folder_id="foreign-folder";if(field==3)++invalid.workspace_revision;if(field==4)invalid.run_id="foreign-run";if(field==5)invalid.tool_name="list_files";rejects<Conflict>([&]{reads.invoke(invalid);});}
    auto malformed=Json::parse(call.serialize());malformed["approval_id"]="forged";rejects<AgentWorkerRpcRemoteError>([&]{peer.call("read.invoke",malformed.dump());});
    const auto actual=reads.invoke(call);check(actual.success,"Native backend read must succeed");
    if(mode=="read"){check(peer.call("fixture.rebuild_read_host","")=="rebuilt","Fixture must reconstruct its native owner");rejects<Conflict>([&]{reads.invoke(call);});}
    if(mode=="read_turn_rollback"){bool failed=false;try{journal.commit_read_turn(call.attempt_id);}catch(const AgentWorkerRpcRemoteError& error){failed=error.code()=="failed";}check(failed&&journal.run_history(scope.run_id).size()==1,"Injected native turn fault must roll back history instead of creating a partial batch");return Json{{"pid",GetCurrentProcessId()},{"actual_output_json",actual.output_json},{"rollback",true}}.dump();}
    if(mode!="read_delivery"){
        const auto history=commit(peer,journal,call.attempt_id);
        if(mode=="read"){
            auto next=input;next.messages.clear();for(const auto& stored:history){const auto data=Json::parse(stored.json);ModelMessage message;message.role=stored.role=="user"?MessageRole::user:stored.role=="assistant"?MessageRole::assistant:MessageRole::tool;message.content=data.at("content").get<std::string>();
                if(data.contains("tool_call_id"))message.tool_call_id=data.at("tool_call_id").get<std::string>();if(data.contains("tool_calls"))for(const auto& tool:data.at("tool_calls"))message.tool_calls.push_back({tool.at("id").get<std::string>(),tool.at("name").get<std::string>(),tool.at("arguments").get<std::string>()});if(data.contains("provider_items"))message.provider_items_json=data.at("provider_items").dump();next.messages.push_back(std::move(message));}
            journal.reserve_model_call("read-final");journal.start_model_call("read-final");const auto final=model.complete("read-final",next,[](const ModelEvent&){});check(final.finish_reason=="stop"&&final.tool_calls.empty()&&final.content=="Read turn recorded.","Actual continuation must consume the backend committed tool history");journal.finish_model_call("read-final");check(journal.complete_run("read-final").state==RunState::completed,"Read continuation must complete from its actual backend response");
        }
    }
    return Json{{"pid",GetCurrentProcessId()},{"actual_output_json",actual.output_json}}.dump();
}
inline void parent(PersistenceService& store,const std::shared_ptr<const BoundProcessExecutable>& image,const std::string& endpoint,const std::filesystem::path& folder,const std::string& mode){
    const auto directory=folder/mode;check(std::filesystem::create_directory(directory),"Read RPC fixture requires a fresh workspace");
    const auto marker="actual-native-read-"+std::to_string(std::random_device{}())+"\n";write(directory/"same.txt",marker);write(directory/"AGENTS.md","This isolated workspace is owned by the native read RPC fixture.\n");
    WorkspaceTools workspace(directory.string());RepositoryInstructionContext guidance(workspace,workspace.repository_instructions("."));guidance.prepare();
    const std::string id="rpc-"+mode,generation="generation-"+mode,identity=R"({"wire":"chat-completions","model_id":"worker-rpc-fixture"})";const auto deadline=Clock::now()+20s;
    store.create_session(id,"Actual worker read RPC").get();store.start_prompt_run(id,id,Json{{"content","Native child model RPC "+mode+"."}}.dump(),RootBudgetSpec{"native.direct-agent",1,workspace.identity(),identity,0,0,8,600000}).get();store.transition(id,RunState::queued,RunState::running).get();store.append_event(id,"agent.workspace_membership",Json{{"workspace_id",workspace.identity()},{"revision",1},{"folders",Json::array({{{"folder_id",workspace.identity()},{"root",workspace.root_path()}}})}}.dump()).get();
    auto worker=AgentWorkerProcess::launch(image,{id,id,workspace.identity(),generation,1},deadline);check(worker->pid()!=GetCurrentProcessId(),"Read proxy must run in an actual distinct child");
    std::cout<<"Native read RPC case "<<mode<<" child_pid="<<worker->pid()<<" creation_time="<<worker->creation_time()<<std::endl;
    auto lease=store.admit_agent_worker({generation,id,id,workspace.identity(),1,worker->pid(),std::to_string(worker->creation_time()),worker->executable_identity(),identity}).get().spec;
    AgentReadToolOwnerContext owner;owner.run_id=id;owner.primary_workspace=&workspace;owner.repository_context=[&](std::size_t index){return index==0?&guidance:nullptr;};owner.offered_tool=[](const std::string& name){return name=="read_file";};owner.deadline=deadline;
    if(mode=="read_denied")owner.permission_rules={{"read_file","**","deny"}};
    if(mode=="read_cancel")owner.permission_rules={{"read_file","**","ask"}};
    auto host=std::make_unique<AgentWorkerReadHost>(store,store,lease,owner);auto dispatch=bind_agent_worker_read_rpc(*host,worker->channel().scope());auto foreign=worker->channel().scope();foreign.generation="foreign";rejects<Conflict>([&]{bind_agent_worker_read_rpc(*host,foreign);});
    ModelRequest input;input.include_usage=true;input.messages.push_back({MessageRole::user,"Native child model RPC "+mode+"."});for(const auto& tool:workspace.definitions())if(tool.name=="read_file")input.tools.push_back(tool);check(input.tools.size()==1,"Use the real native read definition");
    ChatProviderConfig config{endpoint+"/"+mode,"worker-rpc-fixture",Capability::supported,Capability::supported};config.deadline=10s;config.idle_timeout=10s;
    AgentWorkerModelHost model(store,lease,config,input.tools,AgentWorkerModelPolicy{true,{}},make_persistence_agent_model_gateway(store,CredentialReference{"worker","rpc-model","provider:chat"}),deadline);
    auto model_dispatch=bind_agent_worker_model_rpc(model,worker->channel().scope());auto journal_dispatch=bind_agent_worker_journal_rpc(store,lease,worker->channel().scope(),deadline);
    std::promise<void> captured,release;auto ready=captured.get_future();auto released=release.get_future().share();
    std::atomic_bool rollback_observed=false;
    AgentWorkerRpcPeer peer(worker->channel(),[&](const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel)->std::string{
        if(method=="fixture.read_input")return encode_agent_model_request(input);
        if(method=="fixture.await_pending"){const auto until=Clock::now()+5s;while(Clock::now()<until&&!cancel.stop_requested()){for(const auto& operation:store.operations(id).get())if(operation.state==OperationState::awaiting_approval)return "pending";std::this_thread::sleep_for(5ms);}throw std::runtime_error("Read fixture approval was not observed");}
        if(method=="fixture.rebuild_read_host"){write(directory/"same.txt","Changed after the actual read; never redispatch.\n");host=std::make_unique<AgentWorkerReadHost>(store,store,lease,owner);dispatch=bind_agent_worker_read_rpc(*host,worker->channel().scope());return "rebuilt";}
        if(method.starts_with("journal.")){
            const auto history_before=store.run_history(id).get();const auto events_before=store.events(id).get();
            try{const auto result=journal_dispatch(method,payload,std::move(sink),cancel);if(mode=="read_turn_delivery"&&method=="journal.commit_read_turn"){captured.set_value();while(released.wait_for(10ms)!=std::future_status::ready)if(cancel.stop_requested())throw AgentWorkerRpcRemoteError("cancelled");}return result;}
            catch(const DatabaseError& error){if(mode=="read_turn_rollback"&&method=="journal.commit_read_turn"&&std::string(error.what()).find("fixture_worker_read_turn_abort")!=std::string::npos){const auto after=store.run_history(id).get();const auto events=store.events(id).get();check(after.size()==history_before.size()&&after.size()==1&&after.front().json==history_before.front().json&&events.size()==events_before.size()&&events.back().sequence==events_before.back().sequence,"Actual native read-turn abort must roll back every history and event append");rollback_observed=true;}throw;}
        }
        if(method.starts_with("model."))return model_dispatch(method,payload,std::move(sink),cancel);
        const auto result=dispatch(method,payload,std::move(sink),cancel);
        if(mode=="read_delivery"){captured.set_value();while(released.wait_for(10ms)!=std::future_status::ready)if(cancel.stop_requested())throw AgentWorkerRpcRemoteError("cancelled");}return result;
    },deadline);
    worker->channel().send({"fixture.boot",mode},deadline);peer.start();
    if(mode=="read_delivery"||mode=="read_turn_delivery"){
        auto task=std::async(std::launch::async,[&]{return peer.call("fixture.run","{}");});struct Unblock{std::promise<void>& release;~Unblock(){try{release.set_value();}catch(...){}}} unblock{release};
        check(ready.wait_for(5s)==std::future_status::ready,"Actual read must be captured before the delivery fault");const auto before=store.agent_worker_read_claim(id,"read-attempt","actual-read").get();check(before.state=="returned"&&before.dispatched&&before.success&&Json::parse(before.actual_output_json).at("content")==marker,"Actual native bytes must be durable before reply delivery");
        if(mode=="read_turn_delivery"){const auto receipt=store.commit_agent_worker_read_turn(lease,"read-attempt",deadline).get();const auto history=store.run_history(id).get();check(history.size()==3&&receipt.message_sequences.size()==2&&history[2].sequence==receipt.message_sequences[1]&&Json::parse(history[2].json).at("content").get<std::string>()==before.actual_output_json,"Read-turn lost reply must retain its exact canonical history before worker death");}
        else check(store.run_history(id).get().size()==1,"A durable read without a turn commit must not invent conversation history");
        worker->terminate();release.set_value();rejects<AgentWorkerRpcClosed>([&]{task.get();});peer.stop();check(worker->wait(Clock::now()+5s)==1,"Explicit delivery fault must retire only its owned worker");
        const auto after=store.agent_worker_read_claim(id,"read-attempt","actual-read").get();check(after.actual_output_json==before.actual_output_json&&after.output_binding==before.output_binding,"Worker death must retain exact actual read output without replay");
        if(mode=="read_turn_delivery"){const auto count=store.events(id).get().size();store.commit_agent_worker_read_turn(lease,"read-attempt",deadline).get();check(store.run_history(id).get().size()==3&&store.events(id).get().size()==count,"Lost tool-turn reply acknowledgement must never append duplicate history or events");std::cout<<"Actual read-turn lost delivery retained one canonical committed turn."<<std::endl;}
    }else{
        const auto result=Json::parse(peer.call("fixture.run","{}"));const auto saved=store.agent_worker_read_claim(id,"read-attempt","actual-read").get();
        if(mode=="read_denied"||mode=="read_cancel"){
            check(result.at("pid")==worker->pid()&&result.at(mode=="read_denied"?"denied":"cancelled")==true&&saved.state=="returned"&&!saved.dispatched&&!saved.success&&saved.actual_output_json.find(marker)==std::string::npos,"Denied/cancelled native read must retain failure without dispatch or file bytes");
            if(mode=="read_cancel"){const auto operations=store.operations(id).get();check(operations.size()==1&&operations.front().state==OperationState::cancelled,"Caller cancellation must retire its actual pending approval");}
            else check(store.operations(id).get().empty(),"Native policy denial must precede any approval");
        }else check(result.at("pid")==worker->pid()&&result.at("actual_output_json").get<std::string>()==saved.actual_output_json&&saved.state=="returned"&&saved.dispatched&&saved.success&&Json::parse(saved.actual_output_json).at("content")==marker,"Actual child must receive exact backend-owned filesystem bytes");
        const auto history=store.run_history(id).get();
        if(mode=="read"){const auto final=store.agent_worker_model_claim(id,"read-final").get();check(store.run(id).get().state==RunState::completed&&store.agent_worker_lease(generation).get().retirement_reason=="completed"&&history.size()==4&&history[1].json==store.agent_worker_model_claim(id,"read-attempt").get().actual_assistant_json&&Json::parse(history[2].json).at("content").get<std::string>()==saved.actual_output_json&&history.back().json==final.actual_assistant_json,"Actual child read/model continuation must complete with exactly its backend history");std::cout<<"Actual child read turn continued through a second native model call and canonical completion."<<std::endl;}
        else if(mode=="read_turn_rollback"){check(rollback_observed&&result.at("rollback")==true&&history.size()==1&&store.run(id).get().state==RunState::running,"Observed read-turn transaction failure must retain read facts without an invented conversation turn");std::cout<<"Observed injected read-turn storage fault; history/events rollback verified."<<std::endl;}
        else check(history.size()==3&&Json::parse(history[2].json).at("content").get<std::string>()==saved.actual_output_json,"Failed native reads must commit only their actual backend failure result");
        if(store.agent_worker_lease(generation).get().state=="active")store.retire_agent_worker(generation,"cancelled").get();check(worker->alive()&&peer.call("fixture.stale","{}")=="stale-read-rejected","Still-live authenticated child must lose read authority after revocation");peer.stop();check(worker->wait(Clock::now()+5s)==0,"Read worker must drain after owner channel closure");
    }
    if(store.agent_worker_lease(generation).get().state=="active")store.retire_agent_worker(generation,"worker_exit").get();
    const auto events=store.events(id).get();const auto expected=(mode=="read_denied"||mode=="read_cancel")?0:1;check(std::count_if(events.begin(),events.end(),[](const Event& event){return event.kind=="worker.read.dispatching";})==expected&&store.root_budget(id).get().model_calls_reserved==(mode=="read"?2:1),"Rejected/reconstructed/lost read delivery must not redispatch or refund its model");
    std::cout<<"Actual child read RPC "<<mode<<" verified durable dispatch count "<<expected<<" and exact native outcome."<<std::endl;
}
}
