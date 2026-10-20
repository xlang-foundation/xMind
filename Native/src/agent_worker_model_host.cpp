#include "agentflow/agent_worker_model_host.hpp"
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
std::string response_bytes(const ModelCompletion& response){
    Json calls=Json::array();for(const auto& call:response.tool_calls)calls.push_back({{"id",call.id},{"name",call.name},{"arguments_json",call.arguments_json}});
    return Json{{"content",response.content},{"refusal",response.refusal},{"finish_reason",response.finish_reason},{"usage_json",response.usage_json},{"provider_items_json",response.provider_items_json},{"tool_calls",std::move(calls)}}.dump();
}
}
AgentWorkerModelHost::AgentWorkerModelHost(PersistenceService& store,AgentWorkerLeaseSpec lease,ChatProviderConfig provider,
    std::vector<ModelToolDefinition> tools,AgentWorkerModelPolicy policy,std::shared_ptr<AgentModelGateway> gateway,std::chrono::steady_clock::time_point deadline,std::stop_token owner_cancel)
    :store_(store),lease_(std::move(lease)),provider_(std::move(provider)),tools_(std::move(tools)),policy_(std::move(policy)),gateway_(std::move(gateway)),deadline_(deadline),owner_cancel_(owner_cancel){
    if(!gateway_)throw std::invalid_argument("Worker model host requires its captured backend gateway");const auto identity=Json::parse(lease_.provider_identity_json);
    if(identity.at("model_id")!=provider_.model||identity.at("wire")!=wire_name(provider_.wire))throw Conflict("Worker provider differs from captured admission");
    const auto saved=store_.agent_worker_lease(lease_.generation).get();if(saved.state!="active"||saved.spec!=lease_)throw Conflict("Worker model host has no exact captured lease");
}
ModelCompletion AgentWorkerModelHost::complete(const std::string& attempt,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel){
    std::lock_guard lock(invocation_);std::stop_source linked;std::stop_callback owner(owner_cancel_,[&]{linked.request_stop();});std::stop_callback caller(cancel,[&]{linked.request_stop();});
    auto check=[&]{if(linked.stop_requested())throw TransportCancelled("Worker model owner cancelled");if(std::chrono::steady_clock::now()>=deadline_)throw TransportTimeout("Worker model root deadline elapsed");};check();if(!sink)throw std::invalid_argument("Worker model event sink is required");
    if(request.include_usage!=policy_.include_usage||request.max_output_tokens!=policy_.max_output_tokens)throw Conflict("Worker changed its admitted model controls");
    if(request.tools.size()!=tools_.size())throw Conflict("Worker changed its admitted model tool catalogue");for(std::size_t i=0;i<tools_.size();++i)if(request.tools[i].name!=tools_[i].name||request.tools[i].description!=tools_[i].description||request.tools[i].input_schema_json!=tools_[i].input_schema_json)throw Conflict("Worker changed its admitted model tool catalogue");
    const auto bytes=request_bytes(provider_,request);AgentWorkerModelClaimSpec claim{lease_.generation,lease_.root_run_id,lease_.owner_run_id,attempt,context_digest(bytes)};check();store_.claim_agent_worker_model(claim).get();
    ModelCompletion response;
    try{check();auto selected=provider_;const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline_-std::chrono::steady_clock::now());if(remaining.count()<1)throw TransportTimeout("Worker model root deadline elapsed");selected.deadline=std::min(selected.deadline,remaining);selected.idle_timeout=std::min(selected.idle_timeout,selected.deadline);
        response=gateway_->complete(selected,request,[&](const ModelEvent& event){check();sink(event);},linked.get_token());
    }catch(...){const auto failure=std::current_exception();try{store_.interrupt_agent_worker_model(claim).get();}catch(...){throw AgentWorkerModelOutcomeUnrecorded("Worker model dispatch outcome could not be recorded");}std::rethrow_exception(failure);}
    try{store_.record_agent_worker_model_response(claim,response_bytes(response)).get();}catch(...){throw AgentWorkerModelOutcomeUnrecorded("Actual worker model response could not be recorded");}return response;
}
}
