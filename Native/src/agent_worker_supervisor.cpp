#include "agentflow/agent_worker_supervisor.hpp"
#include "agentflow/agent_worker_control.hpp"
#include "agentflow/persistence_service.hpp"
#include <mutex>
#include <set>

namespace agentflow {
struct AgentWorkerSupervisor::Impl {
    PersistenceService& store;
    AgentWorkerChannel::Deadline deadline;
    std::unique_ptr<AgentWorkerProcess> worker;
    AgentWorkerLeaseSpec lease;
    std::unique_ptr<AgentWorkerReadOwner> owner;
    std::unique_ptr<AgentWorkerRpcPeer> peer;
    std::mutex mutex;
    bool started=false,ready=false,executed=false,draining=false;
    std::size_t callers=0;
    Impl(PersistenceService& persistence,std::shared_ptr<const BoundProcessExecutable> image,
        AgentWorkerScope scope,std::string identity,AgentWorkerChannel::Deadline until)
        :store(persistence),deadline(until){
        if(scope.root_id!=scope.run_id)throw std::invalid_argument("Worker supervisor requires an ordinary root");
        worker=AgentWorkerProcess::launch(std::move(image),scope,deadline);
        lease=store.admit_agent_worker({scope.generation,scope.root_id,scope.run_id,scope.workspace_id,
            scope.workspace_revision,worker->pid(),std::to_string(worker->creation_time()),worker->executable_identity(),std::move(identity)}).get().spec;
    }
    struct Caller {
        Impl& impl;
        explicit Caller(Impl& value,bool execution):impl(value){
            std::lock_guard lock(impl.mutex);
            if(!impl.ready||!impl.peer||impl.draining||(execution&&impl.executed))throw Conflict("Worker supervisor control is not available");
            if(execution)impl.executed=true;
            ++impl.callers;
        }
        ~Caller(){std::lock_guard lock(impl.mutex);--impl.callers;}
    };
};
AgentWorkerSupervisor::AgentWorkerSupervisor(PersistenceService& store,std::shared_ptr<const BoundProcessExecutable> image,
    AgentWorkerScope scope,std::string identity,AgentWorkerChannel::Deadline deadline)
    :impl_(std::make_unique<Impl>(store,std::move(image),std::move(scope),std::move(identity),deadline)){}
AgentWorkerSupervisor::~AgentWorkerSupervisor()=default;
void AgentWorkerSupervisor::start(AgentReadToolOwnerContext context,AgentWorkerReadRequestPolicy policy,ChatProviderConfig config,
    std::string identity,std::shared_ptr<AgentModelGateway> gateway,std::stop_token cancel){
    std::lock_guard lock(impl_->mutex);
    if(impl_->started||impl_->draining)throw Conflict("Worker supervisor cannot restart");
    impl_->started=true; // Startup failure is not permission to retry this child.
    impl_->owner=std::make_unique<AgentWorkerReadOwner>(impl_->store,impl_->lease,impl_->worker->channel().scope(),
        std::move(context),std::move(policy),std::move(config),std::move(identity),std::move(gateway),impl_->deadline,cancel);
    impl_->peer=std::make_unique<AgentWorkerRpcPeer>(impl_->worker->channel(),
        [owner=impl_->owner.get()](const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token stop){return owner->dispatch(method,payload,std::move(sink),stop);},impl_->deadline);
    impl_->worker->channel().send({"worker.boot","{\"version\":1}"},impl_->deadline,cancel);
    impl_->peer->start();
    impl_->ready=true;
}
RunState AgentWorkerSupervisor::execute(){Impl::Caller call(*impl_,true);return request_agent_worker_execution(*impl_->peer);}
void AgentWorkerSupervisor::cancel(){Impl::Caller call(*impl_,false);request_agent_worker_cancellation(*impl_->peer);}
AgentWorkerTerminalObservation AgentWorkerSupervisor::drain_completed(AgentWorkerChannel::Deadline deadline){
    std::lock_guard lock(impl_->mutex);
    if(impl_->callers||!impl_->peer)throw Conflict("Worker supervisor requires joined callers and a bound peer");
    // The retirement helper checks durable completion before closing the peer.
    try{const auto result=drain_completed_agent_worker(impl_->store,*impl_->worker,*impl_->peer,impl_->lease,deadline);
        impl_->draining=true;return result;}
    catch(...){if(!impl_->peer->alive())impl_->draining=true;throw;}
}
AgentWorkerTerminalObservation AgentWorkerSupervisor::drain_terminal(RunState outcome,const std::string& reason,AgentWorkerChannel::Deadline deadline){
    std::lock_guard lock(impl_->mutex);
    if(impl_->callers)throw Conflict("Worker supervisor requires joined callers before drain");
    const std::set<std::string> failures{"deadline","provider_failure","worker_exit","dispatch_error"};
    if(!((outcome==RunState::cancelled&&reason=="cancelled")||(outcome==RunState::failed&&failures.contains(reason))))throw std::invalid_argument("Invalid worker terminal observation");
    impl_->draining=true;
    if(impl_->peer)return drain_and_terminate_agent_worker(impl_->store,*impl_->worker,*impl_->peer,impl_->lease,outcome,reason,deadline);
    // Failed startup can precede RPC construction. Revalidate authority, close
    // only this retained channel, observe exact exit, then persist retirement.
    impl_->store.read_agent_worker_journal(impl_->lease,{AgentWorkerJournalReadKind::run,impl_->lease.owner_run_id}).get();
    impl_->worker->channel().close();const auto exit=impl_->worker->wait(deadline);
    return {impl_->store.terminate_agent_worker(impl_->lease,outcome,reason).get(),exit};
}
const AgentWorkerLeaseSpec& AgentWorkerSupervisor::lease()const noexcept{return impl_->lease;}
bool AgentWorkerSupervisor::alive()const{return impl_->worker->alive();}
}
