#include "agentflow/agent_worker_read_owner.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/agent_worker_context_window.hpp"
#include "agentflow/agent_worker_planning_owner.hpp"

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
    std::unique_ptr<AgentWorkerJournalHost> native_journal;
    std::unique_ptr<NativePlanningBinding> native_planning;
    std::unique_ptr<NativeWorkerModelBinding> native_model;
    Impl(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,
        AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,ChatProviderConfig provider,
        std::string identity,std::shared_ptr<AgentModelGateway> gateway,
        AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::function<void(AgentWorkerJournalReadKind,std::stop_token)> journal_read)
        :context(store,store,lease,captured_owner(std::move(owner),deadline),policy,cancel){
        if(scope!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Read owner differs from captured worker scope");
        AgentWorkerModelPolicy model_policy{policy.include_usage,policy.max_output_tokens,std::move(identity)};
        model_policy.request_context=[this](std::stop_token stop){return context.prepare(stop);};
        model_policy.admit_context=[this](const std::string& attempt,const ModelRequest& request,std::stop_token stop){context.admit(attempt,request,stop);};
        AgentWorkerJournalModelHooks hooks;hooks.before_read=std::move(journal_read);
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
        if(policy.planning)hooks.complete=[&store,lease,budget=policy.planning->budget,deadline,cancel](const std::string& attempt,std::stop_token stop){budget->check(stop);if(lease.owner_run_id!=lease.root_run_id)return store.complete_agent_worker_run(lease,attempt,deadline,cancel,stop).get();return store.complete_agent_worker_dynamic_run(lease,attempt,budget->segment_id(),budget->active_elapsed_ms(),deadline,cancel,stop).get();};
        if(policy.planning)native_planning=std::make_unique<NativePlanningBinding>(bind_agent_worker_planning_context(policy.planning->callbacks,store,lease,context_window.get(),[this](const std::string& directory,std::stop_token stop){return context.planning_guidance(directory,stop);}));
        model=std::make_unique<AgentWorkerModelHost>(store,lease,std::move(provider),std::move(policy.tools),
            std::move(model_policy),std::move(gateway),deadline,cancel);
        native_journal=std::make_unique<AgentWorkerJournalHost>(store,lease,deadline,cancel,hooks);
        native_model=std::make_unique<NativeWorkerModelBinding>(*model,native_journal.get(),&context,nullptr,nullptr,nullptr,nullptr,native_planning.get());
    }
};
AgentWorkerReadOwner::AgentWorkerReadOwner(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,
    AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,ChatProviderConfig provider,
    std::string identity,std::shared_ptr<AgentModelGateway> gateway,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,std::function<void(AgentWorkerJournalReadKind,std::stop_token)> journal_read)
    :impl_(std::make_unique<Impl>(store,std::move(lease),scope,std::move(owner),std::move(policy),std::move(provider),std::move(identity),std::move(gateway),deadline,cancel,std::move(journal_read))){}
AgentWorkerReadOwner::~AgentWorkerReadOwner()=default;
bool AgentWorkerReadOwner::outcome_unrecorded()const noexcept{return impl_->native_model->outcome_unrecorded();}
NativeWorkerModelAccess AgentWorkerReadOwner::native_model_access()const{return impl_->native_model->access();}
}
