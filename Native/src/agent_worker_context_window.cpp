#include "agentflow/agent_worker_context_window.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/agent_model_values.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>

namespace agentflow {
namespace {AgentModelGateway& required_gateway(const std::shared_ptr<AgentModelGateway>& gateway){if(!gateway)throw std::invalid_argument("Context owner requires its backend gateway");return *gateway;}}
AgentWorkerContextWindow::AgentWorkerContextWindow(PersistenceService& store,AgentWorkerLeaseSpec lease,
    ChatProviderConfig provider,AgentWorkerContextPolicy policy,std::shared_ptr<AgentModelGateway> gateway,
    Preview preview,Admit admit,AgentWorkerChannel::Deadline deadline,std::stop_token cancel)
    :store_(store),lease_(std::move(lease)),provider_(std::move(provider)),policy_(std::move(policy)),gateway_(std::move(gateway)),
     preview_(std::move(preview)),admit_(std::move(admit)),deadline_(deadline),owner_cancel_(cancel),
     budget_(store,store.root_budget(lease_.root_run_id).get(),deadline,cancel),
     manager_(store,required_gateway(gateway_),provider_,policy_.runtime,model_message_from_history){
    if(!gateway_||!preview_||!admit_||lease_.owner_run_id!=lease_.root_run_id||
       store.root_budget(lease_.root_run_id).get().spec.policy_id!="native.context"||
       policy_.binding.provider_identity_json!=lease_.provider_identity_json)
        throw Conflict("Worker context differs from captured ordinary context root");
    const auto run=store.run(lease_.owner_run_id).get();owner_.scope={ContextScopeKind::session,run.session_id};
    owner_.root_run_id=lease_.root_run_id;owner_.owner_run_id=lease_.owner_run_id;
    owner_.role=ModelCallRole::parent;owner_.binding=policy_.binding;
}
void AgentWorkerContextWindow::check(std::stop_token cancel){
    if(cancel.stop_requested()||owner_cancel_.stop_requested())throw AgentWorkerAuthorityCancelled("Context owner cancelled");
    if(std::chrono::steady_clock::now()>=deadline_)throw AgentWorkerAuthorityExpired("Context owner deadline elapsed");
    (void)store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get();
}
ModelRequest AgentWorkerContextWindow::prepare(std::stop_token cancel){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker context owner is busy");check(cancel);
    if(prepared_)return prepared_->request;
    store_.validate_agent_worker_model_window(lease_,deadline_,owner_cancel_,cancel).get();
    raw_=preview_(cancel);auto trusted=raw_;
    std::erase_if(trusted.messages,[](const auto& message){return message.role!=MessageRole::system&&message.role!=MessageRole::developer;});
    trusted.canonical_window.reset();
    prepared_=manager_.prepare(owner_,trusted,budget_,nullptr,cancel);handed_out_=false;check(cancel);return prepared_->request;
}
void AgentWorkerContextWindow::admit(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker context owner is busy");check(cancel);
    if(!prepared_||!handed_out_||attempt!=prepared_->inference.attempt_id||encode_agent_model_request(request)!=encode_agent_model_request(prepared_->request))throw Conflict("Worker changed its funded context window");
    admit_(attempt,raw_,cancel);
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
