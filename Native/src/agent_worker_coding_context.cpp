#include "agentflow/agent_worker_coding_context.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agentflow/persistence_service.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>

namespace agentflow {
namespace {
AgentWorkerReadRequestPolicy read_policy(AgentWorkerReadRequestPolicy policy){
    std::set<std::string> names;
    for(const auto& tool:policy.tools)if((!is_agent_read_tool(tool.name)&&!is_agent_file_tool(tool.name)&&!is_agent_process_tool(tool.name))||!names.insert(tool.name).second)
        throw std::invalid_argument("Coding workspace contains an unsupported capability");
    std::erase_if(policy.tools,[](const auto& tool){return !is_agent_read_tool(tool.name);});return policy;
}
}
AgentWorkerCodingContext::AgentWorkerCodingContext(PersistenceService& store,AgentWorkerLeaseSpec lease,
    AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::shared_ptr<AgentProcessToolHost> process_owner)
    :reads_(store,store,lease,owner,read_policy(policy),cancel),tools_(std::move(policy.tools)){
    if(owner.deadline!=deadline)throw std::invalid_argument("Coding model and tools require the same captured deadline");
    AgentFileToolOwnerContext files;files.run_id=owner.run_id;files.primary_workspace=owner.primary_workspace;
    files.workspace_members=owner.workspace_members;files.approval_timeout=owner.approval_timeout;
    files.permission_layers=owner.inherited_permission_layers;if(!owner.permission_rules.empty())files.permission_layers.push_back(owner.permission_rules);
    files.repository_context=[this](std::size_t index){return index<reads_.contexts_.size()?reads_.contexts_[index].get():nullptr;};
    for(const auto& tool:tools_)if(is_agent_file_tool(tool.name)){if(!owner.offered_tool(tool.name))throw Conflict("Coding file catalogue differs from its captured owner");files.allowed_tools.push_back(tool.name);}
    files_=std::make_unique<AgentWorkerFileHost>(store,std::move(lease),std::move(files),deadline,cancel);
    if(std::any_of(tools_.begin(),tools_.end(),[](const auto& tool){return is_agent_process_tool(tool.name);})){
        if(!process_owner)throw Conflict("Coding command catalogue lacks its native profile owner");
        process_=std::move(process_owner);
        AgentProcessToolOwnerContext context;context.run_id=owner.run_id;context.process=process_.get();context.primary_workspace=owner.primary_workspace;context.workspace_members=owner.workspace_members;context.repository_context=[this](std::size_t i){return i<reads_.contexts_.size()?reads_.contexts_[i].get():nullptr;};context.offered_tool=owner.offered_tool;context.permission_layers=owner.inherited_permission_layers;if(!owner.permission_rules.empty())context.permission_layers.push_back(owner.permission_rules);context.deadline=deadline;context.approval_timeout=owner.approval_timeout;context.allow_background=true;
        processes_=std::make_unique<AgentWorkerProcessHost>(store,files_->lease(),std::move(context),deadline,cancel);
    }
}
ModelRequest AgentWorkerCodingContext::prepare(std::stop_token cancel){auto request=reads_.prepare(cancel);request.tools=tools_;return request;}
void AgentWorkerCodingContext::admit(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){
    if(encode_agent_model_request(prepare(cancel))!=encode_agent_model_request(request))throw Conflict("Coding request changed backend history, guidance or catalogue");
    auto read_request=request;read_request.tools=reads_.policy_.tools;reads_.admit(attempt,read_request,cancel);
}
AgentReadToolResult AgentWorkerCodingContext::invoke(const AgentWorkspaceInvocation& call,std::stop_token cancel){return reads_.invoke(call,cancel);}
AgentWorkerRpcHandler AgentWorkerCodingContext::bind_files(const AgentWorkerScope& scope){
    auto dispatch=bind_agent_worker_file_rpc(*files_,scope);
    return [this,dispatch=std::move(dispatch)](const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel){
        if(method!="file.invoke")return dispatch(method,payload,std::move(sink),cancel);
        try{
            std::unique_lock lock(reads_.mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Coding workspace is busy");
            reads_.check(cancel);const auto call=AgentFileToolInvocation::parse(payload);
            if(call.attempt_id!=reads_.admitted_attempt_)throw Conflict("File call lacks its actually admitted guidance window");
            return dispatch(method,payload,std::move(sink),cancel);
        }catch(const AgentWorkerAuthorityCancelled&){throw AgentWorkerRpcRemoteError("cancelled");}
        catch(const AgentWorkerAuthorityExpired&){throw AgentWorkerRpcRemoteError("timeout");}
        catch(const Conflict&){throw AgentWorkerRpcRemoteError("rejected");}
        catch(const std::invalid_argument&){throw AgentWorkerRpcRemoteError("rejected");}
        catch(const nlohmann::json::exception&){throw AgentWorkerRpcRemoteError("rejected");}
    };
}
AgentWorkerRpcHandler AgentWorkerCodingContext::bind_processes(const AgentWorkerScope& scope){
    if(!processes_)return [](const std::string&,const std::string&,AgentWorkerRpcSink,std::stop_token)->std::string{throw AgentWorkerRpcRemoteError("unsupported");};
    auto dispatch=bind_agent_worker_process_rpc(*processes_,scope);
    return [this,dispatch=std::move(dispatch)](const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel){
        if(method!="process.invoke")return dispatch(method,payload,std::move(sink),cancel);
        try{
            std::unique_lock lock(reads_.mutex_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Coding workspace is busy");
            reads_.check(cancel);const auto call=AgentProcessToolInvocation::parse(payload);
            if(call.attempt_id!=reads_.admitted_attempt_)throw Conflict("Process call lacks its actually admitted guidance window");
            return dispatch(method,payload,std::move(sink),cancel);
        }catch(const AgentWorkerAuthorityCancelled&){throw AgentWorkerRpcRemoteError("cancelled");}
        catch(const AgentWorkerAuthorityExpired&){throw AgentWorkerRpcRemoteError("timeout");}
        catch(const Conflict&){throw AgentWorkerRpcRemoteError("rejected");}
        catch(const std::invalid_argument&){throw AgentWorkerRpcRemoteError("rejected");}
        catch(const nlohmann::json::exception&){throw AgentWorkerRpcRemoteError("rejected");}
    };
}
}
