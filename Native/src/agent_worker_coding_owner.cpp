#include "agentflow/agent_worker_coding_owner.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/agent_worker_coding_context.hpp"
#include "agentflow/agent_worker_context_window.hpp"
#include "agentflow/agent_worker_planning_owner.hpp"

namespace agentflow {
struct AgentWorkerCodingOwner::Impl {
    AgentWorkerCodingContext context;
    std::unique_ptr<AgentWorkerContextWindow> context_window;
    std::unique_ptr<AgentWorkerModelHost> model;
    std::unique_ptr<AgentWorkerJournalHost> native_journal;
    std::unique_ptr<NativePlanningBinding> native_planning;
    std::unique_ptr<NativeWorkerModelBinding> native_model;
    Impl(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,
        AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,ChatProviderConfig provider,
        std::string identity,std::shared_ptr<AgentModelGateway> gateway,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::shared_ptr<AgentProcessToolHost> process_owner,std::optional<AgentRemoteToolOwnerContext> remote_owner,std::optional<AgentCallableToolOwnerContext> callable_owner)
        :context(store,lease,std::move(owner),policy,deadline,cancel,std::move(process_owner),std::move(remote_owner),std::move(callable_owner)){
        if(scope!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Coding worker scope differs from captured lease");
        AgentWorkerModelPolicy model_policy{policy.include_usage,policy.max_output_tokens,std::move(identity)};
        model_policy.request_context=[this](std::stop_token stop){return context.prepare(stop);};
        model_policy.admit_context=[this](const std::string& attempt,const ModelRequest& request,std::stop_token stop){context.admit(attempt,request,stop);};
        AgentWorkerJournalModelHooks hooks;
        if(policy.context){
            context_window=std::make_unique<AgentWorkerContextWindow>(store,lease,provider,*policy.context,gateway,
                [this](std::stop_token stop){return context.prepare(stop);},
                [this](const std::string& attempt,const ModelRequest& raw,std::stop_token stop){context.admit(attempt,raw,stop);},deadline,cancel,policy.planning?policy.planning->budget:policy.context_budget);
            model_policy.request_context=[this](std::stop_token stop){return context_window->prepare(stop);};
            model_policy.admit_context=[this](const std::string& attempt,const ModelRequest& request,std::stop_token stop){context_window->admit(attempt,request,stop);};
            model_policy.reject_context=[this](const std::string& attempt,const ModelRequest& request,std::int64_t elapsed,std::stop_token stop){return context_window->reject(attempt,request,elapsed,stop);};
            model_policy.rebuild_context=[this](const std::string& attempt,const ModelRequest& request,std::stop_token stop){return context_window->rebuild(attempt,request,stop);};
            hooks.reserve=[this](const std::string& attempt,std::stop_token stop){return context_window->reserve(attempt,stop);};
            hooks.start=[this](const std::string& attempt,std::stop_token stop){return context_window->start(attempt,stop);};
            hooks.finish=[this](const std::string& attempt,std::stop_token stop){return context_window->finish(attempt,stop);};
            if(policy.planning)hooks.reserve_planning_continuation=[this](const std::string& call,const std::string& attempt,std::stop_token stop){return context_window->reserve_planning_continuation(call,attempt,stop);};
        }
        if(policy.planning&&!context_window)hooks.reserve_planning_continuation=[&store,budget=policy.planning->budget](const std::string& call,const std::string& attempt,std::stop_token stop){if(!attempt.empty()&&store.dynamic_plan_call(call).get().continuation_attempt_id!=attempt)throw Conflict("Worker changed the backend-selected continuation attempt");return budget->reserve_worker_continuation(call,stop);};
        if(policy.planning)hooks.complete=[&store,lease,budget=policy.planning->budget,deadline,cancel](const std::string& attempt,std::stop_token stop){budget->check(stop);return store.complete_agent_worker_dynamic_run(lease,attempt,budget->segment_id(),budget->active_elapsed_ms(),deadline,cancel,stop).get();};
        if(policy.planning)native_planning=std::make_unique<NativePlanningBinding>(bind_agent_worker_planning_context(policy.planning->callbacks,store,lease,context_window.get()));
        model=std::make_unique<AgentWorkerModelHost>(store,lease,std::move(provider),std::move(policy.tools),std::move(model_policy),std::move(gateway),deadline,cancel);
        native_journal=std::make_unique<AgentWorkerJournalHost>(store,context.files(),deadline,cancel,hooks);
        native_model=std::make_unique<NativeWorkerModelBinding>(*model,native_journal.get(),&context,&context,&context,&context,&context,native_planning.get());
    }
};
AgentWorkerCodingOwner::AgentWorkerCodingOwner(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,
    AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,ChatProviderConfig provider,std::string identity,
    std::shared_ptr<AgentModelGateway> gateway,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::shared_ptr<AgentProcessToolHost> process_owner,std::optional<AgentRemoteToolOwnerContext> remote_owner,std::optional<AgentCallableToolOwnerContext> callable_owner)
    :impl_(std::make_unique<Impl>(store,std::move(lease),scope,std::move(owner),std::move(policy),std::move(provider),std::move(identity),std::move(gateway),deadline,cancel,std::move(process_owner),std::move(remote_owner),std::move(callable_owner))){}
AgentWorkerCodingOwner::~AgentWorkerCodingOwner()=default;
bool AgentWorkerCodingOwner::outcome_unrecorded()const noexcept{return impl_->native_model->outcome_unrecorded();}
NativeWorkerModelAccess AgentWorkerCodingOwner::native_model_access()const{return impl_->native_model->access();}
}
