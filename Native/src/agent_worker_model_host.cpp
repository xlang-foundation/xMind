#include "agentflow/agent_worker_model_host.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/deepseek_provider.hpp"
#include "agentflow/gemini_history.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <exception>

namespace agentflow {
namespace {
using Json=nlohmann::json;
std::string wire_name(ProviderWire wire){switch(wire){case ProviderWire::chat_completions:return "chat-completions";case ProviderWire::responses:return "responses";case ProviderWire::anthropic_messages:return "anthropic-messages";case ProviderWire::gemini_generate_content:return "gemini-generate-content";}throw std::invalid_argument("Unsupported worker model wire");}
std::string request_bytes(const ChatProviderConfig& config,const ModelRequest& input){
    const auto request=project_provider_history(config,input);
    if(config.chat_dialect!=ChatDialect::openai&&((config.chat_dialect!=ChatDialect::deepseek&&config.chat_dialect!=ChatDialect::mistral)||config.wire!=ProviderWire::chat_completions))throw std::invalid_argument("Invalid worker model dialect");
    switch(config.wire){
    case ProviderWire::chat_completions:return config.chat_dialect==ChatDialect::deepseek?serialize_deepseek_request(config,request):config.chat_dialect==ChatDialect::mistral?serialize_mistral_request(config,request):serialize_chat_request(config,request);
    case ProviderWire::responses:return serialize_responses_request(config,request);
    case ProviderWire::anthropic_messages:return serialize_anthropic_request(config,request);
    case ProviderWire::gemini_generate_content:
        if(config.reasoning_effort||(request.include_usage&&config.stream_usage!=Capability::supported)||(request.max_output_tokens&&config.output_limit!=Capability::supported))throw std::invalid_argument("Unsupported worker Gemini controls");
        return serialize_gemini_request(gemini_model_request(request,config.tools));
    }throw std::invalid_argument("Unsupported worker model wire");
}

}
AgentWorkerModelHost::AgentWorkerModelHost(PersistenceService& store,AgentWorkerLeaseSpec lease,ChatProviderConfig provider,
    std::vector<ModelToolDefinition> tools,AgentWorkerModelPolicy policy,std::shared_ptr<AgentModelGateway> gateway,std::chrono::steady_clock::time_point deadline,std::stop_token owner_cancel)
    :store_(store),lease_(std::move(lease)),provider_(std::move(provider)),tools_(std::move(tools)),policy_(std::move(policy)),gateway_(std::move(gateway)),deadline_(deadline),owner_cancel_(owner_cancel){
    if(!gateway_)throw std::invalid_argument("Worker model host requires its captured backend gateway");const auto identity=Json::parse(lease_.provider_identity_json);
    if(bool(policy_.request_context)!=bool(policy_.admit_context))throw std::invalid_argument("Worker request context requires paired backend preview and admission");
    if(identity.at("model_id")!=provider_.model||identity.at("wire")!=wire_name(provider_.wire))throw Conflict("Worker provider differs from captured admission");
    if(!policy_.assistant_provider_context_json.empty()&&Json::parse(policy_.assistant_provider_context_json)!=identity)throw Conflict("Worker assistant context differs from captured admission");
    const auto saved=store_.agent_worker_lease(lease_.generation).get();if(saved.state!="active"||saved.spec!=lease_)throw Conflict("Worker model host has no exact captured lease");
}
AgentWorkerModelAdmission AgentWorkerModelHost::admission(std::stop_token cancel){
    std::unique_lock lock(invocation_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker model owner is busy");
    const auto check=[&]{if(owner_cancel_.stop_requested()||cancel.stop_requested())throw TransportCancelled("Worker model admission cancelled");if(std::chrono::steady_clock::now()>=deadline_)throw TransportTimeout("Worker model admission deadline elapsed");};check();
    (void)store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get();check();
    AgentWorkerModelAdmission result;result.model_id=provider_.model;result.wire=provider_.wire;result.tools=provider_.tools;result.stream_usage=provider_.stream_usage;result.output_limit=provider_.output_limit;result.reasoning=provider_.reasoning;result.reasoning_effort=provider_.reasoning_effort;result.chat_dialect=provider_.chat_dialect;result.include_usage=policy_.include_usage;result.max_output_tokens=policy_.max_output_tokens;result.owned_request_window=bool(policy_.request_context);return result;
}
ModelRequest AgentWorkerModelHost::prepare_request(std::stop_token cancel){
    std::unique_lock lock(invocation_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker model owner is busy");std::stop_source linked;std::stop_callback owner(owner_cancel_,[&]{linked.request_stop();});std::stop_callback caller(cancel,[&]{linked.request_stop();});
    const auto check=[&]{if(linked.stop_requested())throw TransportCancelled("Worker context owner cancelled");if(std::chrono::steady_clock::now()>=deadline_)throw TransportTimeout("Worker context deadline elapsed");};check();
    if(!policy_.request_context)throw Conflict("Worker model owner has no admitted request context");
    (void)store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get();auto request=policy_.request_context(linked.get_token());check();return request;
}
ModelRequest AgentWorkerModelHost::rebuild_request(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){
    std::unique_lock lock(invocation_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker context owner is busy");std::stop_source linked;std::stop_callback owner(owner_cancel_,[&]{linked.request_stop();});std::stop_callback caller(cancel,[&]{linked.request_stop();});
    if(linked.stop_requested())throw TransportCancelled("Worker context rebuild cancelled");if(std::chrono::steady_clock::now()>=deadline_)throw TransportTimeout("Worker context rebuild deadline elapsed");
    if(!policy_.rebuild_context)throw Conflict("Worker context rebuild is unavailable");(void)store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get();return policy_.rebuild_context(attempt,request,linked.get_token());
}
ModelCompletion AgentWorkerModelHost::complete(const std::string& attempt,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel){
    std::lock_guard lock(invocation_);std::stop_source linked;std::stop_callback owner(owner_cancel_,[&]{linked.request_stop();});std::stop_callback caller(cancel,[&]{linked.request_stop();});
    auto check=[&]{if(linked.stop_requested())throw TransportCancelled("Worker model owner cancelled");if(std::chrono::steady_clock::now()>=deadline_)throw TransportTimeout("Worker model root deadline elapsed");};check();if(!sink)throw std::invalid_argument("Worker model event sink is required");
    if(request.include_usage!=policy_.include_usage||request.max_output_tokens!=policy_.max_output_tokens)throw Conflict("Worker changed its admitted model controls");
    if(!policy_.request_context){if(request.tools.size()!=tools_.size())throw Conflict("Worker changed its admitted model tool catalogue");for(std::size_t i=0;i<tools_.size();++i)if(request.tools[i].name!=tools_[i].name||request.tools[i].description!=tools_[i].description||request.tools[i].input_schema_json!=tools_[i].input_schema_json)throw Conflict("Worker changed its admitted model tool catalogue");}
    if(policy_.request_context&&encode_agent_model_request(policy_.request_context(linked.get_token()))!=encode_agent_model_request(request))throw Conflict("Worker request differs from backend guidance or history");
    const auto bytes=request_bytes(provider_,request);AgentWorkerModelClaimSpec claim{lease_.generation,lease_.root_run_id,lease_.owner_run_id,attempt,context_digest(bytes)};check();store_.claim_agent_worker_model(claim).get();
    ModelCompletion response;const auto started=std::chrono::steady_clock::now();std::optional<std::int64_t> first_token;
    try{check();if(policy_.admit_context)policy_.admit_context(attempt,request,linked.get_token());check();auto selected=provider_;const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline_-std::chrono::steady_clock::now());if(remaining.count()<1)throw TransportTimeout("Worker model root deadline elapsed");selected.deadline=std::min(selected.deadline,remaining);selected.idle_timeout=std::min(selected.idle_timeout,selected.deadline);
        response=gateway_->complete(selected,request,[&](const ModelEvent& event){check();if(!first_token&&(event.kind=="model.text"||event.kind=="model.refusal"||event.kind=="model.tool_delta"||event.kind=="model.reasoning"))first_token=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();store_.record_agent_worker_model_event(claim,event.kind,event.json).get();sink(event);},linked.get_token());
    }catch(...){const auto failure=std::current_exception();AgentWorkerModelFailure code=AgentWorkerModelFailure::dispatch_error;
        try{std::rethrow_exception(failure);}
        catch(const TransportCancelled&){code=std::chrono::steady_clock::now()>=deadline_?AgentWorkerModelFailure::deadline:AgentWorkerModelFailure::cancelled;}
        catch(const AgentWorkerAuthorityCancelled&){code=AgentWorkerModelFailure::cancelled;}
        catch(const TransportTimeout&){code=AgentWorkerModelFailure::deadline;}
        catch(const AgentWorkerAuthorityExpired&){code=AgentWorkerModelFailure::deadline;}
        catch(const ProviderHttpError& error){code=AgentWorkerModelFailure::provider_failure;
            if(error.status==400&&error.code=="context_length_exceeded"&&!first_token&&!linked.stop_requested()&&policy_.reject_context){
                try{if(policy_.reject_context(attempt,request,std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count(),linked.get_token()))code=AgentWorkerModelFailure::context_overflow;}
                catch(...){throw AgentWorkerModelOutcomeUnrecorded("Context rejection proof could not be recorded");}
            }
        }
        catch(const ModelProtocolError&){code=AgentWorkerModelFailure::provider_failure;}
        catch(const TransportError&){code=AgentWorkerModelFailure::provider_failure;}
        catch(...){}
        try{store_.interrupt_agent_worker_model(claim,code).get();}catch(...){throw AgentWorkerModelOutcomeUnrecorded("Worker model dispatch outcome could not be recorded");}std::rethrow_exception(failure);}
    const auto actual=encode_agent_model_completion(response);std::string assistant;
    try{assistant=format_agent_model_assistant(response,provider_.model,std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count(),first_token,policy_.assistant_provider_context_json);}
    catch(...){const auto failure=std::current_exception();try{store_.record_agent_worker_model_response(claim,actual).get();}catch(...){throw AgentWorkerModelOutcomeUnrecorded("Actual worker model response could not be recorded");}std::rethrow_exception(failure);}
    try{store_.record_agent_worker_model_response(claim,actual,assistant).get();}catch(...){throw AgentWorkerModelOutcomeUnrecorded("Actual worker model response could not be recorded");}return response;
}
}
