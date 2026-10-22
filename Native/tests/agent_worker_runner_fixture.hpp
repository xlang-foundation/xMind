#pragma once
// Instrumentation and adversarial probes around native worker components.
// Process entry and product placement remain separate from this fixture.
#include "agentflow/agent_runner.hpp"
#include "agentflow/agent_terminal_handoff.hpp"
#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/agent_worker_run_journal.hpp"
#include "agentflow/agent_worker_model_gateway.hpp"
#include "agentflow/agent_worker_read_workspace.hpp"
#include "agentflow/agent_worker_closed_ports.hpp"
#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/agent_worker_runner.hpp"
#include "agentflow/a2a_tool_registry.hpp"
#include <type_traits>
#include <iostream>
#include <utility>
namespace worker_runner_fixture {
using namespace agentflow;
inline void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class E,class F> void rejects(F f){try{f();}catch(const E&){return;}throw std::runtime_error("Expected child fixture rejection");}
template<class T,class F> std::future<T> ready(F f){std::promise<T> p;auto result=p.get_future();try{if constexpr(std::is_void_v<T>){f();p.set_value();}else p.set_value(f());}catch(...){p.set_exception(std::current_exception());}return result;}
// Instrumentation bridge only: all admitted journal operations below execute
// the production adapter. The direct client remains for adversarial RPC probes.
class ChildJournal:public AgentWorkerOrdinaryJournal {
public:
    explicit ChildJournal(AgentWorkerRpcPeer& peer,std::stop_token cancel={}):peer(peer),client(peer),native(make_agent_worker_ordinary_journal(peer,cancel)){}
    bool bound_to(const AgentWorkerClientContext& peer)const noexcept override{return native->bound_to(peer);}
    AgentWorkerJournalProgress progress()const override{return native->progress();}
    void own(const std::string& id){if(id!=peer.scope().run_id)throw Conflict("Child journal owner differs");}
    void refresh(){const auto actual=native->progress();current_attempt=actual.current_attempt;canonical=actual.canonical_assistant;ordinal=actual.model_event_ordinal;}
    std::future<Run> run(std::string id)override{return native->run(std::move(id));}
    std::future<Session> session(std::string id)override{return native->session(std::move(id));}
    std::future<std::vector<Message>> run_history(std::string id)override{return native->run_history(std::move(id));}
    std::future<std::vector<Event>> events(std::string id,std::int64_t after)override{return native->events(std::move(id),after);}
    std::future<RootBudgetRecord> root_budget(std::string id)override{return native->root_budget(std::move(id));}
    std::future<Run> transition(std::string id,RunState expected,RunState next,std::string json)override{return native->transition(std::move(id),expected,next,std::move(json));}
    std::future<ModelCallReservation> reserve_model_call(std::string root,std::string owner,std::string attempt,ModelCallRole role)override{return native->reserve_model_call(std::move(root),std::move(owner),std::move(attempt),role);}
    std::future<ModelCallReservation> start_model_call(std::string root,std::string owner,std::string attempt)override{return ready<ModelCallReservation>([&]{auto actual=native->start_model_call(std::move(root),std::move(owner),std::move(attempt)).get();refresh();return actual;});}
    std::future<ModelCallReservation> finish_model_call(std::string root,std::string owner,std::string attempt,std::optional<std::string> assistant)override{return ready<ModelCallReservation>([&]{auto actual=native->finish_model_call(std::move(root),std::move(owner),std::move(attempt),std::move(assistant)).get();refresh();return actual;});}
    std::future<Event> append_event(std::string id,std::string kind,std::string json)override{return ready<Event>([&]{auto actual=native->append_event(std::move(id),std::move(kind),std::move(json)).get();refresh();return actual;});}
    std::future<void> record_tool_turn(std::string id,std::string assistant,std::vector<std::string> results,std::optional<SkillSelections> skills={})override{return native->record_tool_turn(std::move(id),std::move(assistant),std::move(results),std::move(skills));}
    std::future<Run> complete_run(std::string id,std::string assistant)override{return native->complete_run(std::move(id),std::move(assistant));}
    std::future<bool> append_a2a_task_event(std::string run_id,std::string operation_id,std::string json)override{return native->append_a2a_task_event(std::move(run_id),std::move(operation_id),std::move(json));}
    std::future<ChildAdmissionRecord> child_admission(std::string child)override{return native->child_admission(std::move(child));}
    std::future<void> commit_dynamic_tool_turn(std::string root,std::string call)override{return native->commit_dynamic_tool_turn(std::move(root),std::move(call));}
    std::future<Run> complete_dynamic_owner(std::string root,std::string assistant_json,std::string segment_id,std::int64_t measured_active_elapsed_ms)override{return native->complete_dynamic_owner(std::move(root),std::move(assistant_json),std::move(segment_id),std::move(measured_active_elapsed_ms));}
    std::future<ContextStatusObservation> context_status_observation(ContextScope scope,ContextBinding binding)override{return native->context_status_observation(std::move(scope),std::move(binding));}
    std::future<DynamicPlanCapabilities> dynamic_capabilities(std::string root)override{return native->dynamic_capabilities(std::move(root));}
    std::future<std::optional<ContextPausePin>> dynamic_context_pause(std::string root)override{return native->dynamic_context_pause(std::move(root));}
    std::future<DynamicPlanRecord> dynamic_plan(std::string plan)override{return native->dynamic_plan(std::move(plan));}
    std::future<std::vector<DynamicPlanCallRecord>> dynamic_plan_calls(std::string root)override{return native->dynamic_plan_calls(std::move(root));}
    std::future<std::optional<DynamicPlanRecord>> dynamic_plan_for_root(std::string root)override{return native->dynamic_plan_for_root(std::move(root));}
    std::future<std::optional<DynamicPlanRecord>> dynamic_plan_for_owner(std::string owner)override{return native->dynamic_plan_for_owner(std::move(owner));}
    std::future<DynamicPlanCapabilities> finalize_dynamic_capabilities(std::string root,std::string backend_identity,std::string catalogue_json,std::vector<DynamicPresetCapability> presets)override{return native->finalize_dynamic_capabilities(std::move(root),std::move(backend_identity),std::move(catalogue_json),std::move(presets));}
    std::future<InferenceStepRecord> finish_inference_attempt(InferenceAttemptResult result)override{return native->finish_inference_attempt(std::move(result));}
    std::future<InferenceStepRecord> inference_step(std::string id)override{return native->inference_step(std::move(id));}
    std::future<Operation> finish_operation(std::string id,OperationState outcome,std::string result_json)override{return native->finish_operation(std::move(id),std::move(outcome),std::move(result_json));}
    std::future<GraphContextOwnerRecord> graph_context_owner(std::string root)override{return native->graph_context_owner(std::move(root));}
    std::future<IdleContextOwnerRecord> idle_context_owner(std::string id)override{return native->idle_context_owner(std::move(id));}
    std::future<SkillSelections> initialize_run_skills(std::string id,std::string workspace_id)override{return native->initialize_run_skills(std::move(id),std::move(workspace_id));}
    std::future<DynamicBudgetSegment> open_dynamic_budget_segment(DynamicSegmentSpec spec)override{return native->open_dynamic_budget_segment(std::move(spec));}
    std::future<void> record_rejected_dynamic_tool_turn(std::string root,std::string origin_attempt_id,std::string actual_assistant_json,std::string safe_code)override{return native->record_rejected_dynamic_tool_turn(std::move(root),std::move(origin_attempt_id),std::move(actual_assistant_json),std::move(safe_code));}
    std::future<Run> retire_dynamic_owner(std::string root,RunState terminal_state,std::string reason_json,std::string segment_id={},std::int64_t measured_active_elapsed_ms=0)override{return native->retire_dynamic_owner(std::move(root),std::move(terminal_state),std::move(reason_json),std::move(segment_id),std::move(measured_active_elapsed_ms));}
    std::future<DynamicResumeRecord> resume_dynamic_owner(DynamicResumeSpec spec)override{return native->resume_dynamic_owner(std::move(spec));}
    std::future<DynamicPlanCallRecord> dynamic_plan_call(std::string call)override{return native->dynamic_plan_call(std::move(call));}
    std::future<std::vector<DynamicPlanRecord>> dynamic_plans_for_root(std::string root)override{return native->dynamic_plans_for_root(std::move(root));}
    std::future<ModelCallReservation> reserve_dynamic_continuation(std::string call,std::string attempt)override{return native->reserve_dynamic_continuation(std::move(call),std::move(attempt));}
    std::future<std::vector<OwnedChildRecord>> owned_children(std::string parent)override{return native->owned_children(std::move(parent));}
    std::future<Run> suspend_dynamic_owner(DynamicPauseSpec spec)override{return native->suspend_dynamic_owner(std::move(spec));}
    std::future<Operation> request_operation(std::string id,OperationSpec spec,std::int64_t expires_unix_ms)override{return native->request_operation(std::move(id),std::move(spec),std::move(expires_unix_ms));}
    std::future<Operation> operation(std::string id)override{return native->operation(std::move(id));}
    std::future<Operation> decide_operation(std::string id,OperationDecision decision,std::string actor)override{return native->decide_operation(std::move(id),std::move(decision),std::move(actor));}
    std::future<Operation> claim_operation(std::string id,OperationSpec actual)override{return native->claim_operation(std::move(id),std::move(actual));}
    std::future<Operation> cancel_operation(std::string id,OperationSpec actual)override{return native->cancel_operation(std::move(id),std::move(actual));}
    std::future<Operation> expire_operation(std::string id)override{return native->expire_operation(std::move(id));}
    std::future<Run> start_prompt_run(std::string id,std::string session_id,std::string prompt_json,std::optional<RootBudgetSpec> budget={},std::optional<DynamicPlanCapabilities> dynamic={},std::string inbox_id={})override{return native->start_prompt_run(std::move(id),std::move(session_id),std::move(prompt_json),std::move(budget),std::move(dynamic),std::move(inbox_id));}
    AgentWorkerRpcPeer& peer;AgentWorkerJournalClient client;std::shared_ptr<AgentWorkerOrdinaryJournal> native;std::string current_attempt,canonical,model_diagnostic;std::int64_t ordinal=0;
};
class ChildModel final:public AgentModelGateway {
public:
    explicit ChildModel(ChildJournal& journal):journal(journal),native(make_agent_worker_model_gateway(journal.peer,journal.native)){}
    ModelCompletion complete(const ChatProviderConfig& config,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel)override{
        try{
        check(config.endpoint=="worker://captured"&&config.model=="worker-rpc-fixture"&&!journal.current_attempt.empty(),"Child gateway must use captured proxy routing");
        rejects<Conflict>([&]{journal.client.begin_run();});
        if(!observed){
            auto changed=config;changed.endpoint="https://unadmitted.invalid/model";rejects<Conflict>([&]{native->complete(changed,request,{},{});});changed=config;changed.model="unadmitted-model";rejects<Conflict>([&]{native->complete(changed,request,{},{});});changed=config;changed.wire=ProviderWire::responses;rejects<Conflict>([&]{native->complete(changed,request,{},{});});
            std::stop_source stopped;stopped.request_stop();rejects<TransportCancelled>([&]{native->complete(config,request,{},stopped.get_token());});
            rejects<std::logic_error>([&]{native->count_context(config,request,{});});rejects<std::logic_error>([&]{native->compact_context(config,request,{});});
        }
        return native->complete(config,request,[&](const ModelEvent& event){
            if(!observed)rejects<Conflict>([&]{native->complete(config,request,{},{});});
            if(!observed){const auto first=journal.client.acknowledge_model_event(journal.current_attempt,0,event.kind,event.json);const auto duplicate=journal.client.acknowledge_model_event(journal.current_attempt,0,event.kind,event.json);const auto saved=journal.client.events(journal.peer.scope().run_id);check(first.sequence==duplicate.sequence&&std::count_if(saved.begin(),saved.end(),[&](const Event& value){return value.sequence==first.sequence&&value.kind==event.kind&&value.json==event.json;})==1,"Acknowledgement replay must retain the same captured event");
                rejects<Conflict>([&]{journal.client.acknowledge_model_event(journal.current_attempt,0,event.kind,R"({"forged":true})");});rejects<Conflict>([&]{journal.client.acknowledge_model_event(journal.current_attempt,8192,event.kind,event.json);});rejects<Conflict>([&]{journal.client.acknowledge_model_event(journal.current_attempt,0,"tool.started",event.json);});}
            ++observed;sink(event);
        },cancel);
        }catch(const std::exception& error){journal.model_diagnostic=error.what();throw;}
    }
    ResponsesTokenCount count_context(const ChatProviderConfig& config,const ModelRequest& request,std::stop_token cancel)override{return native->count_context(config,request,cancel);}
    ResponsesCompactionResult compact_context(const ChatProviderConfig& config,const ModelRequest& request,std::stop_token cancel)override{return native->compact_context(config,request,cancel);}
    ChildJournal& journal;std::shared_ptr<AgentModelGateway> native;std::int64_t observed=0;
};
inline void verify_closed_ports(AgentWorkerClosedPorts& closed){
    check(closed.context&&closed.catalogue&&closed.tools&&closed.runtime,"Native worker must receive explicit closed capability objects");
    const auto denied=[&](auto action){try{action();}catch(const std::logic_error& error){check(std::string(error.what()).find("not admitted")!=std::string::npos,"Closed ports must explicitly deny capability, not return an invalid future");return;}throw std::runtime_error("Closed worker capability was acknowledged");};
    denied([&]{closed.context->start_context_compaction("unadmitted","unadmitted").get();});
    denied([&]{closed.context->retire_context_measure("unadmitted",ContextFailureCode::cancelled,0).get();});
    denied([&]{closed.catalogue->agent_definitions();});
    denied([&]{closed.catalogue->credential_versions(AgentSettings{});});
    denied([&]{closed.tools->read(AgentReadToolOwnerContext{});});
    denied([&]{closed.runtime->a2a_catalog();});
}
struct Outcome {Run run;std::string attempt,assistant;std::int64_t events;};
inline Outcome execute(AgentWorkerRpcPeer& peer,std::stop_token cancel={}){
    rejects<std::invalid_argument>([]{AgentTerminalHandoff invalid(RunState::completed);});
    ChildJournal journal(peer,cancel);auto closed=make_agent_worker_closed_ports();verify_closed_ports(closed);
    std::stop_source assembly_stop;assembly_stop.request_stop();rejects<TransportCancelled>([&]{execute_agent_worker_read_root(peer,assembly_stop.get_token());});
    rejects<Conflict>([&]{execute_agent_worker_read_root(peer);});
    const auto public_metadata=encode_agent_worker_model_admission(AgentWorkerModelClient(peer).admission());check(public_metadata.find("127.0.0.1")==std::string::npos&&public_metadata.find("synthetic-model-rpc-not-live")==std::string::npos&&public_metadata.find("deadline")==std::string::npos,"Admission metadata must omit backend destinations, credentials and deadlines");
    for(const auto* payload:{R"({"model_id":"unadmitted"})",R"({"endpoint":"unadmitted"})","[]"})rejects<AgentWorkerRpcRemoteError>([&]{peer.call("model.admission",payload);});
    std::stop_source metadata_stop;metadata_stop.request_stop();rejects<TransportCancelled>([&]{AgentWorkerModelClient(peer).admission(metadata_stop.get_token());});
    const auto model_only=make_agent_worker_read_workspace(peer,journal.native);check(model_only->workspace_id()=="workspace:none"&&model_only->captured_budget()->workspace_identity=="workspace:none","Model-only proxy must retain its exact absence of workspace authority");
    rejects<std::invalid_argument>([&]{make_agent_worker_model_gateway(peer,{});});
    struct ForeignBinding final:ChildJournal {using ChildJournal::ChildJournal;bool bound_to(const AgentWorkerClientContext&)const noexcept override{return false;}};
    auto foreign=std::make_shared<ForeignBinding>(peer);rejects<std::invalid_argument>([&]{make_agent_worker_model_gateway(peer,foreign);});
    rejects<Conflict>([&]{journal.native->run("foreign-root").get();});
    rejects<Conflict>([&]{journal.native->session("foreign-session").get();});
    rejects<Conflict>([&]{journal.native->complete_run(peer.scope().run_id,R"({"content":"forged"})").get();});
    rejects<std::logic_error>([&]{journal.native->dynamic_plan_for_root(peer.scope().run_id).get();});
    rejects<std::logic_error>([&]{journal.native->request_operation("unadmitted",{},0).get();});
    rejects<std::logic_error>([&]{journal.native->initialize_run_skills(peer.scope().run_id,peer.scope().workspace_id).get();});
    std::stop_source stopped;stopped.request_stop();auto stopped_journal=make_agent_worker_ordinary_journal(peer,stopped.get_token());
    rejects<TransportCancelled>([&]{stopped_journal->reserve_model_call(peer.scope().run_id,peer.scope().run_id,"stopped-funding",ModelCallRole::parent).get();});
    check(journal.client.run(peer.scope().run_id).state==RunState::queued,"Actual child must receive queued admission");
    rejects<Conflict>([&]{journal.client.reserve_model_call("before-start");});
    auto gateway=std::make_shared<ChildModel>(journal);AgentSettings settings;const auto admission=AgentWorkerModelClient(peer).admission();check(!admission.owned_request_window&&admission.include_usage,"Plain worker must observe the captured backend model policy");settings.provider=agent_worker_model_config(admission);settings.max_output_tokens=admission.max_output_tokens;settings.instructions.clear();const auto captured=journal.native->root_budget(peer.scope().root_id).get();settings.max_turns=static_cast<std::size_t>(captured.spec.max_model_calls);settings.run_timeout=std::chrono::milliseconds(captured.spec.wall_limit_ms);settings.a2a_enabled=false;
    AgentRunner runner(journal,*closed.context,std::move(settings),AgentRunnerPorts{gateway,closed.catalogue,closed.tools,closed.runtime});
    Run result;try{result=runner.execute(peer.scope().run_id,cancel);}catch(const AgentTerminalHandoff&){throw;}catch(const std::exception& error){throw std::runtime_error(std::string("Child AgentRunner: ")+error.what()+"; model diagnostic: "+journal.model_diagnostic);}check(result.state==RunState::completed&&gateway->observed==journal.ordinal&&!journal.canonical.empty(),"Actual child AgentRunner must complete through scoped backend ports");
    const auto actual=journal.native->progress();check(actual.current_attempt==journal.current_attempt&&actual.canonical_assistant==journal.canonical&&actual.model_event_ordinal==journal.ordinal,"Production journal progress must contain the exact acknowledged runner facts");
    std::cout<<"Production ordinary worker journal verified in actual child: scoped reads, closed capabilities, cancelled funding, backend model receipts and native completion."<<std::endl;
    return {result,journal.current_attempt,journal.canonical,journal.ordinal};
}
}
