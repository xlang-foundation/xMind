#include "agentflow/agent_worker_coding_owner.hpp"
#include "agentflow/agent_worker_coding_context.hpp"
#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/agent_worker_read_rpc.hpp"

namespace agentflow {
struct AgentWorkerCodingOwner::Impl {
    AgentWorkerCodingContext context;
    std::unique_ptr<AgentWorkerModelHost> model;
    AgentWorkerRpcHandler model_rpc,read_rpc,file_rpc,process_rpc,journal_rpc;
    Impl(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,
        AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,ChatProviderConfig provider,
        std::string identity,std::shared_ptr<AgentModelGateway> gateway,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::vector<ProcessProfile> profiles)
        :context(store,lease,std::move(owner),policy,deadline,cancel,std::move(profiles)){
        AgentWorkerModelPolicy model_policy{policy.include_usage,policy.max_output_tokens,std::move(identity)};
        model_policy.request_context=[this](std::stop_token stop){return context.prepare(stop);};
        model_policy.admit_context=[this](const std::string& attempt,const ModelRequest& request,std::stop_token stop){context.admit(attempt,request,stop);};
        model=std::make_unique<AgentWorkerModelHost>(store,lease,std::move(provider),std::move(policy.tools),std::move(model_policy),std::move(gateway),deadline,cancel);
        model_rpc=bind_agent_worker_model_rpc(*model,scope);read_rpc=bind_agent_worker_read_rpc(context,lease,scope);
        file_rpc=context.bind_files(scope);process_rpc=context.bind_processes(scope);journal_rpc=bind_agent_worker_workspace_journal_rpc(store,context.files(),scope,deadline,cancel);
    }
};
AgentWorkerCodingOwner::AgentWorkerCodingOwner(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,
    AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,ChatProviderConfig provider,std::string identity,
    std::shared_ptr<AgentModelGateway> gateway,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::vector<ProcessProfile> profiles)
    :impl_(std::make_unique<Impl>(store,std::move(lease),scope,std::move(owner),std::move(policy),std::move(provider),std::move(identity),std::move(gateway),deadline,cancel,std::move(profiles))){}
AgentWorkerCodingOwner::~AgentWorkerCodingOwner()=default;
std::string AgentWorkerCodingOwner::dispatch(const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel){
    if(method.starts_with("model."))return impl_->model_rpc(method,payload,std::move(sink),cancel);
    if(method.starts_with("journal."))return impl_->journal_rpc(method,payload,std::move(sink),cancel);
    if(method.starts_with("process."))return impl_->process_rpc(method,payload,std::move(sink),cancel);
    if(method.starts_with("file."))return impl_->file_rpc(method,payload,std::move(sink),cancel);
    return impl_->read_rpc(method,payload,std::move(sink),cancel);
}
}
