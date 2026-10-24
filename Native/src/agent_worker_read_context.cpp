#include "agentflow/agent_worker_read_context.hpp"
#include "agentflow/agent_worker_planning_owner.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "agentflow/delegation_executor.hpp"
#include "agentflow/agent_permissions.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>

namespace agentflow {
using Json=nlohmann::json;
struct AgentWorkerReadContext::Preview {ModelRequest request;std::vector<std::unique_ptr<RepositoryInstructionContext>> contexts;std::optional<ProviderHistoryReplaySpec> replay;};
AgentWorkerReadContext::AgentWorkerReadContext(AgentRunJournal& journal,PersistenceService& store,AgentWorkerLeaseSpec lease,AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,std::stop_token cancel)
    :store_(store),lease_(std::move(lease)),owner_(std::move(owner)),policy_(std::move(policy)),owner_cancel_(cancel){
    const bool model_only=lease_.workspace_id=="workspace:none";
    if((model_only?(owner_.primary_workspace||owner_.workspace_members||!policy_.tools.empty()||bool(policy_.delegation)):(!owner_.primary_workspace||!owner_.repository_context||!owner_.offered_tool))||policy_.instructions.size()>65536||policy_.instructions.find('\0')!=std::string::npos||policy_.instruction_suffix.size()>65536||policy_.instruction_suffix.find('\0')!=std::string::npos||policy_.tools.size()>64)throw std::invalid_argument("Invalid backend worker read context");
    if(bool(policy_.history_owner)!=bool(policy_.history_provider))throw std::invalid_argument("Worker session history requires its captured provider route");
    if(policy_.history_owner&&(policy_.history_owner->root_run_id!=lease_.root_run_id||policy_.history_owner->owner_run_id!=lease_.owner_run_id||policy_.history_owner->role!=(lease_.root_run_id==lease_.owner_run_id?ModelCallRole::parent:ModelCallRole::leaf)||policy_.history_owner->scope.kind!=(lease_.root_run_id==lease_.owner_run_id?ContextScopeKind::session:ContextScopeKind::execution)||policy_.history_owner->provider_identity_json!=lease_.provider_identity_json))throw Conflict("Worker history differs from captured model ownership");
    if(policy_.context_budget&&(!policy_.context||lease_.root_run_id==lease_.owner_run_id||(policy_.planning&&policy_.context_budget!=policy_.planning->budget)))throw Conflict("Shared child context budget requires the exact counted planning owner");
    if(policy_.planning){const AgentWorkerScope scope{lease_.root_run_id,lease_.owner_run_id,lease_.workspace_id,lease_.generation,lease_.workspace_revision};const auto& budget=policy_.planning->budget;if(policy_.planning->callbacks.scope!=scope||!budget||budget->root_id()!=lease_.root_run_id||budget->deadline()!=owner_.deadline||!budget->segment_open()||budget->snapshot().spec.policy_id!="native.dynamic-plan"||store_.dynamic_budget_segment(lease_.root_run_id).get().id!=budget->segment_id())throw Conflict("Planning workspace differs from its captured root budget");if(lease_.owner_run_id!=lease_.root_run_id){const auto child=std::get<ChildAdmissionRecord>(store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::child_admission,lease_.owner_run_id}).get());if(child.kind!=ChildAdmissionKind::dynamic_agent||child.root_run_id!=lease_.root_run_id)throw Conflict("Planning child context requires its exact dynamic admission");}}
    std::set<std::string> names;for(const auto& tool:policy_.tools)if((!is_agent_read_tool(tool.name)&&!is_agent_guidance_tool(tool.name)&&!(policy_.planning&&(tool.name=="plan_tasks"||tool.name=="revise_plan"||tool.name=="inspect_plan"))&&!(tool.name=="delegate_tasks"&&policy_.delegation&&lease_.root_run_id==lease_.owner_run_id))||!owner_.offered_tool(tool.name)||!names.insert(tool.name).second)throw std::invalid_argument("Read context catalogue differs from its captured owner");
    const auto count=model_only?0:owner_.workspace_members?owner_.workspace_members->folders().size():1;
    for(std::size_t index=0;index<count;++index){auto* context=owner_.repository_context(index);if(!context)throw std::invalid_argument("Worker read context requires actual native guidance");contexts_.push_back(std::make_unique<RepositoryInstructionContext>(*context));}
    auto read_owner=owner_;read_owner.guidance_instruction_bytes=policy_.instructions.size()+policy_.instruction_suffix.size();read_owner.repository_context=[this](std::size_t index){return contexts_.at(index).get();};read_owner.offered_tool=[this](const std::string& name){return std::any_of(policy_.tools.begin(),policy_.tools.end(),[&](const auto& tool){return tool.name==name;});};
    if(!model_only)native_=std::make_unique<AgentWorkerReadHost>(journal,store_,lease_,std::move(read_owner),owner_cancel_);
}
AgentWorkerReadContext::~AgentWorkerReadContext()=default;
InstructionPrecondition AgentWorkerReadContext::planning_guidance(const std::string& directory,std::stop_token stop){
    std::lock_guard lock(mutex_);check(stop);if(contexts_.empty()||!contexts_.front()->ready(directory,stop))return {};return contexts_.front()->precondition(directory);
}
void AgentWorkerReadContext::check(std::stop_token cancel){
    if(cancel.stop_requested()||owner_cancel_.stop_requested())throw AgentWorkerAuthorityCancelled("Worker read context cancelled");if(std::chrono::steady_clock::now()>=owner_.deadline)throw AgentWorkerAuthorityExpired("Worker read context deadline elapsed");
    const auto run=std::get<Run>(store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get());if(run.state!=RunState::running)throw Conflict("Worker read context requires its running owner");
    if(lease_.root_run_id!=lease_.owner_run_id)(void)store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::child_admission,lease_.owner_run_id}).get();
}
AgentWorkerReadContext::Preview AgentWorkerReadContext::preview(std::stop_token cancel){
    check(cancel);Preview result;auto instructions=policy_.instructions;
    const auto selected=contexts_.empty()?SkillSelections{}:store_.initialize_run_skills(lease_.owner_run_id,lease_.workspace_id).get();
    for(std::size_t index=0;index<contexts_.size();++index){auto copy=std::make_unique<RepositoryInstructionContext>(*contexts_[index]);if(!index)copy->skills().restore(selected,cancel);if(index)instructions+="\n\nRepository guidance for registered workspace folder "+owner_.workspace_members->folders().at(index).folder_id+" (paths relative to this folder):\n";instructions+=copy->prepare(cancel);result.contexts.push_back(std::move(copy));}
    instructions+=policy_.instruction_suffix;
    if(instructions.size()>65536)throw ToolFileError("Combined backend worker instructions exceed limits");if(!instructions.empty())result.request.messages.push_back({MessageRole::system,std::move(instructions)});
    result.request.tools=policy_.tools;result.request.include_usage=policy_.include_usage;result.request.max_output_tokens=policy_.max_output_tokens;
    if(policy_.history_owner){
        const auto history=store_.provider_history(*policy_.history_owner).get();std::vector<std::size_t> projected;
        for(const auto& payload:history.groups)for(const auto& original:payload.source.originals){auto decoded=model_message_from_history(original);if(payload.reproject_provider_items&&decoded.role==MessageRole::assistant&&decoded.provider_items_json!="[]")projected.push_back(result.request.messages.size());result.request.messages.push_back(std::move(decoded));}
        if(!projected.empty()){result.request=reproject_checked_provider_history(*policy_.history_provider,result.request,projected);result.replay=ProviderHistoryReplaySpec{*policy_.history_owner,{},history.source_binding,context_digest(serialize_provider_history_projection(*policy_.history_provider,result.request))};}
    }else{const auto history=std::get<std::vector<Message>>(store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::history,lease_.owner_run_id}).get());for(const auto& message:history)result.request.messages.push_back(model_message_from_history(message));}
    check(cancel);return result;
}
ModelRequest AgentWorkerReadContext::prepare(std::stop_token cancel){std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker read context is busy");return preview(cancel).request;}
void AgentWorkerReadContext::admit(const std::string& attempt,const ModelRequest& request,std::stop_token cancel,const ModelRequest* full_window){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker read context is busy");store_.validate_agent_worker_model_window(lease_,owner_.deadline,owner_cancel_,cancel).get();auto candidate=preview(cancel);
    if(encode_agent_model_request(candidate.request)!=encode_agent_model_request(request))throw Conflict("Worker model request changed backend instructions or history");
    const auto claim=store_.agent_worker_model_claim(lease_.root_run_id,attempt).get();if(claim.spec.generation!=lease_.generation||claim.spec.owner_run_id!=lease_.owner_run_id||claim.state!="dispatching")throw Conflict("Worker guidance has no captured live model dispatch");
    if(candidate.replay){if(full_window)candidate.replay->projection_binding=context_digest(serialize_provider_history_projection(*policy_.history_provider,*full_window));store_.record_provider_history_replay(*candidate.replay).get();}
    Json metadata=Json::array();for(std::size_t index=0;index<candidate.contexts.size();++index)metadata.push_back({{"folder_id",owner_.workspace_members?owner_.workspace_members->folders().at(index).folder_id:lease_.workspace_id},{"guidance",Json::parse(candidate.contexts[index]->metadata())}});
    store_.append_event(lease_.owner_run_id,"agent.repository_scope",Json{{"snapshot","worker_before_model_dispatch"},{"attempt_id",attempt},{"contexts",std::move(metadata)}}.dump()).get();check(cancel);contexts_=std::move(candidate.contexts);admitted_attempt_=attempt;
}
AgentReadToolResult AgentWorkerReadContext::invoke(const AgentWorkspaceInvocation& call,std::stop_token cancel){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker read context is busy");check(cancel);if(!native_)throw Conflict("Model-only worker has no workspace tool authority");if(call.attempt_id!=admitted_attempt_)throw Conflict("Worker read has no actually admitted guidance window");
    if(call.tool_name!="delegate_tasks")return native_->invoke(call,cancel);
    if(!policy_.delegation||lease_.root_run_id!=lease_.owner_run_id||!owner_.offered_tool(call.tool_name)||std::none_of(policy_.tools.begin(),policy_.tools.end(),[&](const auto& tool){return tool.name==call.tool_name;}))throw Conflict("Worker has no captured delegation capability");
    const auto claimed=store_.claim_agent_worker_read(lease_,call,owner_.deadline,owner_cancel_,cancel).get();
    AgentReadToolResult result;bool denied=agent_permission_denies(owner_.permission_rules,call.tool_name,"*");for(const auto& layer:owner_.inherited_permission_layers)denied=denied||agent_permission_denies(layer,call.tool_name,"*");
    if(denied)result={false,R"({"error":{"code":"agent_permission_denied","message":"The selected agent policy denied delegation before dispatch"}})"};
    else if(!contexts_.front()->skills().ready(cancel))result={false,R"({"error":{"code":"repository_instructions_required","message":"Updated repository or skill guidance must be delivered before delegation"}})"};
    else{
        store_.start_agent_worker_read(lease_,call,owner_.deadline,owner_cancel_,cancel).get();
        try{const auto actual=store_.agent_worker_model_claim(claimed.model.root_run_id,claimed.model.attempt_id).get();result=policy_.delegation(call,actual.actual_assistant_json,cancel);}
        catch(const RootBudgetExhausted&){result={false,R"({"error":{"code":"delegation_budget_exhausted","message":"No additional children were admitted within the shared root budget"}})"};}
        catch(const DelegationCapacityUnavailable&){result={false,R"({"error":{"code":"delegation_capacity_unavailable","message":"The bounded delegation queue has no available capacity"}})"};}
        catch(const std::invalid_argument&){result={false,R"({"error":{"code":"invalid_delegation_request","message":"The delegation request did not match its registered preset"}})"};}
        catch(...){throw AgentWorkerReadOutcomeUnrecorded("Delegation outcome requires ownership recovery; no replay is authorized");}
    }
    try{store_.record_agent_worker_read_result(lease_,call,result.success,result.output_json).get();}catch(...){throw AgentWorkerReadOutcomeUnrecorded("Delegation result could not be recorded before delivery");}
    check(cancel);return result;
}
}
