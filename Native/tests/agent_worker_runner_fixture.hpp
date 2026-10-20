#pragma once
// Actual AgentRunner child proof only. Unsupported context/tools/catalogue
// operations fail explicitly; these fixtures are not product worker adapters.
#include "agentflow/agent_runner.hpp"
#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/a2a_tool_registry.hpp"
#include <type_traits>
namespace worker_runner_fixture {
using namespace agentflow;
inline void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class E,class F> void rejects(F f){try{f();}catch(const E&){return;}throw std::runtime_error("Expected child fixture rejection");}
template<class T,class F> std::future<T> ready(F f){std::promise<T> p;auto result=p.get_future();try{if constexpr(std::is_void_v<T>){f();p.set_value();}else p.set_value(f());}catch(...){p.set_exception(std::current_exception());}return result;}
class UnsupportedRunJournal:public AgentRunJournal {public:
    std::future<bool> append_a2a_task_event([[maybe_unused]] std::string run_id,[[maybe_unused]] std::string operation_id,[[maybe_unused]] std::string json) override {throw std::logic_error("Unsupported child fixture port: append_a2a_task_event");}
    std::future<Event> append_event([[maybe_unused]] std::string id,[[maybe_unused]] std::string kind,[[maybe_unused]] std::string json) override {throw std::logic_error("Unsupported child fixture port: append_event");}
    std::future<ChildAdmissionRecord> child_admission([[maybe_unused]] std::string child) override {throw std::logic_error("Unsupported child fixture port: child_admission");}
    std::future<void> commit_dynamic_tool_turn([[maybe_unused]] std::string root,[[maybe_unused]] std::string call) override {throw std::logic_error("Unsupported child fixture port: commit_dynamic_tool_turn");}
    std::future<Run> complete_dynamic_owner([[maybe_unused]] std::string root,[[maybe_unused]] std::string assistant_json,[[maybe_unused]] std::string segment_id,[[maybe_unused]] std::int64_t measured_active_elapsed_ms) override {throw std::logic_error("Unsupported child fixture port: complete_dynamic_owner");}
    std::future<Run> complete_run([[maybe_unused]] std::string id,[[maybe_unused]] std::string assistant_json) override {throw std::logic_error("Unsupported child fixture port: complete_run");}
    std::future<ContextStatusObservation> context_status_observation([[maybe_unused]] ContextScope scope,[[maybe_unused]] ContextBinding binding) override {throw std::logic_error("Unsupported child fixture port: context_status_observation");}
    std::future<DynamicPlanCapabilities> dynamic_capabilities([[maybe_unused]] std::string root) override {throw std::logic_error("Unsupported child fixture port: dynamic_capabilities");}
    std::future<std::optional<ContextPausePin>> dynamic_context_pause([[maybe_unused]] std::string root) override {throw std::logic_error("Unsupported child fixture port: dynamic_context_pause");}
    std::future<DynamicPlanRecord> dynamic_plan([[maybe_unused]] std::string plan) override {throw std::logic_error("Unsupported child fixture port: dynamic_plan");}
    std::future<std::vector<DynamicPlanCallRecord>> dynamic_plan_calls([[maybe_unused]] std::string root) override {throw std::logic_error("Unsupported child fixture port: dynamic_plan_calls");}
    std::future<std::optional<DynamicPlanRecord>> dynamic_plan_for_root([[maybe_unused]] std::string root) override {throw std::logic_error("Unsupported child fixture port: dynamic_plan_for_root");}
    std::future<std::optional<DynamicPlanRecord>> dynamic_plan_for_owner([[maybe_unused]] std::string owner) override {throw std::logic_error("Unsupported child fixture port: dynamic_plan_for_owner");}
    std::future<std::vector<Event>> events([[maybe_unused]] std::string id,[[maybe_unused]] std::int64_t after=0) override {throw std::logic_error("Unsupported child fixture port: events");}
    std::future<DynamicPlanCapabilities> finalize_dynamic_capabilities([[maybe_unused]] std::string root,[[maybe_unused]] std::string backend_identity,[[maybe_unused]] std::string catalogue_json,[[maybe_unused]] std::vector<DynamicPresetCapability> presets) override {throw std::logic_error("Unsupported child fixture port: finalize_dynamic_capabilities");}
    std::future<InferenceStepRecord> finish_inference_attempt([[maybe_unused]] InferenceAttemptResult result) override {throw std::logic_error("Unsupported child fixture port: finish_inference_attempt");}
    std::future<InferenceStepRecord> inference_step([[maybe_unused]] std::string id) override {throw std::logic_error("Unsupported child fixture port: inference_step");}
    std::future<Operation> finish_operation([[maybe_unused]] std::string id,[[maybe_unused]] OperationState outcome,[[maybe_unused]] std::string result_json) override {throw std::logic_error("Unsupported child fixture port: finish_operation");}
    std::future<GraphContextOwnerRecord> graph_context_owner([[maybe_unused]] std::string root) override {throw std::logic_error("Unsupported child fixture port: graph_context_owner");}
    std::future<IdleContextOwnerRecord> idle_context_owner([[maybe_unused]] std::string id) override {throw std::logic_error("Unsupported child fixture port: idle_context_owner");}
    std::future<SkillSelections> initialize_run_skills([[maybe_unused]] std::string id,[[maybe_unused]] std::string workspace_id) override {throw std::logic_error("Unsupported child fixture port: initialize_run_skills");}
    std::future<DynamicBudgetSegment> open_dynamic_budget_segment([[maybe_unused]] DynamicSegmentSpec spec) override {throw std::logic_error("Unsupported child fixture port: open_dynamic_budget_segment");}
    std::future<void> record_rejected_dynamic_tool_turn([[maybe_unused]] std::string root,[[maybe_unused]] std::string origin_attempt_id,[[maybe_unused]] std::string actual_assistant_json,[[maybe_unused]] std::string safe_code) override {throw std::logic_error("Unsupported child fixture port: record_rejected_dynamic_tool_turn");}
    std::future<void> record_tool_turn([[maybe_unused]] std::string id,[[maybe_unused]] std::string assistant_json,[[maybe_unused]] std::vector<std::string> tool_json,[[maybe_unused]] std::optional<SkillSelections> skills={}) override {throw std::logic_error("Unsupported child fixture port: record_tool_turn");}
    std::future<Run> retire_dynamic_owner([[maybe_unused]] std::string root,[[maybe_unused]] RunState terminal_state,[[maybe_unused]] std::string reason_json,[[maybe_unused]] std::string segment_id={},[[maybe_unused]] std::int64_t measured_active_elapsed_ms=0) override {throw std::logic_error("Unsupported child fixture port: retire_dynamic_owner");}
    std::future<DynamicResumeRecord> resume_dynamic_owner([[maybe_unused]] DynamicResumeSpec spec) override {throw std::logic_error("Unsupported child fixture port: resume_dynamic_owner");}
    std::future<RootBudgetRecord> root_budget([[maybe_unused]] std::string id) override {throw std::logic_error("Unsupported child fixture port: root_budget");}
    std::future<Run> run([[maybe_unused]] std::string id) override {throw std::logic_error("Unsupported child fixture port: run");}
    std::future<std::vector<Message>> run_history([[maybe_unused]] std::string id) override {throw std::logic_error("Unsupported child fixture port: run_history");}
    std::future<Session> session([[maybe_unused]] std::string id) override {throw std::logic_error("Unsupported child fixture port: session");}
    std::future<ModelCallReservation> reserve_model_call([[maybe_unused]] std::string root,[[maybe_unused]] std::string owner,[[maybe_unused]] std::string attempt,[[maybe_unused]] ModelCallRole role) override {throw std::logic_error("Unsupported child fixture port: reserve_model_call");}
    std::future<ModelCallReservation> start_model_call([[maybe_unused]] std::string root,[[maybe_unused]] std::string owner,[[maybe_unused]] std::string attempt) override {throw std::logic_error("Unsupported child fixture port: start_model_call");}
    std::future<ModelCallReservation> finish_model_call([[maybe_unused]] std::string root,[[maybe_unused]] std::string owner,[[maybe_unused]] std::string attempt,[[maybe_unused]] std::optional<std::string> assistant_json={}) override {throw std::logic_error("Unsupported child fixture port: finish_model_call");}
    std::future<DynamicPlanCallRecord> dynamic_plan_call([[maybe_unused]] std::string call) override {throw std::logic_error("Unsupported child fixture port: dynamic_plan_call");}
    std::future<std::vector<DynamicPlanRecord>> dynamic_plans_for_root([[maybe_unused]] std::string root) override {throw std::logic_error("Unsupported child fixture port: dynamic_plans_for_root");}
    std::future<ModelCallReservation> reserve_dynamic_continuation([[maybe_unused]] std::string call,[[maybe_unused]] std::string attempt) override {throw std::logic_error("Unsupported child fixture port: reserve_dynamic_continuation");}
    std::future<std::vector<OwnedChildRecord>> owned_children([[maybe_unused]] std::string parent) override {throw std::logic_error("Unsupported child fixture port: owned_children");}
    std::future<Run> suspend_dynamic_owner([[maybe_unused]] DynamicPauseSpec spec) override {throw std::logic_error("Unsupported child fixture port: suspend_dynamic_owner");}
    std::future<Operation> request_operation([[maybe_unused]] std::string id,[[maybe_unused]] OperationSpec spec,[[maybe_unused]] std::int64_t expires_unix_ms) override {throw std::logic_error("Unsupported child fixture port: request_operation");}
    std::future<Operation> operation([[maybe_unused]] std::string id) override {throw std::logic_error("Unsupported child fixture port: operation");}
    std::future<Operation> decide_operation([[maybe_unused]] std::string id,[[maybe_unused]] OperationDecision decision,[[maybe_unused]] std::string actor) override {throw std::logic_error("Unsupported child fixture port: decide_operation");}
    std::future<Operation> claim_operation([[maybe_unused]] std::string id,[[maybe_unused]] OperationSpec actual) override {throw std::logic_error("Unsupported child fixture port: claim_operation");}
    std::future<Operation> cancel_operation([[maybe_unused]] std::string id,[[maybe_unused]] OperationSpec actual) override {throw std::logic_error("Unsupported child fixture port: cancel_operation");}
    std::future<Operation> expire_operation([[maybe_unused]] std::string id) override {throw std::logic_error("Unsupported child fixture port: expire_operation");}
    std::future<Run> start_prompt_run([[maybe_unused]] std::string id,[[maybe_unused]] std::string session_id,[[maybe_unused]] std::string prompt_json,[[maybe_unused]] std::optional<RootBudgetSpec> budget={},[[maybe_unused]] std::optional<DynamicPlanCapabilities> dynamic={},[[maybe_unused]] std::string inbox_id={}) override {throw std::logic_error("Unsupported child fixture port: start_prompt_run");}
    std::future<Run> transition([[maybe_unused]] std::string id,[[maybe_unused]] RunState expected,[[maybe_unused]] RunState next,[[maybe_unused]] std::string json="{}") override {throw std::logic_error("Unsupported child fixture port: transition");}
};
class UnsupportedContextJournal:public AgentContextJournal {public:
    std::future<ContextSnapshot> context_snapshot([[maybe_unused]] ContextScope scope,[[maybe_unused]] ContextBinding binding,[[maybe_unused]] ContextReadBound bound={}) override {throw std::logic_error("Unsupported child fixture port: context_snapshot");}
    std::future<std::vector<ContextGroupPayload>> context_group_payloads([[maybe_unused]] ContextSnapshot snapshot,[[maybe_unused]] std::vector<std::int64_t> ordinals,[[maybe_unused]] ContextReadBound bound={}) override {throw std::logic_error("Unsupported child fixture port: context_group_payloads");}
    std::future<std::vector<ContextHistoryPayload>> context_history_payloads([[maybe_unused]] ContextSnapshot snapshot,[[maybe_unused]] std::vector<std::int64_t> ordinals,[[maybe_unused]] ContextReadBound bound={}) override {throw std::logic_error("Unsupported child fixture port: context_history_payloads");}
    std::future<void> record_context_history_replay([[maybe_unused]] ContextHistoryReplaySpec spec) override {throw std::logic_error("Unsupported child fixture port: record_context_history_replay");}
    std::future<ProviderHistorySnapshot> provider_history([[maybe_unused]] ProviderHistoryOwner owner,[[maybe_unused]] ProviderHistorySelection selection={}) override {throw std::logic_error("Unsupported child fixture port: provider_history");}
    std::future<void> record_provider_history_replay([[maybe_unused]] ProviderHistoryReplaySpec spec) override {throw std::logic_error("Unsupported child fixture port: record_provider_history_replay");}
    std::future<std::optional<ContextProjection>> context_projection([[maybe_unused]] ContextScope scope,[[maybe_unused]] ContextBinding binding,[[maybe_unused]] ContextReadBound bound={}) override {throw std::logic_error("Unsupported child fixture port: context_projection");}
    std::future<ContextStepReservation> begin_context_compaction([[maybe_unused]] ContextCompactionSpec spec) override {throw std::logic_error("Unsupported child fixture port: begin_context_compaction");}
    std::future<ContextStepReservation> start_context_compaction([[maybe_unused]] std::string id,[[maybe_unused]] std::string maintenance_attempt) override {throw std::logic_error("Unsupported child fixture port: start_context_compaction");}
    std::future<ContextProjection> record_context_compaction_response([[maybe_unused]] ContextCompactionCommit result) override {throw std::logic_error("Unsupported child fixture port: record_context_compaction_response");}
    std::future<ContextProjection> commit_context_compaction([[maybe_unused]] ContextCompactionCommit result) override {throw std::logic_error("Unsupported child fixture port: commit_context_compaction");}
    std::future<void> retire_context_compaction([[maybe_unused]] ContextCompactionFailure failure) override {throw std::logic_error("Unsupported child fixture port: retire_context_compaction");}
    std::future<ContextManualRequest> retire_context_request([[maybe_unused]] std::string id,[[maybe_unused]] ContextBinding binding,[[maybe_unused]] ContextFailureCode code) override {throw std::logic_error("Unsupported child fixture port: retire_context_request");}
    std::future<ContextMeasureReservation> begin_context_measure([[maybe_unused]] ContextMeasureRequestSpec spec) override {throw std::logic_error("Unsupported child fixture port: begin_context_measure");}
    std::future<ContextMeasureReservation> start_context_measure([[maybe_unused]] std::string id) override {throw std::logic_error("Unsupported child fixture port: start_context_measure");}
    std::future<void> finish_context_measure([[maybe_unused]] ContextInputMeasure measure) override {throw std::logic_error("Unsupported child fixture port: finish_context_measure");}
    std::future<void> retire_context_measure([[maybe_unused]] std::string id,[[maybe_unused]] ContextFailureCode code,[[maybe_unused]] std::int64_t elapsed_ms) override {throw std::logic_error("Unsupported child fixture port: retire_context_measure");}
    std::future<InferenceStepRecord> reserve_inference_step([[maybe_unused]] InferenceStepSpec spec) override {throw std::logic_error("Unsupported child fixture port: reserve_inference_step");}
    std::future<InferenceStepRecord> reserve_inference_rebuild([[maybe_unused]] InferenceRebuildSpec spec) override {throw std::logic_error("Unsupported child fixture port: reserve_inference_rebuild");}
};

class ChildJournal:public UnsupportedRunJournal {
public:
    explicit ChildJournal(AgentWorkerRpcPeer& peer):peer(peer),client(peer){}
    void own(const std::string& id){if(id!=peer.scope().run_id)throw Conflict("Child journal owner differs");}
    std::future<Run> run(std::string id)override{return ready<Run>([&]{own(id);return client.run(id);});}
    std::future<Session> session(std::string id)override{return ready<Session>([&]{return client.session(id);});}
    std::future<std::vector<Message>> run_history(std::string id)override{return ready<std::vector<Message>>([&]{own(id);return client.run_history(id);});}
    std::future<std::vector<Event>> events(std::string id,std::int64_t after)override{return ready<std::vector<Event>>([&]{own(id);return client.events(id,after);});}
    std::future<RootBudgetRecord> root_budget(std::string id)override{return ready<RootBudgetRecord>([&]{own(id);return client.root_budget(id);});}
    std::future<Run> transition(std::string id,RunState expected,RunState next,std::string json)override{return ready<Run>([&]{own(id);if(expected!=RunState::queued||next!=RunState::running||json!="{}")throw std::logic_error("Unsupported fixture retirement: "+json);return client.begin_run();});}
    std::future<ModelCallReservation> reserve_model_call(std::string root,std::string owner,std::string attempt,ModelCallRole role)override{return ready<ModelCallReservation>([&]{own(root);own(owner);if(role!=ModelCallRole::parent)throw Conflict("Child fixture role differs");return client.reserve_model_call(attempt);});}
    std::future<ModelCallReservation> start_model_call(std::string root,std::string owner,std::string attempt)override{return ready<ModelCallReservation>([&]{own(root);own(owner);auto result=client.start_model_call(attempt);current_attempt=attempt;ordinal=0;return result;});}
    std::future<ModelCallReservation> finish_model_call(std::string root,std::string owner,std::string attempt,std::optional<std::string> assistant)override{return ready<ModelCallReservation>([&]{own(root);own(owner);if(attempt!=current_attempt||!assistant)throw std::logic_error("Unsupported fixture failed-attempt closure");auto result=client.finish_model_call(attempt);canonical=result.actual_assistant_json;return result;});}
    std::future<Event> append_event(std::string id,std::string kind,std::string json)override{return ready<Event>([&]{own(id);if(!kind.starts_with("model.")||current_attempt.empty())throw std::logic_error("Unsupported child event write");auto result=client.acknowledge_model_event(current_attempt,ordinal,kind,json);++ordinal;return result;});}
    std::future<Run> complete_run(std::string id,std::string assistant)override{return ready<Run>([&]{own(id);if(canonical.empty()||assistant!=canonical)throw Conflict("AgentRunner did not consume the backend receipt");return client.complete_run(current_attempt);});}
    AgentWorkerRpcPeer& peer;AgentWorkerJournalClient client;std::string current_attempt,canonical,model_diagnostic;std::int64_t ordinal=0;
};
class ChildModel final:public AgentModelGateway {
public:
    explicit ChildModel(ChildJournal& journal):journal(journal),client(journal.peer){}
    ModelCompletion complete(const ChatProviderConfig& config,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel)override{
        try{
        check(config.endpoint=="worker://captured"&&config.model=="worker-rpc-fixture"&&!journal.current_attempt.empty(),"Child gateway must use captured proxy routing");
        rejects<Conflict>([&]{journal.client.begin_run();});
        return client.complete(journal.current_attempt,request,[&](const ModelEvent& event){
            if(!observed){const auto first=journal.client.acknowledge_model_event(journal.current_attempt,0,event.kind,event.json);const auto duplicate=journal.client.acknowledge_model_event(journal.current_attempt,0,event.kind,event.json);const auto saved=journal.client.events(journal.peer.scope().run_id);check(first.sequence==duplicate.sequence&&std::count_if(saved.begin(),saved.end(),[&](const Event& value){return value.sequence==first.sequence&&value.kind==event.kind&&value.json==event.json;})==1,"Acknowledgement replay must retain the same captured event");
                rejects<Conflict>([&]{journal.client.acknowledge_model_event(journal.current_attempt,0,event.kind,R"({"forged":true})");});rejects<Conflict>([&]{journal.client.acknowledge_model_event(journal.current_attempt,8192,event.kind,event.json);});rejects<Conflict>([&]{journal.client.acknowledge_model_event(journal.current_attempt,0,"tool.started",event.json);});}
            ++observed;sink(event);
        },cancel);
        }catch(const std::exception& error){journal.model_diagnostic=error.what();throw;}
    }
    ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::logic_error("Unsupported child count");}
    ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::logic_error("Unsupported child compaction");}
    ChildJournal& journal;AgentWorkerModelClient client;std::int64_t observed=0;
};
class ClosedCatalog final:public AgentBackendCatalog {
public:
    std::vector<AgentAuthorityCredentialVersion> credential_versions(const AgentSettings&)override{throw std::logic_error("Unsupported child credential catalogue");}
    AgentDefinitionCatalog agent_definitions()override{throw std::logic_error("Unsupported child agent catalogue");}
};
class ClosedTools final:public AgentToolHostFactory {
public:
    std::unique_ptr<AgentFileToolHost> file(AgentFileToolOwnerContext)override{throw std::logic_error("Unsupported child file owner");}
    std::unique_ptr<AgentProcessOwnerHost> process(AgentProcessToolOwnerContext)override{throw std::logic_error("Unsupported child process owner");}
    std::unique_ptr<AgentReadToolHost> read(AgentReadToolOwnerContext)override{throw std::logic_error("Unsupported child read owner");}
    std::unique_ptr<AgentRemoteToolHost> remote(AgentRemoteToolOwnerContext)override{throw std::logic_error("Unsupported child remote owner");}
    std::unique_ptr<AgentGuidanceToolHost> guidance(AgentGuidanceToolOwnerContext)override{throw std::logic_error("Unsupported child guidance owner");}
    std::unique_ptr<AgentCallableToolHost> callable(AgentCallableToolOwnerContext)override{throw std::logic_error("Unsupported child callable owner");}
};
class ClosedRuntime final:public AgentBackendRuntime {
public:
    std::unique_ptr<AgentProcessToolHost> process(WorkspaceTools&,const std::string&,std::vector<ProcessProfile>)override{throw std::logic_error("Unsupported child process runtime");}
    std::shared_ptr<AgentMcpPeer> connect_mcp(const McpServerSetting&,std::chrono::steady_clock::time_point,std::stop_token)override{throw std::logic_error("Unsupported child MCP runtime");}
    A2aRemoteAgentCatalog a2a_catalog()override{throw std::logic_error("Unsupported child A2A catalogue");}
    std::unique_ptr<A2aAgentToolRegistry> discover_a2a(WorkspaceTools&,std::chrono::milliseconds,std::stop_token)override{throw std::logic_error("Unsupported child A2A runtime");}
};
struct Outcome {Run run;std::string attempt,assistant;std::int64_t events;};
inline Outcome execute(AgentWorkerRpcPeer& peer){
    ChildJournal journal(peer);UnsupportedContextJournal context;
    check(journal.client.run(peer.scope().run_id).state==RunState::queued,"Actual child must receive queued admission");
    rejects<Conflict>([&]{journal.client.reserve_model_call("before-start");});
    auto gateway=std::make_shared<ChildModel>(journal);AgentSettings settings;settings.provider={"worker://captured","worker-rpc-fixture",Capability::supported,Capability::supported};settings.instructions.clear();settings.max_turns=8;settings.a2a_enabled=false;
    AgentRunner runner(journal,context,std::move(settings),AgentRunnerPorts{gateway,std::make_shared<ClosedCatalog>(),std::make_shared<ClosedTools>(),std::make_shared<ClosedRuntime>()});
    Run result;try{result=runner.execute(peer.scope().run_id);}catch(const std::exception& error){throw std::runtime_error(std::string("Child AgentRunner: ")+error.what()+"; model diagnostic: "+journal.model_diagnostic);}check(result.state==RunState::completed&&gateway->observed==journal.ordinal&&!journal.canonical.empty(),"Actual child AgentRunner must complete through scoped backend ports");
    return {result,journal.current_attempt,journal.canonical,journal.ordinal};
}
}
