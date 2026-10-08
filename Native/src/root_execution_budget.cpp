#include "agentflow/root_execution_budget.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "agentflow/context_selection.hpp"
#include <random>
#include <sstream>
#include <iomanip>

namespace agentflow {
namespace {
std::string attempt_id(){std::random_device source;std::ostringstream out;out<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)out<<std::setw(8)<<source();return out.str();}
}
RootExecutionBudget::RootExecutionBudget(PersistenceService& store,RootBudgetRecord record,
    std::chrono::steady_clock::time_point deadline,std::stop_token cancel,
    std::optional<DynamicBudgetSegment> segment,std::string backend_identity)
    :store_(store),root_(std::move(record.root_run_id)),policy_(record.spec.policy_id),deadline_(deadline),cancel_(cancel),parallel_(static_cast<std::size_t>(record.spec.max_parallel)),
      segment_(std::move(segment)),backend_identity_(std::move(backend_identity)),active_started_(std::chrono::steady_clock::now()){
    if(root_.empty() || record.spec.max_parallel<0 || record.spec.max_parallel>2||
       (record.spec.max_parallel==0&&record.spec.policy_id!="native.context"))throw std::invalid_argument("Invalid root execution budget");
    if(segment_&&((record.spec.policy_id!="native.dynamic-plan"&&record.spec.policy_id!="native.graph-context")||segment_->root_run_id!=root_||segment_->state!="open"||
        segment_->id.empty()||segment_->remaining_active_ms<1||segment_->remaining_active_ms>record.spec.wall_limit_ms||
        backend_identity_.size()!=64||backend_identity_.find_first_not_of("0123456789abcdef")!=std::string::npos))
        throw std::invalid_argument("Invalid active planning budget segment");
    if(segment_){
        active_started_=deadline_-std::chrono::milliseconds(segment_->remaining_active_ms);
        if(active_started_>std::chrono::steady_clock::now())throw std::invalid_argument("Planning clock starts after actual owner activation");
    }
}
void RootExecutionBudget::check(std::stop_token cancel)const{
    if(cancel_.stop_requested() || cancel.stop_requested())throw TransportCancelled("Root execution cancelled");
    if(segment_&&segment_->state!="open")throw DynamicPlanUnavailable("Planning active segment is closed");
    if(std::chrono::steady_clock::now()>=deadline_)throw RootBudgetDeadlineExceeded("Root execution deadline elapsed");
}
RootBudgetRecord RootExecutionBudget::snapshot()const{return store_.root_budget(root_).get();}
ModelCallReservation RootExecutionBudget::reserve(const std::string& owner,ModelCallRole role,std::stop_token cancel){
    check(cancel);return store_.reserve_model_call(root_,owner,attempt_id(),role).get();
}
void RootExecutionBudget::start(const ModelCallReservation& reservation,std::stop_token cancel){
    check(cancel);
    if(reservation.root_run_id!=root_)throw Conflict("Model call belongs to another execution budget");
    store_.start_model_call(root_,reservation.owner_run_id,reservation.attempt_id).get();
}
void RootExecutionBudget::finish(const ModelCallReservation& reservation,const std::string& actual_assistant_json){
    if(reservation.root_run_id!=root_)throw Conflict("Model call belongs to another execution budget");
    try{store_.finish_model_call(root_,reservation.owner_run_id,reservation.attempt_id,
        actual_assistant_json.empty()?std::nullopt:std::optional<std::string>(actual_assistant_json)).get();}
    catch(...){throw BudgetOutcomeUnrecorded("Model call retirement could not be recorded; stop execution and recover ownership");}
}
ModelCallReservation RootExecutionBudget::reserve_continuation(const std::string& call,std::stop_token cancel){
    check(cancel);
    if(!segment_||policy_!="native.dynamic-plan")throw Conflict("A planning continuation requires its active root segment");
    const auto accepted=store_.dynamic_plan_call(call).get();
    if(accepted.root_run_id!=root_)throw Conflict("Planning call belongs to another root budget");
    // A repeated native wake can inspect the old binding, never mint an attempt.
    const auto attempt=accepted.continuation_attempt_id.empty()?attempt_id():accepted.continuation_attempt_id;
    return store_.reserve_dynamic_continuation(call,attempt).get();
}
void RootExecutionBudget::pin_context(const ContextSnapshot& snapshot){
    check();const auto root=store_.run(root_).get();
    if(policy_!="native.dynamic-plan"||root.graph_root||!root.parent_id.empty()||snapshot.head.scope.kind!=ContextScopeKind::session||snapshot.head.scope.id!=root.session_id)
        throw ContextBindingChanged("Paused context does not belong to the ordinary root");
    std::vector<std::int64_t> users;for(const auto& user:snapshot.protected_users)users.push_back(user.ordinal);
    context_pin_=ContextPausePin{snapshot.head,snapshot.source_watermark,
        context_source_binding(snapshot,snapshot.source_watermark),context_user_binding(snapshot,users)};
}
void RootExecutionBudget::adopt_context_pin(const ContextPausePin& pin){
    check();const auto root=store_.run(root_).get();
    if(policy_!="native.dynamic-plan"||!segment_open()||root.graph_root||!root.parent_id.empty()||pin.head.scope.kind!=ContextScopeKind::session||
       pin.head.scope.id!=root.session_id||pin.head.binding.authority_identity!=backend_identity_)
        throw ContextBindingChanged("Resumed context pin does not belong to the exact dynamic owner");
    context_pin_=pin;
}
std::int64_t RootExecutionBudget::active_elapsed_ms()const{
    if(!segment_open())throw Conflict("Execution has no open planning segment");
    // Round consumption upward; repeated pauses cannot regain sub-ms time.
    return std::chrono::ceil<std::chrono::milliseconds>(std::chrono::steady_clock::now()-active_started_).count();
}
const std::string& RootExecutionBudget::segment_id()const{
    if(!segment_)throw Conflict("Execution has no active planning segment");
    return segment_->id;
}
Run RootExecutionBudget::suspend_for_human(const DynamicPlanRecord& plan,const std::string& call){
    check();
    if(policy_!="native.dynamic-plan"||!segment_||plan.root_run_id!=root_||active_leaves_.load()!=0)
        throw Conflict("Human pause requires the exact drained planning owner");
    const auto budget=snapshot();
    DynamicPauseSpec spec;spec.plan_id=plan.id;spec.plan_call_id=call;spec.segment_id=segment_->id;
    spec.backend_identity=backend_identity_;spec.expected_revision=plan.revision;
    spec.expected_state_sequence=plan.state_sequence;spec.expected_budget_revision=budget.revision;
    spec.measured_active_elapsed_ms=active_elapsed_ms();
    spec.context_pin=context_pin_;
    const auto paused=store_.suspend_dynamic_owner(std::move(spec)).get();
    segment_->state="closed";
    return paused;
}
bool RootExecutionBudget::try_acquire_leaf()noexcept{
    if(segment_&&segment_->state!="open")return false;
    auto count=active_leaves_.load();while(count<parallel_)if(active_leaves_.compare_exchange_weak(count,count+1))return true;return false;
}
void RootExecutionBudget::release_leaf()noexcept{active_leaves_.fetch_sub(1);}
}
