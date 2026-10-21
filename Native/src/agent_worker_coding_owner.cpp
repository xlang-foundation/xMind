#include "agentflow/agent_worker_coding_owner.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/agent_worker_coding_context.hpp"
#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/agent_worker_read_rpc.hpp"
#include "agentflow/agent_worker_context_window.hpp"

namespace agentflow {
struct AgentWorkerCodingOwner::Impl {
    AgentWorkerCodingContext context;
    std::unique_ptr<AgentWorkerContextWindow> context_window;
    std::unique_ptr<AgentWorkerModelHost> model;
    std::unique_ptr<AgentWorkerJournalHost> native_journal;
    std::unique_ptr<NativeWorkerModelBinding> native_model;
    bool native_model_required=false;
    AgentWorkerRpcHandler model_rpc,read_rpc,file_rpc,process_rpc,remote_rpc,callable_rpc,journal_rpc;
    Impl(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,
        AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,ChatProviderConfig provider,
        std::string identity,std::shared_ptr<AgentModelGateway> gateway,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::shared_ptr<AgentProcessToolHost> process_owner,std::optional<AgentRemoteToolOwnerContext> remote_owner,std::optional<AgentCallableToolOwnerContext> callable_owner)
        :context(store,lease,std::move(owner),policy,deadline,cancel,std::move(process_owner),std::move(remote_owner),std::move(callable_owner)){
        AgentWorkerModelPolicy model_policy{policy.include_usage,policy.max_output_tokens,std::move(identity)};
        model_policy.request_context=[this](std::stop_token stop){return context.prepare(stop);};
        model_policy.admit_context=[this](const std::string& attempt,const ModelRequest& request,std::stop_token stop){context.admit(attempt,request,stop);};
        AgentWorkerJournalModelHooks hooks;
        if(policy.context){
            context_window=std::make_unique<AgentWorkerContextWindow>(store,lease,provider,*policy.context,gateway,
                [this](std::stop_token stop){return context.prepare(stop);},
                [this](const std::string& attempt,const ModelRequest& raw,std::stop_token stop){context.admit(attempt,raw,stop);},deadline,cancel);
            model_policy.request_context=[this](std::stop_token stop){return context_window->prepare(stop);};
            model_policy.admit_context=[this](const std::string& attempt,const ModelRequest& request,std::stop_token stop){context_window->admit(attempt,request,stop);};
            model_policy.reject_context=[this](const std::string& attempt,const ModelRequest& request,std::int64_t elapsed,std::stop_token stop){return context_window->reject(attempt,request,elapsed,stop);};
            model_policy.rebuild_context=[this](const std::string& attempt,const ModelRequest& request,std::stop_token stop){return context_window->rebuild(attempt,request,stop);};
            hooks.reserve=[this](const std::string& attempt,std::stop_token stop){return context_window->reserve(attempt,stop);};
            hooks.start=[this](const std::string& attempt,std::stop_token stop){return context_window->start(attempt,stop);};
            hooks.finish=[this](const std::string& attempt,std::stop_token stop){return context_window->finish(attempt,stop);};
        }
        model=std::make_unique<AgentWorkerModelHost>(store,lease,std::move(provider),std::move(policy.tools),std::move(model_policy),std::move(gateway),deadline,cancel);
        native_journal=std::make_unique<AgentWorkerJournalHost>(store,context.files(),deadline,cancel,hooks);
        native_model=std::make_unique<NativeWorkerModelBinding>(*model,native_journal.get(),&context,&context,&context);
        model_rpc=bind_agent_worker_model_rpc(*model,scope);read_rpc=bind_agent_worker_read_rpc(context,lease,scope);
        file_rpc=context.bind_files(scope);process_rpc=context.bind_processes(scope);remote_rpc=context.bind_remotes(scope);callable_rpc=context.bind_callables(scope);journal_rpc=bind_agent_worker_workspace_journal_rpc(store,context.files(),scope,deadline,cancel,std::move(hooks));
    }
};
AgentWorkerCodingOwner::AgentWorkerCodingOwner(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,
    AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,ChatProviderConfig provider,std::string identity,
    std::shared_ptr<AgentModelGateway> gateway,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::shared_ptr<AgentProcessToolHost> process_owner,std::optional<AgentRemoteToolOwnerContext> remote_owner,std::optional<AgentCallableToolOwnerContext> callable_owner)
    :impl_(std::make_unique<Impl>(store,std::move(lease),scope,std::move(owner),std::move(policy),std::move(provider),std::move(identity),std::move(gateway),deadline,cancel,std::move(process_owner),std::move(remote_owner),std::move(callable_owner))){}
AgentWorkerCodingOwner::~AgentWorkerCodingOwner()=default;
bool AgentWorkerCodingOwner::outcome_unrecorded()const noexcept{return impl_->native_model->outcome_unrecorded();}
NativeWorkerModelAccess AgentWorkerCodingOwner::native_model_access()const{impl_->native_model_required=true;impl_->model_rpc={};impl_->journal_rpc={};impl_->read_rpc={};impl_->file_rpc={};impl_->process_rpc={};return impl_->native_model->access();}
std::string AgentWorkerCodingOwner::dispatch(const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel){
    if(method.starts_with("model.")){if(impl_->native_model_required)throw AgentWorkerRpcRemoteError("unsupported");return impl_->model_rpc(method,payload,std::move(sink),cancel);}
    if(method.starts_with("journal.")){if(impl_->native_model_required)throw AgentWorkerRpcRemoteError("unsupported");return impl_->journal_rpc(method,payload,std::move(sink),cancel);}
    if(method.starts_with("callable."))return impl_->callable_rpc(method,payload,std::move(sink),cancel);
    if(method.starts_with("remote."))return impl_->remote_rpc(method,payload,std::move(sink),cancel);
    if(method.starts_with("process.")){if(impl_->native_model_required)throw AgentWorkerRpcRemoteError("unsupported");return impl_->process_rpc(method,payload,std::move(sink),cancel);}
    if(method.starts_with("file.")){if(impl_->native_model_required)throw AgentWorkerRpcRemoteError("unsupported");return impl_->file_rpc(method,payload,std::move(sink),cancel);}
    if(impl_->native_model_required)throw AgentWorkerRpcRemoteError("unsupported");
    return impl_->read_rpc(method,payload,std::move(sink),cancel);
}
}
