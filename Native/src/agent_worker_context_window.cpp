#include "agentflow/agent_worker_context_window.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/agent_model_values.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>

namespace agentflow {
namespace {std::shared_ptr<RootExecutionBudget> context_budget(PersistenceService& store,const AgentWorkerLeaseSpec& lease,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::shared_ptr<RootExecutionBudget> shared){
    auto record=store.root_budget(lease.root_run_id).get();
    if(record.spec.policy_id=="native.dynamic-plan"){
        const auto segment=store.dynamic_budget_segment(lease.root_run_id).get();
        if(!shared||shared->root_id()!=lease.root_run_id||shared->deadline()!=deadline||!shared->segment_open()||segment.state!="open"||shared->segment_id()!=segment.id)throw Conflict("Planning context requires its exact shared active budget");
        if(lease.owner_run_id!=lease.root_run_id){const auto child=std::get<ChildAdmissionRecord>(store.read_agent_worker_journal(lease,{AgentWorkerJournalReadKind::child_admission,lease.owner_run_id}).get());if(child.root_run_id!=lease.root_run_id||(child.kind!=ChildAdmissionKind::dynamic_agent&&child.kind!=ChildAdmissionKind::delegated_leaf))throw Conflict("Shared planning child context requires exact typed child admission");}
        return shared;
    }
    if(shared)throw Conflict("Ordinary worker context cannot adopt a planning budget");
    if(record.spec.policy_id=="native.graph-context"){const auto captured=store.graph_context_owner(lease.root_run_id).get();if(!captured.segment||captured.segment->state!="open")throw Conflict("Graph worker context requires its owning active segment");return std::make_shared<RootExecutionBudget>(store,std::move(record),deadline,cancel,captured.segment,captured.authority.backend_identity);}
    return std::make_shared<RootExecutionBudget>(store,std::move(record),deadline,cancel);
}
AgentModelGateway& required_gateway(const std::shared_ptr<AgentModelGateway>& gateway){if(!gateway)throw std::invalid_argument("Context owner requires its backend gateway");return *gateway;}}
AgentWorkerContextWindow::AgentWorkerContextWindow(PersistenceService& store,AgentWorkerLeaseSpec lease,
    ChatProviderConfig provider,AgentWorkerContextPolicy policy,std::shared_ptr<AgentModelGateway> gateway,
    Preview preview,Admit admit,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::shared_ptr<RootExecutionBudget> shared_budget)
    :store_(store),lease_(std::move(lease)),provider_(std::move(provider)),policy_(std::move(policy)),gateway_(std::move(gateway)),
     preview_(std::move(preview)),admit_(std::move(admit)),deadline_(deadline),owner_cancel_(cancel),
     budget_(context_budget(store,lease_,deadline,cancel,std::move(shared_budget))),
     manager_(store,required_gateway(gateway_),provider_,policy_.runtime,model_message_from_history){
    const bool leaf=lease_.owner_run_id!=lease_.root_run_id;
    if(leaf)(void)store.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::child_admission,lease_.owner_run_id}).get();
    if(!gateway_||!preview_||!admit_||
       (leaf?(store.root_budget(lease_.root_run_id).get().spec.policy_id!="native.delegation"&&store.root_budget(lease_.root_run_id).get().spec.policy_id!="native.graph-context"&&store.root_budget(lease_.root_run_id).get().spec.policy_id!="native.dynamic-plan"):(store.root_budget(lease_.root_run_id).get().spec.policy_id!="native.context"&&store.root_budget(lease_.root_run_id).get().spec.policy_id!="native.delegation"&&store.root_budget(lease_.root_run_id).get().spec.policy_id!="native.dynamic-plan"))||
       policy_.binding.provider_identity_json!=lease_.provider_identity_json)
        throw Conflict("Worker context differs from captured ordinary context root");
    const auto run=store.run(lease_.owner_run_id).get();owner_.scope={leaf?ContextScopeKind::execution:ContextScopeKind::session,leaf?run.id:run.session_id};
    owner_.root_run_id=lease_.root_run_id;owner_.owner_run_id=lease_.owner_run_id;
    owner_.role=leaf?ModelCallRole::leaf:ModelCallRole::parent;owner_.binding=policy_.binding;
}
void AgentWorkerContextWindow::check(std::stop_token cancel){
    if(cancel.stop_requested()||owner_cancel_.stop_requested())throw AgentWorkerAuthorityCancelled("Context owner cancelled");
    if(std::chrono::steady_clock::now()>=deadline_)throw AgentWorkerAuthorityExpired("Context owner deadline elapsed");
    (void)store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get();
}
ModelRequest AgentWorkerContextWindow::prepare(std::stop_token cancel){
    return prepare_window({},cancel);
}
ModelRequest AgentWorkerContextWindow::prepare_planning_continuation(const std::string& call,std::stop_token cancel){return prepare_window(call,cancel);}
ModelRequest AgentWorkerContextWindow::prepare_window(std::optional<std::string> call,std::stop_token cancel){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker context owner is busy");check(cancel);
    if(rejected_)throw Conflict("Rejected worker context requires its one typed rebuild");
    if(prepared_){if(call&&call!=owner_.planning_call_id)throw Conflict("Prepared context belongs to another planning call");return prepared_->request;}
    if(!call&&budget_->snapshot().spec.policy_id=="native.dynamic-plan"){
        const auto events=store_.events(lease_.owner_run_id).get();
        for(const auto& accepted:store_.dynamic_plan_calls(lease_.root_run_id).get())if(accepted.owner_run_id==lease_.owner_run_id&&accepted.state=="turn_committed"){
            bool completed=false;if(!accepted.continuation_attempt_id.empty())try{const auto receipt=store_.agent_worker_model_claim(lease_.root_run_id,accepted.continuation_attempt_id).get();completed=receipt.spec.generation==lease_.generation&&receipt.state=="responded"&&std::any_of(events.begin(),events.end(),[&](const auto& event){return event.kind=="budget.model_call.finished"&&nlohmann::json::parse(event.json).value("attempt_id",std::string{})==accepted.continuation_attempt_id;});}catch(const NotFound&){}
            if(!completed)throw Conflict("Planning continuation requires typed context preparation");
        }
    }
    if(call){const auto accepted=store_.dynamic_plan_call(*call).get();if(accepted.root_run_id!=lease_.root_run_id||accepted.owner_run_id!=lease_.owner_run_id||!accepted.conversation_commit_seq||accepted.state!="turn_committed")throw Conflict("Planning context requires its signed committed call");}
    owner_.planning_call_id=std::move(call);
    store_.validate_agent_worker_model_window(lease_,deadline_,owner_cancel_,cancel).get();
    raw_=preview_(cancel);auto trusted=raw_;
    std::erase_if(trusted.messages,[](const auto& message){return message.role!=MessageRole::system&&message.role!=MessageRole::developer;});
    trusted.canonical_window.reset();
    prepared_=manager_.prepare(owner_,trusted,*budget_,nullptr,cancel);if(lease_.root_run_id==lease_.owner_run_id&&store_.root_budget(lease_.root_run_id).get().spec.policy_id=="native.dynamic-plan")budget_->pin_context(prepared_->snapshot);handed_out_=false;rejected_=false;rebuild_used_=false;check(cancel);return prepared_->request;
}
void AgentWorkerContextWindow::admit(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker context owner is busy");check(cancel);
    if(!prepared_||!handed_out_||attempt!=prepared_->inference.attempt_id||encode_agent_model_request(request)!=encode_agent_model_request(prepared_->request))throw Conflict("Worker changed its funded context window");
    admit_(attempt,raw_,cancel);
}
bool AgentWorkerContextWindow::reject(const std::string& attempt,const ModelRequest& request,std::int64_t elapsed,std::stop_token cancel){
    std::lock_guard lock(mutex_);check(cancel);if(!policy_.runtime.compaction.automatic||rebuild_used_)return false;
    if(!prepared_||!handed_out_||rejected_||attempt!=prepared_->inference.attempt_id||encode_agent_model_request(request)!=encode_agent_model_request(prepared_->request))throw Conflict("Context rejection differs from its actual funded request");
    store_.finish_inference_attempt({prepared_->inference_step_id,attempt,InferenceAttemptOutcome::context_overflow,{},"context_length_exceeded",elapsed}).get();rejected_=true;return true;
}
ModelRequest AgentWorkerContextWindow::rebuild(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){
    std::lock_guard lock(mutex_);check(cancel);
    if(!prepared_||!rejected_||rebuild_used_||attempt!=prepared_->inference.attempt_id||encode_agent_model_request(request)!=encode_agent_model_request(prepared_->request))throw Conflict("Context rebuild differs from its one actual rejection");
    const auto claim=store_.agent_worker_model_claim(lease_.root_run_id,attempt).get();if(claim.spec.generation!=lease_.generation||claim.spec.owner_run_id!=lease_.owner_run_id||claim.state!="rejected")throw Conflict("Context rebuild lacks its backend rejection receipt");
    rebuild_used_=true;auto trusted=raw_;std::erase_if(trusted.messages,[](const auto& message){return message.role!=MessageRole::system&&message.role!=MessageRole::developer;});trusted.canonical_window.reset();
    auto rebuilt=manager_.rebuild(owner_,trusted,*prepared_,*budget_,nullptr,cancel);prepared_=std::move(rebuilt);if(lease_.root_run_id==lease_.owner_run_id&&store_.root_budget(lease_.root_run_id).get().spec.policy_id=="native.dynamic-plan")budget_->pin_context(prepared_->snapshot);rejected_=false;handed_out_=false;check(cancel);return prepared_->request;
}
ModelCallReservation AgentWorkerContextWindow::reserve(const std::string&,std::stop_token cancel){
    std::lock_guard lock(mutex_);check(cancel);if(!prepared_||handed_out_)throw Conflict("Worker context reservation is not available");
    if(owner_.planning_call_id)throw Conflict("Planning context requires its typed reservation");
    handed_out_=true;return prepared_->inference;
}
ModelCallReservation AgentWorkerContextWindow::reserve_planning_continuation(const std::string& call,const std::string& attempt,std::stop_token cancel){
    std::lock_guard lock(mutex_);check(cancel);
    if(!prepared_||rejected_||owner_.planning_call_id!=call||(!attempt.empty()&&prepared_->inference.attempt_id!=attempt))throw Conflict("Planning reservation differs from its prepared context");
    const auto actual=store_.reserve_agent_worker_planning_continuation(lease_,call,prepared_->inference.attempt_id,deadline_,owner_cancel_,cancel).get();
    if(actual.inference_step_id!=prepared_->inference_step_id)throw Conflict("Planning reservation lost its counted inference step");
    handed_out_=true;return actual;
}
ModelCallReservation AgentWorkerContextWindow::start(const std::string& attempt,std::stop_token cancel){
    std::lock_guard lock(mutex_);check(cancel);if(!prepared_||!handed_out_||attempt!=prepared_->inference.attempt_id)throw Conflict("Worker start differs from its funded context attempt");
    budget_->start(prepared_->inference,cancel);auto result=prepared_->inference;result.state="started";return result;
}
ModelCallReservation AgentWorkerContextWindow::finish(const std::string& attempt,std::stop_token cancel){
    std::lock_guard lock(mutex_);check(cancel);if(!prepared_||!handed_out_||attempt!=prepared_->inference.attempt_id)throw Conflict("Worker completion differs from its context attempt");
    const auto claim=store_.agent_worker_model_claim(lease_.root_run_id,attempt).get();
    if(claim.spec.generation!=lease_.generation||claim.spec.owner_run_id!=lease_.owner_run_id||claim.state!="responded"||claim.actual_assistant_json.empty())throw Conflict("Context completion requires the actual backend assistant");
    const auto elapsed=nlohmann::json::parse(claim.actual_assistant_json).at("elapsed_ms").get<std::int64_t>();
    store_.finish_inference_attempt({prepared_->inference_step_id,attempt,InferenceAttemptOutcome::completed,claim.actual_assistant_json,"",elapsed}).get();
    auto result=prepared_->inference;result.state="finished";result.actual_assistant_json=claim.actual_assistant_json;prepared_.reset();handed_out_=false;return result;
}
}
