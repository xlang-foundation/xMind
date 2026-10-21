#include "agentflow/agent_worker_context_window.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/agent_model_values.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>

namespace agentflow {
namespace {RootExecutionBudget context_budget(PersistenceService& store,const AgentWorkerLeaseSpec& lease,AgentWorkerChannel::Deadline deadline,std::stop_token cancel){
    auto record=store.root_budget(lease.root_run_id).get();
    if(record.spec.policy_id=="native.graph-context"){const auto captured=store.graph_context_owner(lease.root_run_id).get();if(!captured.segment||captured.segment->state!="open")throw Conflict("Graph worker context requires its owning active segment");return RootExecutionBudget(store,std::move(record),deadline,cancel,captured.segment,captured.authority.backend_identity);}
    return RootExecutionBudget(store,std::move(record),deadline,cancel);
}
AgentModelGateway& required_gateway(const std::shared_ptr<AgentModelGateway>& gateway){if(!gateway)throw std::invalid_argument("Context owner requires its backend gateway");return *gateway;}}
AgentWorkerContextWindow::AgentWorkerContextWindow(PersistenceService& store,AgentWorkerLeaseSpec lease,
    ChatProviderConfig provider,AgentWorkerContextPolicy policy,std::shared_ptr<AgentModelGateway> gateway,
    Preview preview,Admit admit,AgentWorkerChannel::Deadline deadline,std::stop_token cancel)
    :store_(store),lease_(std::move(lease)),provider_(std::move(provider)),policy_(std::move(policy)),gateway_(std::move(gateway)),
     preview_(std::move(preview)),admit_(std::move(admit)),deadline_(deadline),owner_cancel_(cancel),
     budget_(context_budget(store,lease_,deadline,cancel)),
     manager_(store,required_gateway(gateway_),provider_,policy_.runtime,model_message_from_history){
    const bool leaf=lease_.owner_run_id!=lease_.root_run_id;
    if(leaf)(void)store.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::child_admission,lease_.owner_run_id}).get();
    if(!gateway_||!preview_||!admit_||
       (leaf?(store.root_budget(lease_.root_run_id).get().spec.policy_id!="native.delegation"&&store.root_budget(lease_.root_run_id).get().spec.policy_id!="native.graph-context"):(store.root_budget(lease_.root_run_id).get().spec.policy_id!="native.context"&&store.root_budget(lease_.root_run_id).get().spec.policy_id!="native.delegation"))||
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
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker context owner is busy");check(cancel);
    if(rejected_)throw Conflict("Rejected worker context requires its one typed rebuild");
    if(prepared_)return prepared_->request;
    store_.validate_agent_worker_model_window(lease_,deadline_,owner_cancel_,cancel).get();
    raw_=preview_(cancel);auto trusted=raw_;
    std::erase_if(trusted.messages,[](const auto& message){return message.role!=MessageRole::system&&message.role!=MessageRole::developer;});
    trusted.canonical_window.reset();
    prepared_=manager_.prepare(owner_,trusted,budget_,nullptr,cancel);handed_out_=false;rejected_=false;rebuild_used_=false;check(cancel);return prepared_->request;
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
    auto rebuilt=manager_.rebuild(owner_,trusted,*prepared_,budget_,nullptr,cancel);prepared_=std::move(rebuilt);rejected_=false;handed_out_=false;check(cancel);return prepared_->request;
}
ModelCallReservation AgentWorkerContextWindow::reserve(const std::string&,std::stop_token cancel){
    std::lock_guard lock(mutex_);check(cancel);if(!prepared_||handed_out_)throw Conflict("Worker context reservation is not available");
    handed_out_=true;return prepared_->inference;
}
ModelCallReservation AgentWorkerContextWindow::start(const std::string& attempt,std::stop_token cancel){
    std::lock_guard lock(mutex_);check(cancel);if(!prepared_||!handed_out_||attempt!=prepared_->inference.attempt_id)throw Conflict("Worker start differs from its funded context attempt");
    budget_.start(prepared_->inference,cancel);auto result=prepared_->inference;result.state="started";return result;
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
