#include "agentflow/agent_worker_run_journal.hpp"
#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/dynamic_plan_execution.hpp"
#include "agentflow/agent_terminal_handoff.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <mutex>
#include <set>
#include <type_traits>
#include <utility>

namespace agentflow {
namespace {
using Json=nlohmann::json;
template<class T,class F> std::future<T> owned(F action){
    std::promise<T> result;auto future=result.get_future();
    try{if constexpr(std::is_void_v<T>){action();result.set_value();}else result.set_value(action());}
    catch(const AgentWorkerAuthorityCancelled&){result.set_exception(std::make_exception_ptr(TransportCancelled("Worker journal authority cancelled")));}
    catch(const AgentWorkerAuthorityExpired&){result.set_exception(std::make_exception_ptr(TransportTimeout("Worker journal authority expired")));}
    catch(const AgentWorkerRpcCancelled&){result.set_exception(std::make_exception_ptr(TransportCancelled("Worker journal RPC cancelled")));}
    catch(const AgentWorkerRpcTimeout&){result.set_exception(std::make_exception_ptr(TransportTimeout("Worker journal RPC deadline elapsed")));}
    catch(...){result.set_exception(std::current_exception());}return future;
}
class OrdinaryJournal final:public AgentWorkerOrdinaryJournal {
public:
    OrdinaryJournal(AgentWorkerClientContext& peer,std::stop_token cancel,bool workspace=false):peer_(&peer),client_(peer),scope_(peer.scope()),cancel_(cancel),workspace_(workspace){
        const auto run=client_.run(scope_.run_id);const auto budget=client_.root_budget(scope_.root_id);
        if(scope_.root_id!=scope_.run_id){
            admission_=client_.child_admission(scope_.run_id);const bool graph=admission_->kind==ChildAdmissionKind::graph_agent;const bool delegated_planning=admission_->kind==ChildAdmissionKind::delegated_leaf&&budget.spec.policy_id=="native.dynamic-plan";
            if(admission_->kind==ChildAdmissionKind::dynamic_agent){if(!peer.native_model()||budget.spec.policy_id!="native.dynamic-plan"||budget.spec.policy_revision!=1||budget.root_run_id!=scope_.root_id||budget.spec.workspace_identity!=scope_.workspace_id||run.parent_id.empty()||run.graph_root||(run.state!=RunState::queued&&run.state!=RunState::running)||(workspace_&&(admission_->preset_id!="workspace.coding"||!client_.coding_workspace_admitted())))throw Conflict("Dynamic child journal differs from its captured SDK admission");if(!peer.native_model()->planning(client_.execution_deadline())->healthy())throw Conflict("Dynamic child planning service is unavailable");planning_=true;session_=run.session_id;return;}
            if((workspace_&&(!graph||!client_.coding_workspace_admitted()))||run.parent_id.empty()||(!graph&&run.parent_id!=scope_.root_id)||run.graph_root||(run.state!=RunState::queued&&run.state!=RunState::running)||budget.root_run_id!=scope_.root_id||(graph?budget.spec.policy_id!="native.graph-context":(admission_->kind!=ChildAdmissionKind::delegated_leaf||(budget.spec.policy_id!="native.delegation"&&!delegated_planning)))||budget.spec.policy_revision!=1||budget.spec.workspace_identity!=scope_.workspace_id||(graph&&(budget.spec.max_children!=8||budget.spec.max_parallel<1||budget.spec.max_parallel>2||budget.spec.max_model_calls!=32)))throw Conflict("Worker child journal differs from captured admission");
            session_=run.session_id;return;
        }
        const bool delegation=budget.spec.policy_id=="native.delegation";
        if(budget.spec.policy_id=="native.dynamic-plan"){auto native=peer.native_model();if(!native||budget.root_run_id!=scope_.root_id||!run.parent_id.empty()||run.graph_root||run.state!=RunState::running||budget.spec.policy_revision!=1||budget.spec.workspace_identity!=scope_.workspace_id||budget.spec.max_children<1||budget.spec.max_children>32||budget.spec.max_parallel<1||budget.spec.max_parallel>2||!native->planning(client_.execution_deadline())->healthy())throw Conflict("Worker planning journal differs from its captured SDK admission");planning_=true;session_=run.session_id;return;}
        if(budget.root_run_id!=scope_.root_id||!run.parent_id.empty()||run.graph_root||(run.state!=RunState::queued&&run.state!=RunState::running)||(!delegation&&budget.spec.policy_id!="native.direct-agent"&&budget.spec.policy_id!="native.context")||budget.spec.policy_revision!=1||(delegation?(budget.spec.max_children<1||budget.spec.max_children>8||budget.spec.max_parallel<1||budget.spec.max_parallel>2):(budget.spec.max_children!=0||budget.spec.max_parallel!=0))||budget.spec.workspace_identity!=scope_.workspace_id)
            throw Conflict("Worker journal admission is not an ordinary non-delegating root");
        session_=run.session_id;
    }
    bool bound_to(const AgentWorkerClientContext& peer)const noexcept override{return peer_==&peer;}
    AgentWorkerJournalProgress progress()const override{std::lock_guard lock(mutex_);return progress_;}
    std::future<Run> run(std::string id)override{return owned<Run>([&]{std::lock_guard lock(mutex_);own(id);return client_.run(id);});}
    std::future<Session> session(std::string id)override{return owned<Session>([&]{std::lock_guard lock(mutex_);if(id!=session_)throw Conflict("Worker session differs from captured admission");return client_.session(id);});}
    std::future<std::vector<Message>> run_history(std::string id)override{return owned<std::vector<Message>>([&]{std::lock_guard lock(mutex_);own(id);return client_.run_history(id);});}
    std::future<std::vector<Event>> events(std::string id,std::int64_t after)override{return owned<std::vector<Event>>([&]{std::lock_guard lock(mutex_);own(id);return client_.events(id,after);});}
    std::future<RootBudgetRecord> root_budget(std::string id)override{return owned<RootBudgetRecord>([&]{std::lock_guard lock(mutex_);root(id);return client_.root_budget(id);});}
    std::future<Run> transition(std::string id,RunState expected,RunState next,std::string json)override{return owned<Run>([&]{
        std::lock_guard lock(mutex_);own(id);
        if((expected==RunState::queued||expected==RunState::running)&&(next==RunState::failed||next==RunState::cancelled))throw AgentTerminalHandoff(next);
        if(expected!=RunState::queued||next!=RunState::running||json!="{}")throw Conflict("Worker journal cannot select this lifecycle transition");
        live();return client_.begin_run();
    });}
    std::future<ModelCallReservation> reserve_model_call(std::string root,std::string owner,std::string attempt,ModelCallRole role)override{return owned<ModelCallReservation>([&]{
        std::lock_guard lock(mutex_);this->root(root);own(owner);if(role!=(scope_.root_id==scope_.run_id?ModelCallRole::parent:ModelCallRole::leaf))throw Conflict("Ordinary worker funding role differs");live();return client_.reserve_model_call(attempt);
    });}
    std::future<ModelCallReservation> start_model_call(std::string root,std::string owner,std::string attempt)override{return owned<ModelCallReservation>([&]{
        std::lock_guard lock(mutex_);this->root(root);own(owner);live();auto actual=client_.start_model_call(attempt);progress_.current_attempt=attempt;progress_.canonical_assistant.clear();progress_.model_event_ordinal=0;calls_.clear();return actual;
    });}
    std::future<ModelCallReservation> finish_model_call(std::string root,std::string owner,std::string attempt,std::optional<std::string> assistant)override{return owned<ModelCallReservation>([&]{
        std::lock_guard lock(mutex_);this->root(root);own(owner);if(attempt!=progress_.current_attempt)throw Conflict("Worker model attempt changed before retirement");
        if(!assistant)throw AgentTerminalHandoff(cancel_.stop_requested()?RunState::cancelled:RunState::failed);
        // The supplied assistant is never sent as authority. Backend formatting
        // and supplied model facts define the canonical receipt and tool calls.
        auto actual=client_.finish_model_call(attempt);if(actual.actual_assistant_json.empty())throw Conflict("Worker model lacks a canonical backend assistant");
        auto projected=model_message_from_history({0,"assistant",actual.actual_assistant_json});calls_=std::move(projected.tool_calls);progress_.canonical_assistant=actual.actual_assistant_json;return actual;
    });}
    std::future<Event> append_event(std::string id,std::string kind,std::string json)override{return owned<Event>([&]{
        std::lock_guard lock(mutex_);own(id);if(progress_.current_attempt.empty())throw Conflict("Worker activity has no captured model attempt");
        if(kind.starts_with("model.")){auto actual=client_.acknowledge_model_event(progress_.current_attempt,progress_.model_event_ordinal,kind,json);++progress_.model_event_ordinal;++progress_.model_events;return actual;}
        if(kind!="tool.started"&&kind!="tool.completed"&&kind!="tool.failed")throw std::logic_error("Worker journal cannot append arbitrary events");
        const auto call_id=Json::parse(json).at("call_id").get<std::string>();const auto call=std::find_if(calls_.begin(),calls_.end(),[&](const auto& value){return value.id==call_id;});if(call==calls_.end())throw Conflict("Worker activity differs from its canonical model calls");
        auto actual=workspace_?client_.acknowledge_workspace_tool_event(progress_.current_attempt,std::distance(calls_.begin(),call),kind!="tool.started",kind,json):client_.acknowledge_tool_event(progress_.current_attempt,std::distance(calls_.begin(),call),kind!="tool.started",kind,json);if(activity_.insert(actual.sequence).second)++progress_.tool_events;return actual;
    });}
    std::future<void> record_tool_turn(std::string id,std::string assistant,std::vector<std::string> results,std::optional<SkillSelections> skills)override{return owned<void>([&]{
        std::lock_guard lock(mutex_);own(id);if(skills||calls_.empty()||assistant!=progress_.canonical_assistant||results.size()!=calls_.size())throw Conflict("Worker turn differs from its captured read batch");
        const auto receipt=workspace_?client_.commit_workspace_turn(progress_.current_attempt):client_.commit_read_turn(progress_.current_attempt);const auto history=client_.run_history(id);if(receipt.message_sequences.size()!=results.size()+1)throw Conflict("Worker canonical read receipt has a different batch size");
        for(std::size_t i=0;i<receipt.message_sequences.size();++i){const auto actual=std::find_if(history.begin(),history.end(),[&](const Message& value){return value.sequence==receipt.message_sequences[i];});if(actual==history.end()||actual->role!=(i?"tool":"assistant")||actual->json!=(i?results[i-1]:assistant))throw Conflict("Worker read turn differs from backend committed facts");}
        if(turns_.insert(progress_.current_attempt).second)++progress_.tool_turns;
    });}
    std::future<Run> complete_run(std::string id,std::string assistant)override{return owned<Run>([&]{
        std::lock_guard lock(mutex_);own(id);if(progress_.canonical_assistant.empty()||assistant!=progress_.canonical_assistant||!calls_.empty())throw Conflict("Worker completion differs from its canonical final model");return client_.complete_run(progress_.current_attempt);
    });}
    std::future<bool> append_a2a_task_event([[maybe_unused]] std::string run_id,[[maybe_unused]] std::string operation_id,[[maybe_unused]] std::string json)override{throw std::logic_error("Ordinary worker journal capability is not admitted: append_a2a_task_event");}
    std::future<ChildAdmissionRecord> child_admission(std::string child)override{return owned<ChildAdmissionRecord>([&]{std::lock_guard lock(mutex_);own(child);if(!admission_)throw Conflict("Root worker has no child admission");return client_.child_admission(child);});}
    std::future<void> commit_dynamic_tool_turn([[maybe_unused]] std::string root,[[maybe_unused]] std::string call)override{throw std::logic_error("Ordinary worker journal capability is not admitted: commit_dynamic_tool_turn");}
    std::future<Run> complete_dynamic_owner([[maybe_unused]] std::string root,[[maybe_unused]] std::string assistant_json,[[maybe_unused]] std::string segment_id,[[maybe_unused]] std::int64_t measured_active_elapsed_ms)override{throw std::logic_error("Ordinary worker journal capability is not admitted: complete_dynamic_owner");}
    std::future<ContextStatusObservation> context_status_observation([[maybe_unused]] ContextScope scope,[[maybe_unused]] ContextBinding binding)override{throw std::logic_error("Ordinary worker journal capability is not admitted: context_status_observation");}
    std::future<DynamicPlanCapabilities> dynamic_capabilities([[maybe_unused]] std::string root)override{throw std::logic_error("Ordinary worker journal capability is not admitted: dynamic_capabilities");}
    std::future<std::optional<ContextPausePin>> dynamic_context_pause([[maybe_unused]] std::string root)override{throw std::logic_error("Ordinary worker journal capability is not admitted: dynamic_context_pause");}
    std::future<DynamicPlanRecord> dynamic_plan([[maybe_unused]] std::string plan)override{throw std::logic_error("Ordinary worker journal capability is not admitted: dynamic_plan");}
    std::future<std::vector<DynamicPlanCallRecord>> dynamic_plan_calls([[maybe_unused]] std::string root)override{throw std::logic_error("Ordinary worker journal capability is not admitted: dynamic_plan_calls");}
    std::future<std::optional<DynamicPlanRecord>> dynamic_plan_for_root([[maybe_unused]] std::string root)override{throw std::logic_error("Ordinary worker journal capability is not admitted: dynamic_plan_for_root");}
    std::future<std::optional<DynamicPlanRecord>> dynamic_plan_for_owner([[maybe_unused]] std::string owner)override{throw std::logic_error("Ordinary worker journal capability is not admitted: dynamic_plan_for_owner");}
    std::future<DynamicPlanCapabilities> finalize_dynamic_capabilities([[maybe_unused]] std::string root,[[maybe_unused]] std::string backend_identity,[[maybe_unused]] std::string catalogue_json,[[maybe_unused]] std::vector<DynamicPresetCapability> presets)override{throw std::logic_error("Ordinary worker journal capability is not admitted: finalize_dynamic_capabilities");}
    std::future<InferenceStepRecord> finish_inference_attempt([[maybe_unused]] InferenceAttemptResult result)override{throw std::logic_error("Ordinary worker journal capability is not admitted: finish_inference_attempt");}
    std::future<InferenceStepRecord> inference_step([[maybe_unused]] std::string id)override{throw std::logic_error("Ordinary worker journal capability is not admitted: inference_step");}
    std::future<Operation> finish_operation([[maybe_unused]] std::string id,[[maybe_unused]] OperationState outcome,[[maybe_unused]] std::string result_json)override{throw std::logic_error("Ordinary worker journal capability is not admitted: finish_operation");}
    std::future<GraphContextOwnerRecord> graph_context_owner([[maybe_unused]] std::string root)override{throw std::logic_error("Ordinary worker journal capability is not admitted: graph_context_owner");}
    std::future<IdleContextOwnerRecord> idle_context_owner([[maybe_unused]] std::string id)override{throw std::logic_error("Ordinary worker journal capability is not admitted: idle_context_owner");}
    std::future<SkillSelections> initialize_run_skills([[maybe_unused]] std::string id,[[maybe_unused]] std::string workspace_id)override{throw std::logic_error("Ordinary worker journal capability is not admitted: initialize_run_skills");}
    std::future<DynamicBudgetSegment> open_dynamic_budget_segment([[maybe_unused]] DynamicSegmentSpec spec)override{throw std::logic_error("Ordinary worker journal capability is not admitted: open_dynamic_budget_segment");}
    std::future<void> record_rejected_dynamic_tool_turn([[maybe_unused]] std::string root,[[maybe_unused]] std::string origin_attempt_id,[[maybe_unused]] std::string actual_assistant_json,[[maybe_unused]] std::string safe_code)override{throw std::logic_error("Ordinary worker journal capability is not admitted: record_rejected_dynamic_tool_turn");}
    std::future<Run> retire_dynamic_owner([[maybe_unused]] std::string root,[[maybe_unused]] RunState terminal_state,[[maybe_unused]] std::string reason_json,[[maybe_unused]] std::string segment_id={},[[maybe_unused]] std::int64_t measured_active_elapsed_ms=0)override{throw std::logic_error("Ordinary worker journal capability is not admitted: retire_dynamic_owner");}
    std::future<DynamicResumeRecord> resume_dynamic_owner([[maybe_unused]] DynamicResumeSpec spec)override{throw std::logic_error("Ordinary worker journal capability is not admitted: resume_dynamic_owner");}
    std::future<DynamicPlanCallRecord> dynamic_plan_call([[maybe_unused]] std::string call)override{throw std::logic_error("Ordinary worker journal capability is not admitted: dynamic_plan_call");}
    std::future<std::vector<DynamicPlanRecord>> dynamic_plans_for_root([[maybe_unused]] std::string root)override{throw std::logic_error("Ordinary worker journal capability is not admitted: dynamic_plans_for_root");}
    std::future<ModelCallReservation> reserve_dynamic_continuation(std::string call,std::string attempt)override{return owned<ModelCallReservation>([&]{std::lock_guard lock(mutex_);if(!planning_)throw Conflict("Worker has no captured planning continuation");live();return client_.reserve_planning_continuation(call,attempt);});}
    std::future<std::vector<OwnedChildRecord>> owned_children([[maybe_unused]] std::string parent)override{throw std::logic_error("Ordinary worker journal capability is not admitted: owned_children");}
    std::future<Run> suspend_dynamic_owner([[maybe_unused]] DynamicPauseSpec spec)override{throw std::logic_error("Ordinary worker journal capability is not admitted: suspend_dynamic_owner");}
    std::future<Operation> request_operation([[maybe_unused]] std::string id,[[maybe_unused]] OperationSpec spec,[[maybe_unused]] std::int64_t expires_unix_ms)override{throw std::logic_error("Ordinary worker journal capability is not admitted: request_operation");}
    std::future<Operation> operation([[maybe_unused]] std::string id)override{throw std::logic_error("Ordinary worker journal capability is not admitted: operation");}
    std::future<Operation> decide_operation([[maybe_unused]] std::string id,[[maybe_unused]] OperationDecision decision,[[maybe_unused]] std::string actor)override{throw std::logic_error("Ordinary worker journal capability is not admitted: decide_operation");}
    std::future<Operation> claim_operation([[maybe_unused]] std::string id,[[maybe_unused]] OperationSpec actual)override{throw std::logic_error("Ordinary worker journal capability is not admitted: claim_operation");}
    std::future<Operation> cancel_operation([[maybe_unused]] std::string id,[[maybe_unused]] OperationSpec actual)override{throw std::logic_error("Ordinary worker journal capability is not admitted: cancel_operation");}
    std::future<Operation> expire_operation([[maybe_unused]] std::string id)override{throw std::logic_error("Ordinary worker journal capability is not admitted: expire_operation");}
    std::future<Run> start_prompt_run([[maybe_unused]] std::string id,[[maybe_unused]] std::string session_id,[[maybe_unused]] std::string prompt_json,[[maybe_unused]] std::optional<RootBudgetSpec> budget={},[[maybe_unused]] std::optional<DynamicPlanCapabilities> dynamic={},[[maybe_unused]] std::string inbox_id={})override{throw std::logic_error("Ordinary worker journal capability is not admitted: start_prompt_run");}

private:
    void root(const std::string& id)const{if(id!=scope_.root_id)throw Conflict("Worker journal root differs from captured budget");}
    std::optional<ChildAdmissionRecord> admission_;
    void own(const std::string& id)const{if(id!=scope_.run_id)throw Conflict("Worker journal owner differs from captured root");}
    void live()const{if(cancel_.stop_requested())throw AgentWorkerAuthorityCancelled("Worker journal execution cancelled");}
    AgentWorkerClientContext* peer_;AgentWorkerJournalClient client_;AgentWorkerScope scope_;std::stop_token cancel_;std::string session_;bool workspace_=false,planning_=false;
    mutable std::mutex mutex_;AgentWorkerJournalProgress progress_;std::vector<ModelToolCall> calls_;std::set<std::int64_t> activity_;std::set<std::string> turns_;
};
}
std::unique_ptr<AgentWorkerOrdinaryJournal> make_agent_worker_ordinary_journal(AgentWorkerClientContext& peer,std::stop_token cancel){
    try{return std::make_unique<OrdinaryJournal>(peer,cancel);}
    catch(const AgentWorkerRpcCancelled&){throw TransportCancelled("Worker journal admission cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw TransportTimeout("Worker journal admission deadline elapsed");}
}
std::unique_ptr<AgentWorkerOrdinaryJournal> make_agent_worker_coding_journal(AgentWorkerClientContext& peer,std::stop_token cancel){
    try{return std::make_unique<OrdinaryJournal>(peer,cancel,true);}
    catch(const AgentWorkerRpcCancelled&){throw TransportCancelled("Worker journal admission cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw TransportTimeout("Worker journal admission deadline elapsed");}
}

}
