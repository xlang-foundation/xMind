// The actual shared AgentRunner loop in the child, through owned request/read
// capabilities and authenticated journal facts. No local workspace is opened.
namespace worker_read_runner_fixture {
using namespace agentflow;using Json=nlohmann::json;
using worker_runner_fixture::check;using worker_runner_fixture::rejects;using worker_runner_fixture::ready;
class Journal final:public worker_runner_fixture::ChildJournal {
public:
    using ChildJournal::ChildJournal;
    std::future<Event> append_event(std::string id,std::string kind,std::string json)override{
        if(kind.starts_with("model."))return ready<Event>([&]{auto event=ChildJournal::append_event(id,kind,json).get();++model_events;return event;});
        return ready<Event>([&]{const auto actual=ChildJournal::append_event(id,kind,json).get();own(id);if(kind!="tool.started"&&kind!="tool.completed"&&kind!="tool.failed")throw std::logic_error("Unsupported read-runner event");const auto body=Json::parse(json);const auto call_id=body.at("call_id").get<std::string>();const auto found=std::find_if(calls.begin(),calls.end(),[&](const auto& call){return call.id==call_id;});if(found==calls.end())throw Conflict("Read-runner activity lacks its actual model call");const auto ordinal=std::distance(calls.begin(),found);const bool returned=kind!="tool.started";const auto event=actual;
            const auto count=client.events(id).size();const auto duplicate=client.acknowledge_tool_event(current_attempt,ordinal,returned,kind,json);check(event.sequence==duplicate.sequence&&client.events(id).size()==count,"Activity acknowledgement must not append duplicate events");++tool_events;return event;});
    }
    std::future<void> record_tool_turn(std::string id,std::string assistant,std::vector<std::string> results,std::optional<SkillSelections> skills={})override{return ready<void>([&]{ChildJournal::record_tool_turn(id,assistant,results,skills).get();own(id);if(skills||assistant!=canonical||results.size()!=calls.size())throw Conflict("Read-runner turn differs from its backend assistant");const auto receipt=client.commit_read_turn(current_attempt);const auto history=client.run_history(id);check(receipt.message_sequences.size()==results.size()+1,"Read-runner receipt must cover its full actual batch");
        for(std::size_t index=0;index<receipt.message_sequences.size();++index){const auto found=std::find_if(history.begin(),history.end(),[&](const Message& message){return message.sequence==receipt.message_sequences[index];});check(found!=history.end()&&found->role==(index?"tool":"assistant")&&found->json==(index?results[index-1]:assistant),"Actual AgentRunner must consume exactly the backend committed assistant and results");}
        ++tool_turns;
    });}
    std::vector<ModelToolCall> calls;std::int64_t model_events=0,tool_events=0,tool_turns=0;
};
class Model final:public AgentModelGateway {
public:
    explicit Model(Journal& journal):journal(journal),native(make_agent_worker_model_gateway(journal.peer,journal.native)){}
    ModelCompletion complete(const ChatProviderConfig& config,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel)override{try{check(config.endpoint=="worker://captured"&&config.model=="worker-rpc-fixture","Read-runner model must use captured proxy routing");auto result=native->complete(config,request,std::move(sink),cancel);journal.calls=result.tool_calls;++calls;return result;}catch(const std::exception& error){journal.model_diagnostic=error.what();throw;}}
    ResponsesTokenCount count_context(const ChatProviderConfig& config,const ModelRequest& request,std::stop_token cancel)override{return native->count_context(config,request,cancel);}
    ResponsesCompactionResult compact_context(const ChatProviderConfig& config,const ModelRequest& request,std::stop_token cancel)override{return native->compact_context(config,request,cancel);}
    Journal& journal;std::shared_ptr<AgentModelGateway> native;std::int64_t calls=0;
};
inline std::string execute(AgentWorkerRpcPeer& peer,std::stop_token cancel={}){
    Journal journal(peer,cancel);auto closed=make_agent_worker_closed_ports();worker_runner_fixture::verify_closed_ports(closed);auto model=std::make_shared<Model>(journal);auto workspace=make_agent_worker_read_workspace(peer,journal.native);
    const auto public_metadata=encode_agent_worker_model_admission(AgentWorkerModelClient(peer).admission());check(public_metadata.find("127.0.0.1")==std::string::npos&&public_metadata.find("synthetic-model-rpc-not-live")==std::string::npos&&public_metadata.find("deadline")==std::string::npos,"Read admission metadata must omit backend destinations, credentials and deadlines");
    for(const auto* payload:{R"({"include_usage":false})",R"({"deadline_ms":999999})"})rejects<AgentWorkerRpcRemoteError>([&]{peer.call("model.admission",payload);});
    rejects<std::invalid_argument>([&]{make_agent_worker_read_workspace(peer,{});});
    struct ForeignBinding final:worker_runner_fixture::ChildJournal {using ChildJournal::ChildJournal;bool bound_to(const AgentWorkerRpcPeer&)const noexcept override{return false;}};
    auto foreign=std::make_shared<ForeignBinding>(peer);rejects<std::invalid_argument>([&]{make_agent_worker_read_workspace(peer,foreign);});
    check(workspace->workspace_id()==peer.scope().workspace_id&&workspace->workspace_revision()==peer.scope().workspace_revision,"Native read workspace must preserve exact captured metadata");
    std::stop_source stopped;stopped.request_stop();rejects<TransportCancelled>([&]{workspace->prepare_request(stopped.get_token());});
    auto unadmitted=workspace->read_host();AgentWorkspaceInvocation wrong{1,"foreign-root","unadmitted","unadmitted",peer.scope().workspace_id,peer.scope().workspace_revision,peer.scope().workspace_id,"read_file",R"({"path":"nested/same.txt"})"};rejects<Conflict>([&]{unadmitted->invoke(wrong);});
    AgentSettings settings;const auto admission=AgentWorkerModelClient(peer).admission();check(admission.owned_request_window&&admission.include_usage,"Read worker must observe its captured backend request window and controls");settings.provider=agent_worker_model_config(admission);settings.max_output_tokens=admission.max_output_tokens;settings.instructions.clear();const auto captured=journal.native->root_budget(peer.scope().root_id).get();settings.max_turns=static_cast<std::size_t>(captured.spec.max_model_calls);settings.run_timeout=std::chrono::milliseconds(captured.spec.wall_limit_ms);settings.a2a_enabled=false;
    AgentRunnerPorts ports{model,closed.catalogue,closed.tools,closed.runtime,workspace};
    for(const auto* payload:{R"({"attempt_id":"forged","ordinal":0,"phase":"started","json":"forged event"})",R"({"attempt_id":"forged","ordinal":0,"phase":"unknown"})",R"({"attempt_id":"forged","ordinal":0.5,"phase":"started"})"})rejects<AgentWorkerRpcRemoteError>([&]{peer.call("journal.tool_event",payload);});
    auto invalid=settings;invalid.instructions="worker replacement policy";rejects<std::invalid_argument>([&]{AgentRunner rejected(journal,*closed.context,invalid,ports);});
    AgentRunner runner(journal,*closed.context,std::move(settings),std::move(ports));rejects<RunUnavailable>([&]{runner.start("unadmitted","unadmitted","must not start a child-selected run");});
    Run result;try{result=runner.execute(peer.scope().run_id,cancel);}catch(const AgentTerminalHandoff& intent){return Json{{"pid",GetCurrentProcessId()},{"terminal_handoff",to_string(intent.outcome())}}.dump();}catch(const std::exception& error){return Json{{"runner_failure",std::string(error.what())+"; model: "+journal.model_diagnostic}}.dump();}
    check(result.state==RunState::completed&&model->calls==3&&journal.tool_turns==2&&journal.tool_events==6&&!journal.canonical.empty(),"Actual child AgentRunner must execute three actual models, three read activities and two canonical tool turns");
    const auto actual=journal.native->progress();check(actual.model_events==journal.model_events&&actual.tool_events==journal.tool_events&&actual.tool_turns==journal.tool_turns&&actual.canonical_assistant==journal.canonical,"Production journal must derive read activity bindings and canonical turns from backend receipts");
    std::cout<<"Production ordinary read journal verified in actual child: canonical model-derived activities, exact committed tool turns and acknowledged progress."<<std::endl;
    return Json{{"pid",GetCurrentProcessId()},{"actual_runner",true},{"models",model->calls},{"tool_turns",journal.tool_turns},{"tool_events",journal.tool_events},{"model_events",journal.model_events},{"final_assistant",journal.canonical}}.dump();
}
}
