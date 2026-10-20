// The actual shared AgentRunner loop in the child, through owned request/read
// capabilities and authenticated journal facts. No local workspace is opened.
namespace worker_read_runner_fixture {
using namespace agentflow;using Json=nlohmann::json;
using worker_runner_fixture::check;using worker_runner_fixture::rejects;using worker_runner_fixture::ready;
class Workspace final:public AgentRunnerReadWorkspace {
public:
    explicit Workspace(AgentWorkerRpcPeer& peer):peer(peer),model(peer){}
    std::string workspace_id()const override{return peer.scope().workspace_id;}
    std::int64_t workspace_revision()const override{return peer.scope().workspace_revision;}
    ModelRequest prepare_request(std::stop_token cancel)override{return model.prepare_request(cancel);}
    std::unique_ptr<AgentReadToolHost> read_host()override{return std::make_unique<AgentWorkerReadClient>(peer);}
    AgentWorkerRpcPeer& peer;AgentWorkerModelClient model;
};
class Journal final:public worker_runner_fixture::ChildJournal {
public:
    using ChildJournal::ChildJournal;
    std::future<Event> append_event(std::string id,std::string kind,std::string json)override{
        if(kind.starts_with("model."))return ready<Event>([&]{auto event=ChildJournal::append_event(id,kind,json).get();++model_events;return event;});
        return ready<Event>([&]{own(id);if(kind!="tool.started"&&kind!="tool.completed"&&kind!="tool.failed")throw std::logic_error("Unsupported read-runner event");const auto body=Json::parse(json);const auto call_id=body.at("call_id").get<std::string>();const auto found=std::find_if(calls.begin(),calls.end(),[&](const auto& call){return call.id==call_id;});if(found==calls.end())throw Conflict("Read-runner activity lacks its actual model call");const auto ordinal=std::distance(calls.begin(),found);const bool returned=kind!="tool.started";const auto event=client.acknowledge_tool_event(current_attempt,ordinal,returned,kind,json);
            const auto count=client.events(id).size();const auto duplicate=client.acknowledge_tool_event(current_attempt,ordinal,returned,kind,json);check(event.sequence==duplicate.sequence&&client.events(id).size()==count,"Activity acknowledgement must not append duplicate events");++tool_events;return event;});
    }
    std::future<void> record_tool_turn(std::string id,std::string assistant,std::vector<std::string> results,std::optional<SkillSelections> skills={})override{return ready<void>([&]{own(id);if(skills||assistant!=canonical||results.size()!=calls.size())throw Conflict("Read-runner turn differs from its backend assistant");const auto receipt=client.commit_read_turn(current_attempt);const auto history=client.run_history(id);check(receipt.message_sequences.size()==results.size()+1,"Read-runner receipt must cover its full actual batch");
        for(std::size_t index=0;index<receipt.message_sequences.size();++index){const auto found=std::find_if(history.begin(),history.end(),[&](const Message& message){return message.sequence==receipt.message_sequences[index];});check(found!=history.end()&&found->role==(index?"tool":"assistant")&&found->json==(index?results[index-1]:assistant),"Actual AgentRunner must consume exactly the backend committed assistant and results");}
        ++tool_turns;
    });}
    std::vector<ModelToolCall> calls;std::int64_t model_events=0,tool_events=0,tool_turns=0;
};
class Model final:public AgentModelGateway {
public:
    explicit Model(Journal& journal):journal(journal),client(journal.peer){}
    ModelCompletion complete(const ChatProviderConfig& config,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel)override{try{check(config.endpoint=="worker://captured"&&config.model=="worker-rpc-fixture","Read-runner model must use captured proxy routing");auto result=client.complete(journal.current_attempt,request,std::move(sink),cancel);journal.calls=result.tool_calls;++calls;return result;}catch(const std::exception& error){journal.model_diagnostic=error.what();throw;}}
    ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::logic_error("Unsupported read-runner count");}
    ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::logic_error("Unsupported read-runner compaction");}
    Journal& journal;AgentWorkerModelClient client;std::int64_t calls=0;
};
inline std::string execute(AgentWorkerRpcPeer& peer){
    Journal journal(peer);worker_runner_fixture::UnsupportedContextJournal context;auto model=std::make_shared<Model>(journal);auto workspace=std::make_shared<Workspace>(peer);
    AgentSettings settings;settings.provider={"worker://captured","worker-rpc-fixture",Capability::supported,Capability::supported};settings.instructions.clear();settings.max_turns=8;settings.a2a_enabled=false;
    AgentRunnerPorts ports{model,std::make_shared<worker_runner_fixture::ClosedCatalog>(),std::make_shared<worker_runner_fixture::ClosedTools>(),std::make_shared<worker_runner_fixture::ClosedRuntime>(),workspace};
    for(const auto* payload:{R"({"attempt_id":"forged","ordinal":0,"phase":"started","json":"forged event"})",R"({"attempt_id":"forged","ordinal":0,"phase":"unknown"})",R"({"attempt_id":"forged","ordinal":0.5,"phase":"started"})"})rejects<AgentWorkerRpcRemoteError>([&]{peer.call("journal.tool_event",payload);});
    auto invalid=settings;invalid.instructions="worker replacement policy";rejects<std::invalid_argument>([&]{AgentRunner rejected(journal,context,invalid,ports);});
    AgentRunner runner(journal,context,std::move(settings),std::move(ports));rejects<RunUnavailable>([&]{runner.start("unadmitted","unadmitted","must not start a child-selected run");});
    Run result;try{result=runner.execute(peer.scope().run_id);}catch(const std::exception& error){return Json{{"runner_failure",std::string(error.what())+"; model: "+journal.model_diagnostic}}.dump();}
    check(result.state==RunState::completed&&model->calls==3&&journal.tool_turns==2&&journal.tool_events==6&&!journal.canonical.empty(),"Actual child AgentRunner must execute three actual models, three read activities and two canonical tool turns");
    return Json{{"pid",GetCurrentProcessId()},{"actual_runner",true},{"models",model->calls},{"tool_turns",journal.tool_turns},{"tool_events",journal.tool_events},{"model_events",journal.model_events},{"final_assistant",journal.canonical}}.dump();
}
}
