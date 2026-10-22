#include "agentflow/agent_worker_coding_context.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agentflow/persistence_service.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>

namespace agentflow {
namespace {
AgentWorkerReadRequestPolicy read_policy(AgentWorkerReadRequestPolicy policy,const std::optional<AgentCallableToolOwnerContext>& callables){
    std::set<std::string> names,callable_names;if(callables)for(const auto& binding:callables->bindings)if(!callable_names.insert(binding.definition.name).second)throw std::invalid_argument("Duplicate worker callable binding");
    for(const auto& tool:policy.tools)if((!is_agent_read_tool(tool.name)&&!is_agent_file_tool(tool.name)&&!is_agent_process_tool(tool.name)&&!is_agent_remote_tool(tool.name)&&!is_agent_guidance_tool(tool.name)&&!(policy.planning&&(tool.name=="plan_tasks"||tool.name=="revise_plan"||tool.name=="inspect_plan"))&&!(tool.name=="delegate_tasks"&&policy.delegation)&&!callable_names.contains(tool.name))||!names.insert(tool.name).second)throw std::invalid_argument("Coding workspace contains an unsupported capability");
    std::erase_if(policy.tools,[&](const auto& tool){return callable_names.contains(tool.name)||(!is_agent_read_tool(tool.name)&&!is_agent_guidance_tool(tool.name)&&!(policy.planning&&(tool.name=="plan_tasks"||tool.name=="revise_plan"||tool.name=="inspect_plan"))&&!(tool.name=="delegate_tasks"&&policy.delegation));});return policy;
}
}
AgentWorkerCodingContext::AgentWorkerCodingContext(PersistenceService& store,AgentWorkerLeaseSpec lease,
    AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::shared_ptr<AgentProcessToolHost> process_owner,std::optional<AgentRemoteToolOwnerContext> remote_owner,std::optional<AgentCallableToolOwnerContext> callable_owner)
    :reads_(store,store,lease,owner,read_policy(policy,callable_owner),cancel),tools_(std::move(policy.tools)){
    if(owner.deadline!=deadline)throw std::invalid_argument("Coding model and tools require the same captured deadline");
    if(callable_owner)for(const auto& binding:callable_owner->bindings)callable_names_.insert(binding.definition.name);
    AgentFileToolOwnerContext files;files.run_id=owner.run_id;files.primary_workspace=owner.primary_workspace;
    files.workspace_members=owner.workspace_members;files.approval_timeout=owner.approval_timeout;
    files.permission_layers=owner.inherited_permission_layers;if(!owner.permission_rules.empty())files.permission_layers.push_back(owner.permission_rules);
    files.repository_context=[this](std::size_t index){return index<reads_.contexts_.size()?reads_.contexts_[index].get():nullptr;};
    for(const auto& tool:tools_)if(is_agent_file_tool(tool.name)&&!callable_names_.contains(tool.name)){if(!owner.offered_tool(tool.name))throw Conflict("Coding file catalogue differs from its captured owner");files.allowed_tools.push_back(tool.name);}
    files_=std::make_unique<AgentWorkerFileHost>(store,std::move(lease),std::move(files),deadline,cancel);
    if(std::any_of(tools_.begin(),tools_.end(),[this](const auto& tool){return !callable_names_.contains(tool.name)&&is_agent_process_tool(tool.name);})){
        if(!process_owner)throw Conflict("Coding command catalogue lacks its native profile owner");
        process_=std::move(process_owner);
        AgentProcessToolOwnerContext context;context.run_id=owner.run_id;context.process=process_.get();context.primary_workspace=owner.primary_workspace;context.workspace_members=owner.workspace_members;context.repository_context=[this](std::size_t i){return i<reads_.contexts_.size()?reads_.contexts_[i].get():nullptr;};context.offered_tool=owner.offered_tool;context.permission_layers=owner.inherited_permission_layers;if(!owner.permission_rules.empty())context.permission_layers.push_back(owner.permission_rules);context.deadline=deadline;context.approval_timeout=owner.approval_timeout;context.allow_background=true;
        processes_=std::make_unique<AgentWorkerProcessHost>(store,files_->lease(),std::move(context),deadline,cancel);
    }
    if(std::any_of(tools_.begin(),tools_.end(),[this](const auto& tool){return !callable_names_.contains(tool.name)&&is_agent_remote_tool(tool.name);})){
        if(!remote_owner||remote_owner->run_id!=owner.run_id||remote_owner->primary_workspace!=owner.primary_workspace||remote_owner->workspace_members!=owner.workspace_members||remote_owner->deadline!=deadline)throw Conflict("Coding remote catalogue lacks its captured native registry owner");
        auto context=std::move(*remote_owner);refresh_catalogue_=std::move(context.refresh_catalogue);context.repository_context=nullptr;context.repository_context_for_call=[this]{return reads_.contexts_.front().get();};context.offered_tool=[this](const std::string& name){return std::any_of(tools_.begin(),tools_.end(),[&](const auto& tool){return tool.name==name;});};context.permission_layers=owner.inherited_permission_layers;if(!owner.permission_rules.empty())context.permission_layers.push_back(owner.permission_rules);
        remotes_=std::make_unique<AgentWorkerRemoteHost>(store,files_->lease(),std::move(context),deadline,cancel);
    }
    if(callable_owner&&!callable_owner->bindings.empty()){
        if(callable_owner->run_id!=owner.run_id||callable_owner->workspace_id!=files_->lease().workspace_id||callable_owner->workspace_revision!=files_->lease().workspace_revision||callable_owner->deadline!=deadline)throw Conflict("Callable owner differs from captured coding workspace");
        auto context=std::move(*callable_owner);context.repository_context=nullptr;context.repository_context_for_call=[this]{return reads_.contexts_.front().get();};context.offered_tool=[this](const std::string& name){return callable_names_.contains(name)&&std::any_of(tools_.begin(),tools_.end(),[&](const auto& tool){return tool.name==name;});};context.permission_layers=owner.inherited_permission_layers;if(!owner.permission_rules.empty())context.permission_layers.push_back(owner.permission_rules);callables_=std::make_unique<AgentWorkerCallableHost>(store,files_->lease(),std::move(context),deadline,cancel);
    }
}
ModelRequest AgentWorkerCodingContext::prepare(std::stop_token cancel){
    {std::unique_lock lock(reads_.mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Coding workspace is busy");reads_.check(cancel);
    if(remote_catalogue_invalidated_){
        if(!refresh_catalogue_)throw Conflict("Invalidated worker remote catalogue has no native discovery owner");
        auto definitions=refresh_catalogue_(cancel);auto next=tools_;std::erase_if(next,[this](const auto& tool){return !callable_names_.contains(tool.name)&&is_agent_remote_tool(tool.name);});
        for(auto& tool:definitions){if(!is_agent_remote_tool(tool.name)||next.size()>=64||std::any_of(next.begin(),next.end(),[&](const auto& existing){return existing.name==tool.name;}))throw Conflict("Invalid refreshed worker remote catalogue");next.push_back(std::move(tool));}
        tools_=std::move(next);remote_catalogue_invalidated_=false;
    }}
    auto request=reads_.prepare(cancel);request.tools=tools_;for(const auto& note:joined_process_notes_)request.messages.push_back({MessageRole::system,note});return request;
}
void AgentWorkerCodingContext::admit(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){
    if(encode_agent_model_request(prepare(cancel))!=encode_agent_model_request(request))throw Conflict("Coding request changed backend history, guidance or catalogue");
    auto read_request=request;read_request.messages.resize(read_request.messages.size()-joined_process_notes_.size());read_request.tools=reads_.policy_.tools;reads_.admit(attempt,read_request,cancel,&request);
}
AgentReadToolResult AgentWorkerCodingContext::invoke(const AgentWorkspaceInvocation& call,std::stop_token cancel){return reads_.invoke(call,cancel);}
AgentFileToolResult AgentWorkerCodingContext::invoke(const AgentFileToolInvocation& call,std::stop_token cancel){
    std::unique_lock lock(reads_.mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Coding workspace is busy");
    reads_.check(cancel);if(call.attempt_id!=reads_.admitted_attempt_)throw Conflict("File call lacks its actually admitted guidance window");
    return files_->invoke(call,cancel);
}
AgentProcessToolResult AgentWorkerCodingContext::invoke(const AgentProcessToolInvocation& call,std::stop_token cancel){
    std::unique_lock lock(reads_.mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Coding workspace is busy");
    reads_.check(cancel);if(call.attempt_id!=reads_.admitted_attempt_)throw Conflict("Process call lacks its actually admitted guidance window");
    if(!processes_)throw Conflict("Coding process capability unavailable");return processes_->invoke(call,cancel);
}
bool AgentWorkerCodingContext::join_processes(const std::string& attempt,std::stop_token cancel){
    std::unique_lock lock(reads_.mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Coding workspace is busy");
    if(join_outcome_unrecorded_)throw ProcessOutcomeUnrecorded("Background join observation was not recorded; no retry is authorized");
    reads_.check(cancel);if(attempt!=reads_.admitted_attempt_)throw Conflict("Process join lacks its current admitted model attempt");
    const auto claim=reads_.store_.agent_worker_model_claim(reads_.lease_.root_run_id,attempt).get();
    if(claim.spec.generation!=reads_.lease_.generation||claim.spec.owner_run_id!=reads_.lease_.owner_run_id||claim.state!="responded"||decode_agent_model_completion(claim.actual_response_json).finish_reason!="stop")throw Conflict("Process join requires its actual final model response");
    if(const auto prior=joined_attempts_.find(attempt);prior!=joined_attempts_.end())return prior->second;
    std::stop_source stop;std::stop_callback owner_stop(reads_.owner_cancel_,[&]{stop.request_stop();});std::stop_callback caller_stop(cancel,[&]{stop.request_stop();});
    const auto joined=process_?process_->join_run(reads_.lease_.owner_run_id,stop.get_token()):std::vector<std::string>{};reads_.check(cancel);
    if(!joined.empty()){
        join_outcome_unrecorded_=true;
        try{const auto note=format_joined_process_observation(joined);reads_.store_.append_event(reads_.lease_.owner_run_id,"agent.background_processes_joined",nlohmann::json{{"generation",reads_.lease_.generation},{"attempt_id",attempt},{"results",joined},{"observation",note}}.dump()).get();joined_process_notes_.push_back(note);}
        catch(...){throw ProcessOutcomeUnrecorded("Background join observation could not be recorded; no retry is authorized");}
        join_outcome_unrecorded_=false;
    }
    joined_attempts_.emplace(attempt,!joined.empty());return !joined.empty();
}
AgentRemoteToolResult AgentWorkerCodingContext::invoke(const AgentRemoteToolInvocation& call,std::stop_token cancel){
    std::unique_lock lock(reads_.mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Coding workspace is busy");reads_.check(cancel);
    if(call.attempt_id!=reads_.admitted_attempt_)throw Conflict("Remote call lacks its actually admitted guidance window");if(!remotes_)throw Conflict("Coding remote capability unavailable");
    const auto result=remotes_->invoke(call,cancel);if(!result.success){const auto output=nlohmann::json::parse(result.output_json);if(output.contains("error")&&output.at("error").value("code",std::string{})=="remote_catalogue_changed")remote_catalogue_invalidated_=true;}return result;
}
AgentCallableToolResult AgentWorkerCodingContext::invoke(const AgentCallableInvocation& call,std::stop_token cancel){
    std::unique_lock lock(reads_.mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Coding workspace is busy");reads_.check(cancel);
    if(call.attempt_id!=reads_.admitted_attempt_)throw Conflict("Callable lacks its actually admitted guidance window");if(!callables_)throw Conflict("Coding callable capability unavailable");return callables_->invoke(call,cancel);
}
std::vector<std::string> AgentWorkerCodingContext::tool_names(std::stop_token cancel){
    std::unique_lock lock(reads_.mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Coding workspace is busy");reads_.check(cancel);return callables_?callables_->tool_names(cancel):std::vector<std::string>{};
}

}
