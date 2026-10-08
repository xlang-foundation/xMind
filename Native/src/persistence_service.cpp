#include "agentflow/persistence_service.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/graph.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>

namespace agentflow {
struct PersistenceService::Impl {
    using Request=std::function<void(Repository&)>;
    std::mutex mutex,close_mutex;
    std::condition_variable changed;
    std::deque<Request> pending;
    std::thread worker;
    std::size_t limit;
    bool accepting=true;

    Impl(std::string path,std::vector<std::string> roots,std::size_t max_pending):limit(max_pending) {
        if(limit==0) throw std::invalid_argument("Persistence queue capacity must be positive");
        std::promise<void> ready;auto initialized=ready.get_future();
        worker=std::thread([this,path=std::move(path),roots=std::move(roots),ready=std::move(ready)]() mutable {
            bool started=false;
            try {
                BackendLease lease(path);
                Repository repository(path,roots);
                repository.recover_interrupted(lease);
                ready.set_value();started=true;
                for(;;) {
                    Request request;
                    {
                        std::unique_lock lock(mutex);
                        changed.wait(lock,[this]{return !pending.empty() || !accepting;});
                        if(pending.empty()) break;
                        request=std::move(pending.front());pending.pop_front();
                    }
                    // packaged_task captures operation exceptions in its future.
                    request(repository);
                }
                // Repository destruction precedes lease release, on this thread.
            } catch(...) {
                if(!started) ready.set_exception(std::current_exception());
                std::lock_guard lock(mutex);accepting=false;
                // Destroying tasks resolves their futures with broken_promise.
                pending.clear();
            }
        });
        try {initialized.get();} catch(...) {worker.join();throw;}
    }
    ~Impl() {close();}
    void close() {
        std::lock_guard closing(close_mutex);
        {
            std::lock_guard lock(mutex);accepting=false;
        }
        changed.notify_all();
        if(worker.joinable()) worker.join();
    }
    template<class Function> auto submit(Function action) {
        using Result=std::invoke_result_t<Function,Repository&>;
        auto task=std::make_shared<std::packaged_task<Result(Repository&)>>(std::move(action));
        auto result=task->get_future();
        {
            std::lock_guard lock(mutex);
            if(!accepting) throw PersistenceClosed("Persistence service is closed");
            if(pending.size()>=limit) throw PersistenceBusy("Persistence queue is full");
            pending.emplace_back([task=std::move(task)](Repository& repository){(*task)(repository);});
        }
        changed.notify_one();return result;
    }
};
PersistenceService::PersistenceService(std::string database,std::vector<std::string> roots,std::size_t limit)
    :impl_(std::make_unique<Impl>(std::move(database),std::move(roots),limit)) {}
PersistenceService::~PersistenceService()=default;
void PersistenceService::close() {impl_->close();}
std::future<DynamicPlanCapabilities> PersistenceService::dynamic_capabilities(std::string root) {return impl_->submit([root=std::move(root)](Repository& repository){return repository.dynamic_capabilities(root);});}
std::future<DynamicPlanCapabilities> PersistenceService::finalize_dynamic_capabilities(std::string root,std::string backend_identity,std::string catalogue_json,std::vector<DynamicPresetCapability> presets) {return impl_->submit([root=std::move(root),backend_identity=std::move(backend_identity),catalogue_json=std::move(catalogue_json),presets=std::move(presets)](Repository& repository){return repository.finalize_dynamic_capabilities(root,backend_identity,catalogue_json,presets);});}
std::future<DynamicPlanRecord> PersistenceService::dynamic_plan(std::string plan) {return impl_->submit([plan=std::move(plan)](Repository& repository){return repository.dynamic_plan(plan);});}
std::future<std::optional<DynamicPlanRecord>> PersistenceService::dynamic_plan_for_root(std::string root) {return impl_->submit([root=std::move(root)](Repository& repository){return repository.dynamic_plan_for_root(root);});}
std::future<DynamicPlanCallRecord> PersistenceService::dynamic_plan_call(std::string call) {return impl_->submit([call=std::move(call)](Repository& repository){return repository.dynamic_plan_call(call);});}
std::future<std::vector<DynamicPlanCallRecord>> PersistenceService::dynamic_plan_calls(std::string root) {return impl_->submit([root=std::move(root)](Repository& repository){return repository.dynamic_plan_calls(root);});}
std::future<std::vector<DynamicPlanRevisionRecord>> PersistenceService::dynamic_plan_revisions(std::string plan) {return impl_->submit([plan=std::move(plan)](Repository& repository){return repository.dynamic_plan_revisions(plan);});}
std::future<DynamicPlanRevisionRecord> PersistenceService::dynamic_plan_revision(std::string plan,std::int64_t revision) {return impl_->submit([plan=std::move(plan),revision](Repository& repository){return repository.dynamic_plan_revision(plan,revision);});}
std::future<DynamicPlanCallRecord> PersistenceService::accept_dynamic_plan_change(DynamicPlanChangeSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.accept_dynamic_plan_change(spec);});}
std::future<DynamicFrontierClaim> PersistenceService::admit_dynamic_frontier(DynamicFrontierSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.admit_dynamic_frontier(spec);});}
std::future<DynamicNodeRecord> PersistenceService::settle_dynamic_child(std::string child) {return impl_->submit([child=std::move(child)](Repository& repository){return repository.settle_dynamic_child(child);});}
std::future<DynamicPlanStepResult> PersistenceService::settle_dynamic_plan_step(std::string call) {return impl_->submit([call=std::move(call)](Repository& repository){return repository.settle_dynamic_plan_step(call);});}
std::future<DynamicHumanRequest> PersistenceService::publish_dynamic_human(DynamicHumanRequestSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.publish_dynamic_human(spec);});}
std::future<DynamicHumanRequest> PersistenceService::dynamic_human_request(std::string plan,std::string request) {return impl_->submit([plan=std::move(plan),request=std::move(request)](Repository& repository){return repository.dynamic_human_request(plan,request);});}
std::future<std::vector<DynamicHumanRequest>> PersistenceService::dynamic_human_requests(std::string plan) {return impl_->submit([plan=std::move(plan)](Repository& repository){return repository.dynamic_human_requests(plan);});}
std::future<DynamicHumanRequest> PersistenceService::input_dynamic_human(DynamicHumanInputSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.input_dynamic_human(spec);});}
std::future<DynamicHumanRequest> PersistenceService::expire_dynamic_human(std::string plan,std::string request) {return impl_->submit([plan=std::move(plan),request=std::move(request)](Repository& repository){return repository.expire_dynamic_human(plan,request);});}
std::future<DynamicBudgetSegment> PersistenceService::open_dynamic_budget_segment(DynamicSegmentSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.open_dynamic_budget_segment(spec);});}
std::future<DynamicBudgetSegment> PersistenceService::dynamic_budget_segment(std::string root) {return impl_->submit([root=std::move(root)](Repository& repository){return repository.dynamic_budget_segment(root);});}
std::future<std::optional<ContextPausePin>> PersistenceService::dynamic_context_pause(std::string root) {return impl_->submit([root=std::move(root)](Repository& repository){return repository.dynamic_context_pause(root);});}
std::future<Run> PersistenceService::suspend_dynamic_owner(DynamicPauseSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.suspend_dynamic_owner(spec);});}
std::future<DynamicResumeRecord> PersistenceService::resume_dynamic_owner(DynamicResumeSpec spec) {return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.resume_dynamic_owner(spec);});}
std::future<void> PersistenceService::commit_dynamic_tool_turn(std::string root,std::string call) {return impl_->submit([root=std::move(root),call=std::move(call)](Repository& repository){repository.commit_dynamic_tool_turn(root,call);});}
std::future<void> PersistenceService::record_rejected_dynamic_tool_turn(std::string root,std::string origin_attempt_id,std::string actual_assistant_json,std::string safe_code) {return impl_->submit([root=std::move(root),origin_attempt_id=std::move(origin_attempt_id),actual_assistant_json=std::move(actual_assistant_json),safe_code=std::move(safe_code)](Repository& repository){repository.record_rejected_dynamic_tool_turn(root,origin_attempt_id,actual_assistant_json,safe_code);});}
std::future<ModelCallReservation> PersistenceService::reserve_dynamic_continuation(std::string call,std::string attempt) {return impl_->submit([call=std::move(call),attempt=std::move(attempt)](Repository& repository){return repository.reserve_dynamic_continuation(call,attempt);});}
std::future<Run> PersistenceService::retire_dynamic_owner(std::string root,RunState terminal_state,std::string reason_json,std::string segment_id,std::int64_t measured_active_elapsed_ms) {return impl_->submit([root=std::move(root),terminal_state,reason_json=std::move(reason_json),segment_id=std::move(segment_id),measured_active_elapsed_ms](Repository& repository){return repository.retire_dynamic_owner(root,terminal_state,reason_json,segment_id,measured_active_elapsed_ms);});}
std::future<Run> PersistenceService::complete_dynamic_owner(std::string root,std::string assistant_json,std::string segment_id,std::int64_t measured_active_elapsed_ms) {return impl_->submit([root=std::move(root),assistant_json=std::move(assistant_json),segment_id=std::move(segment_id),measured_active_elapsed_ms](Repository& repository){return repository.complete_dynamic_owner(root,assistant_json,segment_id,measured_active_elapsed_ms);});}
std::future<Session> PersistenceService::create_session(std::string id,std::string title) {
    return impl_->submit([id=std::move(id),title=std::move(title)](Repository& repository){return repository.create_session(id,title);});
}
std::future<Session> PersistenceService::session(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.session(id);});
}
std::future<Session> PersistenceService::rename_session(std::string id,std::string title,std::string expected_title) {
    return impl_->submit([id=std::move(id),title=std::move(title),expected_title=std::move(expected_title)](Repository& repository){return repository.rename_session(id,title,expected_title);});
}
std::future<std::vector<Session>> PersistenceService::sessions() {
    return impl_->submit([](Repository& repository){return repository.sessions();});
}
std::future<Run> PersistenceService::create_run(std::string id,std::string session_id) {
    return impl_->submit([id=std::move(id),session_id=std::move(session_id)](Repository& repository){return repository.create_run(id,session_id);});
}
std::future<Run> PersistenceService::run(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.run(id);});
}
std::future<std::optional<Run>> PersistenceService::incoming_message(std::string message,std::string context,std::string identity,std::string content){return impl_->submit([message=std::move(message),context=std::move(context),identity=std::move(identity),content=std::move(content)](Repository& repository){return repository.incoming_message(message,context,identity,content);});}
std::future<Run> PersistenceService::start_incoming_message(std::string id,std::string context,std::string message,std::string prompt,std::string identity,std::optional<RootBudgetSpec> budget,std::optional<DynamicPlanCapabilities> dynamic){return impl_->submit([id=std::move(id),context=std::move(context),message=std::move(message),prompt=std::move(prompt),identity=std::move(identity),budget=std::move(budget),dynamic=std::move(dynamic)](Repository& repository){return repository.start_incoming_message(id,context,message,prompt,identity,std::move(budget),std::move(dynamic));});}
std::future<std::optional<std::vector<Message>>> PersistenceService::task_history(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.task_history(id);});}
std::future<std::optional<std::string>> PersistenceService::incoming_message_payload(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.incoming_message_payload(id);});}
std::future<std::vector<Event>> PersistenceService::event_batch(std::string id,std::int64_t after,std::size_t count){return impl_->submit([id=std::move(id),after,count](Repository& repository){return repository.event_batch(id,after,count);});}
std::future<RootRunPage> PersistenceService::list_root_runs(std::string context,std::string state,std::optional<std::int64_t> since,std::size_t count,std::int64_t watermark,std::optional<std::int64_t> cursor_ms,std::int64_t cursor_sequence){return impl_->submit([context=std::move(context),state=std::move(state),since,count,watermark,cursor_ms,cursor_sequence](Repository& repository){return repository.list_root_runs(context,state,since,count,watermark,cursor_ms,cursor_sequence);});}
std::future<Run> PersistenceService::start_prompt_run(std::string id,std::string session_id,std::string json,std::optional<RootBudgetSpec> budget,std::optional<DynamicPlanCapabilities> dynamic) {
    return impl_->submit([id=std::move(id),session_id=std::move(session_id),json=std::move(json),budget=std::move(budget),dynamic=std::move(dynamic)](Repository& repository){return repository.start_prompt_run(id,session_id,json,std::move(budget),std::move(dynamic));});
}
std::future<RootBudgetRecord> PersistenceService::root_budget(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.root_budget(id);});}
std::future<ModelCallReservation> PersistenceService::reserve_model_call(std::string root,std::string owner,std::string attempt,ModelCallRole role){return impl_->submit([root=std::move(root),owner=std::move(owner),attempt=std::move(attempt),role](Repository& repository){return repository.reserve_model_call(root,owner,attempt,role);});}
std::future<ModelCallReservation> PersistenceService::start_model_call(std::string root,std::string owner,std::string attempt){return impl_->submit([root=std::move(root),owner=std::move(owner),attempt=std::move(attempt)](Repository& repository){return repository.start_model_call(root,owner,attempt);});}
std::future<ModelCallReservation> PersistenceService::finish_model_call(std::string root,std::string owner,std::string attempt,std::optional<std::string> actual_assistant_json){return impl_->submit([root=std::move(root),owner=std::move(owner),attempt=std::move(attempt),actual_assistant_json=std::move(actual_assistant_json)](Repository& repository){return repository.finish_model_call(root,owner,attempt,std::move(actual_assistant_json));});}
std::future<ContextSnapshot> PersistenceService::context_snapshot(ContextScope scope,ContextBinding binding,ContextReadBound bound){return impl_->submit([scope=std::move(scope),binding=std::move(binding),bound](Repository& r){return r.context_snapshot(scope,binding,bound);});}
std::future<ContextStatusObservation> PersistenceService::context_status_observation(ContextScope scope,ContextBinding binding){return impl_->submit([scope=std::move(scope),binding=std::move(binding)](Repository& r){return r.context_status_observation(scope,binding);});}
std::future<std::vector<ContextGroupPayload>> PersistenceService::context_group_payloads(ContextSnapshot snapshot,std::vector<std::int64_t> ordinals,ContextReadBound bound){return impl_->submit([snapshot=std::move(snapshot),ordinals=std::move(ordinals),bound](Repository& r){return r.context_group_payloads(snapshot,ordinals,bound);});}
std::future<std::optional<ContextProjection>> PersistenceService::context_projection(ContextScope scope,ContextBinding binding,ContextReadBound bound){return impl_->submit([scope=std::move(scope),binding=std::move(binding),bound](Repository& r){return r.context_projection(scope,binding,bound);});}
std::future<ContextStepReservation> PersistenceService::begin_context_compaction(ContextCompactionSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.begin_context_compaction(spec);});}
std::future<ContextStepReservation> PersistenceService::start_context_compaction(std::string id,std::string attempt){return impl_->submit([id=std::move(id),attempt=std::move(attempt)](Repository& r){return r.start_context_compaction(id,attempt);});}
std::future<ContextProjection> PersistenceService::record_context_compaction_response(ContextCompactionCommit result){return impl_->submit([result=std::move(result)](Repository& r){return r.record_context_compaction_response(result);});}
std::future<ContextProjection> PersistenceService::commit_context_compaction(ContextCompactionCommit result){return impl_->submit([result=std::move(result)](Repository& r){return r.commit_context_compaction(result);});}
std::future<void> PersistenceService::retire_context_compaction(ContextCompactionFailure failure){return impl_->submit([failure=std::move(failure)](Repository& r){r.retire_context_compaction(failure);});}
std::future<ContextManualRequest> PersistenceService::request_context_compaction(ContextManualRequestSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.request_context_compaction(spec);});}
std::future<ContextManualRequest> PersistenceService::context_manual_request(std::string id,ContextBinding binding){return impl_->submit([id=std::move(id),binding=std::move(binding)](Repository& r){return r.context_manual_request(id,binding);});}
std::future<std::optional<ContextManualRequest>> PersistenceService::context_current_manual_request(ContextScope scope,ContextBinding binding){return impl_->submit([scope=std::move(scope),binding=std::move(binding)](Repository& r){return r.context_current_manual_request(scope,binding);});}
std::future<ContextManualRequest> PersistenceService::retire_context_request(std::string id,ContextBinding binding,ContextFailureCode code){return impl_->submit([id=std::move(id),binding=std::move(binding),code](Repository& r){return r.retire_context_request(id,binding,code);});}
std::future<IdleContextOwnerRecord> PersistenceService::claim_idle_context_owner(IdleContextOwnerSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.claim_idle_context_owner(spec);});}
std::future<IdleContextOwnerRecord> PersistenceService::idle_context_owner(std::string id){return impl_->submit([id=std::move(id)](Repository& r){return r.idle_context_owner(id);});}
std::future<void> PersistenceService::retire_idle_context_owner(std::string id,ContextFailureCode code,std::int64_t elapsed){return impl_->submit([id=std::move(id),code,elapsed](Repository& r){r.retire_idle_context_owner(id,code,elapsed);});}
std::future<ContextMeasureReservation> PersistenceService::begin_context_measure(ContextMeasureRequestSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.begin_context_measure(spec);});}
std::future<ContextMeasureReservation> PersistenceService::start_context_measure(std::string id){return impl_->submit([id=std::move(id)](Repository& r){return r.start_context_measure(id);});}
std::future<void> PersistenceService::finish_context_measure(ContextInputMeasure measure){return impl_->submit([measure=std::move(measure)](Repository& r){r.finish_context_measure(measure);});}
std::future<void> PersistenceService::retire_context_measure(std::string id,ContextFailureCode code,std::int64_t elapsed){return impl_->submit([id=std::move(id),code,elapsed](Repository& r){r.retire_context_measure(id,code,elapsed);});}
std::future<InferenceStepRecord> PersistenceService::reserve_inference_step(InferenceStepSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.reserve_inference_step(spec);});}
std::future<InferenceStepRecord> PersistenceService::inference_step(std::string id){return impl_->submit([id=std::move(id)](Repository& r){return r.inference_step(id);});}
std::future<InferenceStepRecord> PersistenceService::finish_inference_attempt(InferenceAttemptResult result){return impl_->submit([result=std::move(result)](Repository& r){return r.finish_inference_attempt(result);});}
std::future<InferenceStepRecord> PersistenceService::reserve_inference_rebuild(InferenceRebuildSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.reserve_inference_rebuild(spec);});}
std::future<DelegationBatchRecord> PersistenceService::accept_delegation_batch(DelegationBatchSpec spec){return impl_->submit([spec=std::move(spec)](Repository& repository){return repository.accept_delegation_batch(spec);});}
std::future<DelegationBatchRecord> PersistenceService::delegation_batch(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.delegation_batch(id);});}
std::future<std::vector<DelegationBatchRecord>> PersistenceService::delegation_batches(std::string parent){return impl_->submit([parent=std::move(parent)](Repository& repository){return repository.delegation_batches(parent);});}
std::future<DelegationTaskRecord> PersistenceService::settle_delegation_child(std::string child){return impl_->submit([child=std::move(child)](Repository& repository){return repository.settle_delegation_child(child);});}
std::future<DelegationBatchRecord> PersistenceService::settle_delegation_batch(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.settle_delegation_batch(id);});}
std::future<ChildAdmissionRecord> PersistenceService::child_admission(std::string child){return impl_->submit([child=std::move(child)](Repository& repository){return repository.child_admission(child);});}
std::future<std::vector<OwnedChildRecord>> PersistenceService::owned_children(std::string parent){return impl_->submit([parent=std::move(parent)](Repository& repository){return repository.owned_children(parent);});}
std::future<std::vector<Message>> PersistenceService::owned_child_history(std::string parent,std::string child){return impl_->submit([parent=std::move(parent),child=std::move(child)](Repository& repository){return repository.owned_child_history(parent,child);});}
std::future<std::vector<Event>> PersistenceService::tree_events(std::string parent,std::int64_t after,std::size_t count){return impl_->submit([parent=std::move(parent),after,count](Repository& repository){return repository.tree_events(parent,after,count);});}
std::future<void> PersistenceService::append_user_message(std::string id,std::string json) {
    return impl_->submit([id=std::move(id),json=std::move(json)](Repository& repository){repository.append_user_message(id,json);});
}
std::future<Run> PersistenceService::start_graph_run(std::string id,std::string session,std::string graph,std::int64_t revision,GraphPlan plan,std::string prompt,std::optional<RootBudgetSpec> budget,std::optional<GraphContextSpec> context){return impl_->submit([id=std::move(id),session=std::move(session),graph=std::move(graph),revision,plan=std::move(plan),prompt=std::move(prompt),budget=std::move(budget),context=std::move(context)](Repository& repository){return repository.start_graph_run(id,session,graph,revision,plan,prompt,budget,context);});}
std::future<GraphContextOwnerRecord> PersistenceService::graph_context_owner(std::string root){return impl_->submit([root=std::move(root)](Repository& r){return r.graph_context_owner(root);});}
std::future<GraphContextOpenRecord> PersistenceService::open_graph_context_owner(GraphContextOpenSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.open_graph_context_owner(spec);});}
std::future<GraphContextOpenRecord> PersistenceService::resume_graph_context_owner(GraphContextOpenSpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.resume_graph_context_owner(spec);});}
std::future<GraphRootRecord> PersistenceService::suspend_graph_context_owner(GraphContextBoundarySpec spec){return impl_->submit([spec=std::move(spec)](Repository& r){return r.suspend_graph_context_owner(spec);});}
std::future<Run> PersistenceService::complete_graph_context_owner(GraphContextBoundarySpec spec,std::string join){return impl_->submit([spec=std::move(spec),join=std::move(join)](Repository& r){return r.complete_graph_context_owner(spec,join);});}
std::future<Run> PersistenceService::retire_graph_context_owner(std::string root,RunState terminal,std::string reason,std::string segment,std::int64_t elapsed){return impl_->submit([root=std::move(root),terminal,reason=std::move(reason),segment=std::move(segment),elapsed](Repository& r){return r.retire_graph_context_owner(root,terminal,reason,segment,elapsed);});}
std::future<GraphRootRecord> PersistenceService::graph_run(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.graph_run(id);});}
std::future<Run> PersistenceService::start_graph_child(std::string id,std::string parent,std::string node,std::string prompt,std::int64_t expected){return impl_->submit([id=std::move(id),parent=std::move(parent),node=std::move(node),prompt=std::move(prompt),expected](Repository& repository){return repository.start_graph_child(id,parent,node,prompt,expected);});}
std::future<GraphRootRecord> PersistenceService::settle_graph_child(std::string id,std::int64_t expected){return impl_->submit([id=std::move(id),expected](Repository& repository){return repository.settle_graph_child(id,expected);});}
std::future<GraphRootRecord> PersistenceService::start_graph_human(std::string id,std::string node,std::int64_t expected){return impl_->submit([id=std::move(id),node=std::move(node),expected](Repository& repository){return repository.start_graph_human(id,node,expected);});}
std::future<GraphRootRecord> PersistenceService::input_graph_human(std::string id,std::string node,std::string input,std::string actor,std::int64_t expected){return impl_->submit([id=std::move(id),node=std::move(node),input=std::move(input),actor=std::move(actor),expected](Repository& repository){return repository.input_graph_human(id,node,input,actor,expected);});}
std::future<GraphRootRecord> PersistenceService::skip_graph_node(std::string id,std::string node,std::int64_t expected){return impl_->submit([id=std::move(id),node=std::move(node),expected](Repository& repository){return repository.skip_graph_node(id,node,expected);});}
std::future<Run> PersistenceService::retire_graph_run(std::string id,RunState state,std::string reason){return impl_->submit([id=std::move(id),state,reason=std::move(reason)](Repository& repository){return repository.retire_graph_run(id,state,reason);});}
std::future<std::vector<Run>> PersistenceService::children(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.children(id);});}
std::future<std::vector<Message>> PersistenceService::run_history(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.run_history(id);});}
std::future<std::vector<Event>> PersistenceService::graph_events(std::string id,std::int64_t after){return impl_->submit([id=std::move(id),after](Repository& repository){return repository.graph_events(id,after);});}
std::future<void> PersistenceService::record_tool_turn(std::string id,std::string assistant,std::vector<std::string> tools) {
    return impl_->submit([id=std::move(id),assistant=std::move(assistant),tools=std::move(tools)](Repository& repository){repository.record_tool_turn(id,assistant,tools);});
}
std::future<Run> PersistenceService::complete_run(std::string id,std::string assistant) {
    return impl_->submit([id=std::move(id),assistant=std::move(assistant)](Repository& repository){return repository.complete_run(id,assistant);});
}
std::future<std::vector<Run>> PersistenceService::runs(std::string session_id) {
    return impl_->submit([session_id=std::move(session_id)](Repository& repository){return repository.runs(session_id);});
}
std::future<Run> PersistenceService::transition(std::string id,RunState expected,RunState next,std::string json) {
    return impl_->submit([id=std::move(id),expected,next,json=std::move(json)](Repository& repository){return repository.transition(id,expected,next,json);});
}
std::future<Event> PersistenceService::append_event(std::string id,std::string kind,std::string json) {
    return impl_->submit([id=std::move(id),kind=std::move(kind),json=std::move(json)](Repository& repository){return repository.append_event(id,kind,json);});
}
std::future<std::vector<Event>> PersistenceService::events(std::string id,std::int64_t after) {
    return impl_->submit([id=std::move(id),after](Repository& repository){return repository.events(id,after);});
}
std::future<void> PersistenceService::append_message(std::string id,std::string role,std::string json) {
    return impl_->submit([id=std::move(id),role=std::move(role),json=std::move(json)](Repository& repository){repository.append_message(id,role,json);});
}
std::future<std::vector<Message>> PersistenceService::history(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.history(id);});
}
std::future<void> PersistenceService::put_information(std::string category,std::string id,std::string json) {
    return impl_->submit([category=std::move(category),id=std::move(id),json=std::move(json)](Repository& repository){repository.put_information(category,id,json);});
}
std::future<std::string> PersistenceService::information(std::string category,std::string id) {
    return impl_->submit([category=std::move(category),id=std::move(id)](Repository& repository){return repository.information(category,id);});
}
std::future<void> PersistenceService::compare_information(std::string category,std::string id,std::string json,std::optional<std::string> expected) {
    return impl_->submit([category=std::move(category),id=std::move(id),json=std::move(json),expected=std::move(expected)](Repository& repository){repository.compare_information(category,id,json,expected);});
}
std::future<CredentialMetadata> PersistenceService::put_credential(std::string scope,std::string id,
    std::string purpose,std::string label,SecretBytes secret,std::int64_t revision) {
    return impl_->submit([scope=std::move(scope),id=std::move(id),purpose=std::move(purpose),label=std::move(label),secret=std::move(secret),revision](Repository& repository){
        return repository.put_credential(scope,id,purpose,label,secret,revision);
    });
}
std::future<Operation> PersistenceService::request_operation(std::string id,OperationSpec spec,std::int64_t expiry) {
    return impl_->submit([id=std::move(id),spec=std::move(spec),expiry](Repository& repository){return repository.request_operation(id,spec,expiry);});
}
std::future<Operation> PersistenceService::operation(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.operation(id);});
}
std::future<std::vector<Operation>> PersistenceService::operations(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.operations(id);});
}
std::future<Operation> PersistenceService::decide_operation(std::string id,OperationDecision decision,std::string actor) {
    return impl_->submit([id=std::move(id),decision,actor=std::move(actor)](Repository& repository){return repository.decide_operation(id,decision,actor);});
}
std::future<Operation> PersistenceService::claim_operation(std::string id,OperationSpec spec) {
    return impl_->submit([id=std::move(id),spec=std::move(spec)](Repository& repository){return repository.claim_operation(id,spec);});
}
std::future<Operation> PersistenceService::finish_operation(std::string id,OperationState outcome,std::string result) {
    return impl_->submit([id=std::move(id),outcome,result=std::move(result)](Repository& repository){return repository.finish_operation(id,outcome,result);});
}
std::future<Operation> PersistenceService::cancel_operation(std::string id,OperationSpec spec) {
    return impl_->submit([id=std::move(id),spec=std::move(spec)](Repository& repository){return repository.cancel_operation(id,spec);});
}
std::future<Operation> PersistenceService::expire_operation(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.expire_operation(id);});
}
std::future<std::vector<CredentialMetadata>> PersistenceService::credentials(std::string scope) {
    return impl_->submit([scope=std::move(scope)](Repository& repository){return repository.credentials(scope);});
}
std::future<SecretBytes> PersistenceService::resolve_credential(std::string scope,std::string id,std::string purpose) {
    return impl_->submit([scope=std::move(scope),id=std::move(id),purpose=std::move(purpose)](Repository& repository){return repository.resolve_credential(scope,id,purpose);});
}
std::future<void> PersistenceService::delete_credential(std::string scope,std::string id,std::int64_t revision) {
    return impl_->submit([scope=std::move(scope),id=std::move(id),revision](Repository& repository){repository.delete_credential(scope,id,revision);});
}
}
