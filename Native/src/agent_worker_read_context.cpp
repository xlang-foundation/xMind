#include "agentflow/agent_worker_read_context.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>

namespace agentflow {
using Json=nlohmann::json;
struct AgentWorkerReadContext::Preview {ModelRequest request;std::vector<std::unique_ptr<RepositoryInstructionContext>> contexts;};
AgentWorkerReadContext::AgentWorkerReadContext(AgentRunJournal& journal,PersistenceService& store,AgentWorkerLeaseSpec lease,AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,std::stop_token cancel)
    :store_(store),lease_(std::move(lease)),owner_(std::move(owner)),policy_(std::move(policy)),owner_cancel_(cancel){
    if(!owner_.primary_workspace||!owner_.repository_context||!owner_.offered_tool||policy_.instructions.size()>65536||policy_.instructions.find('\0')!=std::string::npos||policy_.tools.size()>64)throw std::invalid_argument("Invalid backend worker read context");
    std::set<std::string> names;for(const auto& tool:policy_.tools)if(!is_agent_read_tool(tool.name)||!owner_.offered_tool(tool.name)||!names.insert(tool.name).second)throw std::invalid_argument("Read context catalogue differs from its captured owner");
    const auto count=owner_.workspace_members?owner_.workspace_members->folders().size():1;
    for(std::size_t index=0;index<count;++index){auto* context=owner_.repository_context(index);if(!context)throw std::invalid_argument("Worker read context requires actual native guidance");contexts_.push_back(std::make_unique<RepositoryInstructionContext>(*context));}
    auto read_owner=owner_;read_owner.repository_context=[this](std::size_t index){return contexts_.at(index).get();};read_owner.offered_tool=[this](const std::string& name){return std::any_of(policy_.tools.begin(),policy_.tools.end(),[&](const auto& tool){return tool.name==name;});};
    native_=std::make_unique<AgentWorkerReadHost>(journal,store_,lease_,std::move(read_owner),owner_cancel_);
}
AgentWorkerReadContext::~AgentWorkerReadContext()=default;
void AgentWorkerReadContext::check(std::stop_token cancel){
    if(cancel.stop_requested()||owner_cancel_.stop_requested())throw AgentWorkerAuthorityCancelled("Worker read context cancelled");if(std::chrono::steady_clock::now()>=owner_.deadline)throw AgentWorkerAuthorityExpired("Worker read context deadline elapsed");
    const auto run=std::get<Run>(store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get());if(run.state!=RunState::running||lease_.root_run_id!=lease_.owner_run_id)throw Conflict("Worker read context requires its running ordinary root");
}
AgentWorkerReadContext::Preview AgentWorkerReadContext::preview(std::stop_token cancel){
    check(cancel);Preview result;auto instructions=policy_.instructions;
    for(std::size_t index=0;index<contexts_.size();++index){auto copy=std::make_unique<RepositoryInstructionContext>(*contexts_[index]);if(index)instructions+="\n\nRepository guidance for registered workspace folder "+owner_.workspace_members->folders().at(index).folder_id+" (paths relative to this folder):\n";instructions+=copy->prepare(cancel);result.contexts.push_back(std::move(copy));}
    if(instructions.size()>65536)throw ToolFileError("Combined backend worker instructions exceed limits");if(!instructions.empty())result.request.messages.push_back({MessageRole::system,std::move(instructions)});
    const auto history=std::get<std::vector<Message>>(store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::history,lease_.owner_run_id}).get());for(const auto& message:history)result.request.messages.push_back(model_message_from_history(message));
    result.request.tools=policy_.tools;result.request.include_usage=policy_.include_usage;result.request.max_output_tokens=policy_.max_output_tokens;check(cancel);return result;
}
ModelRequest AgentWorkerReadContext::prepare(std::stop_token cancel){std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker read context is busy");return preview(cancel).request;}
void AgentWorkerReadContext::admit(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker read context is busy");auto candidate=preview(cancel);
    if(encode_agent_model_request(candidate.request)!=encode_agent_model_request(request))throw Conflict("Worker model request changed backend instructions or history");
    const auto claim=store_.agent_worker_model_claim(lease_.root_run_id,attempt).get();if(claim.spec.generation!=lease_.generation||claim.spec.owner_run_id!=lease_.owner_run_id||claim.state!="dispatching")throw Conflict("Worker guidance has no captured live model dispatch");
    Json metadata=Json::array();for(std::size_t index=0;index<candidate.contexts.size();++index)metadata.push_back({{"folder_id",owner_.workspace_members?owner_.workspace_members->folders().at(index).folder_id:lease_.workspace_id},{"guidance",Json::parse(candidate.contexts[index]->metadata())}});
    store_.append_event(lease_.owner_run_id,"agent.repository_scope",Json{{"snapshot","worker_before_model_dispatch"},{"attempt_id",attempt},{"contexts",std::move(metadata)}}.dump()).get();check(cancel);contexts_=std::move(candidate.contexts);admitted_attempt_=attempt;
}
AgentReadToolResult AgentWorkerReadContext::invoke(const AgentWorkspaceInvocation& call,std::stop_token cancel){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker read context is busy");check(cancel);if(call.attempt_id!=admitted_attempt_)throw Conflict("Worker read has no actually admitted guidance window");return native_->invoke(call,cancel);
}
}
