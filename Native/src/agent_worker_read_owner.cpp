#include "agentflow/agent_worker_read_owner.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/agent_worker_read_rpc.hpp"
#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/agent_worker_context_window.hpp"

namespace agentflow {
namespace {
AgentReadToolOwnerContext captured_owner(AgentReadToolOwnerContext owner,AgentWorkerChannel::Deadline deadline){
    if(owner.deadline!=deadline)throw std::invalid_argument("Worker read and model owners require the same captured deadline");
    return owner;
}
}
struct AgentWorkerReadOwner::Impl {
    AgentWorkerReadContext context;
    std::unique_ptr<AgentWorkerContextWindow> context_window;
    std::unique_ptr<AgentWorkerModelHost> model;
    std::unique_ptr<NativeWorkerModelBinding> native_model;
    bool native_model_required=false;
    AgentWorkerRpcHandler model_rpc,read_rpc,journal_rpc;
    Impl(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,
        AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,ChatProviderConfig provider,
        std::string identity,std::shared_ptr<AgentModelGateway> gateway,
        AgentWorkerChannel::Deadline deadline,std::stop_token cancel)
        :context(store,store,lease,captured_owner(std::move(owner),deadline),policy,cancel){
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
        model=std::make_unique<AgentWorkerModelHost>(store,lease,std::move(provider),std::move(policy.tools),
            std::move(model_policy),std::move(gateway),deadline,cancel);
        native_model=std::make_unique<NativeWorkerModelBinding>(*model);
        model_rpc=bind_agent_worker_model_rpc(*model,scope);
        read_rpc=bind_agent_worker_read_rpc(context,lease,scope);
        journal_rpc=bind_agent_worker_journal_rpc(store,std::move(lease),scope,deadline,cancel,std::move(hooks));
    }
};
AgentWorkerReadOwner::AgentWorkerReadOwner(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,
    AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,ChatProviderConfig provider,
    std::string identity,std::shared_ptr<AgentModelGateway> gateway,AgentWorkerChannel::Deadline deadline,std::stop_token cancel)
    :impl_(std::make_unique<Impl>(store,std::move(lease),scope,std::move(owner),std::move(policy),std::move(provider),std::move(identity),std::move(gateway),deadline,cancel)){}
AgentWorkerReadOwner::~AgentWorkerReadOwner()=default;
NativeWorkerModelAccess AgentWorkerReadOwner::native_model_access()const{impl_->native_model_required=true;return impl_->native_model->access();}
std::string AgentWorkerReadOwner::dispatch(const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel){
    if(method.starts_with("model.")){if(impl_->native_model_required)throw AgentWorkerRpcRemoteError("unsupported");return impl_->model_rpc(method,payload,std::move(sink),cancel);}
    if(method.starts_with("journal."))return impl_->journal_rpc(method,payload,std::move(sink),cancel);
    return impl_->read_rpc(method,payload,std::move(sink),cancel);
}
}
