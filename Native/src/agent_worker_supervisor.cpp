#include "agentflow/agent_worker_supervisor.hpp"
#include "agentflow/agent_worker_control.hpp"
#include "agentflow/agent_worker_coding_owner.hpp"
#include "agentflow/persistence_service.hpp"
#include <mutex>
#include <atomic>
#include <set>

namespace agentflow {
struct AgentWorkerSupervisor::Impl {
    PersistenceService& store;
    AgentWorkerChannel::Deadline deadline;
    std::unique_ptr<AgentWorkerProcess> worker;
    AgentWorkerLeaseSpec lease;
    std::unique_ptr<AgentWorkerReadOwner> owner;
    std::unique_ptr<AgentWorkerCodingOwner> coding_owner;
    std::unique_ptr<AgentWorkerRpcPeer> peer;
    std::mutex mutex;
    bool started=false,ready=false,executed=false,draining=false,launch_attempted=false,lease_admitted=false;
    std::shared_ptr<const BoundProcessExecutable> image;AgentWorkerScope scope;std::string identity;
    std::size_t callers=0;
    std::atomic<bool> outcome_unrecorded=false;
    Impl(PersistenceService& persistence,std::shared_ptr<const BoundProcessExecutable> image,
        AgentWorkerScope captured_scope,std::string captured_identity,AgentWorkerChannel::Deadline until,bool launch_now=true)
        :store(persistence),deadline(until),image(std::move(image)),scope(std::move(captured_scope)),identity(std::move(captured_identity)){
        if(scope.root_id!=scope.run_id){const auto admitted=store.child_admission(scope.run_id).get();if(admitted.root_run_id!=scope.root_id||!((admitted.kind==ChildAdmissionKind::delegated_leaf&&admitted.preset_id=="workspace.inspect"&&admitted.preset_revision==1)||(admitted.kind==ChildAdmissionKind::graph_agent&&!admitted.graph_node_path.empty())))throw std::invalid_argument("Worker supervisor requires exact agent child admission");}
        if(launch_now)launch();
    }
    void launch(){
        if(launch_attempted||draining)throw Conflict("Worker supervisor cannot relaunch");
        launch_attempted=true;
        worker=AgentWorkerProcess::launch(image,scope,deadline);
        lease=store.admit_agent_worker({scope.generation,scope.root_id,scope.run_id,scope.workspace_id,
            scope.workspace_revision,worker->pid(),std::to_string(worker->creation_time()),worker->executable_identity(),identity}).get().spec;
        lease_admitted=true;
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
AgentWorkerSupervisor::AgentWorkerSupervisor(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
std::unique_ptr<AgentWorkerSupervisor> AgentWorkerSupervisor::prepare(PersistenceService& store,std::shared_ptr<const BoundProcessExecutable> image,
    AgentWorkerScope scope,std::string identity,AgentWorkerChannel::Deadline deadline){
    return std::unique_ptr<AgentWorkerSupervisor>(new AgentWorkerSupervisor(std::make_unique<Impl>(store,std::move(image),std::move(scope),std::move(identity),deadline,false)));
}
void AgentWorkerSupervisor::launch(){std::lock_guard lock(impl_->mutex);impl_->launch();}
void AgentWorkerSupervisor::start(AgentReadToolOwnerContext context,AgentWorkerReadRequestPolicy policy,ChatProviderConfig config,
    std::string identity,std::shared_ptr<AgentModelGateway> gateway,std::stop_token cancel){
    std::lock_guard lock(impl_->mutex);
    if(!impl_->lease_admitted||impl_->started||impl_->draining)throw Conflict("Worker supervisor cannot restart");
    impl_->started=true; // Startup failure is not permission to retry this child.
    impl_->owner=std::make_unique<AgentWorkerReadOwner>(impl_->store,impl_->lease,impl_->worker->channel().scope(),
        std::move(context),std::move(policy),std::move(config),std::move(identity),std::move(gateway),impl_->deadline,cancel);
    impl_->peer=std::make_unique<AgentWorkerRpcPeer>(impl_->worker->channel(),
        [state=impl_.get(),owner=impl_->owner.get()](const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token stop){try{return owner->dispatch(method,payload,std::move(sink),stop);}catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="outcome_unrecorded")state->outcome_unrecorded=true;throw;}},impl_->deadline);
    impl_->worker->channel().send({"worker.boot","{\"version\":1}"},impl_->deadline,cancel);
    impl_->peer->start();
    impl_->ready=true;
}
void AgentWorkerSupervisor::start_coding(AgentReadToolOwnerContext context,AgentWorkerReadRequestPolicy policy,ChatProviderConfig config,
    std::string identity,std::shared_ptr<AgentModelGateway> gateway,std::stop_token cancel,std::shared_ptr<AgentProcessToolHost> process_owner,std::optional<AgentRemoteToolOwnerContext> remote_owner,std::optional<AgentCallableToolOwnerContext> callable_owner){
    std::lock_guard lock(impl_->mutex);
    if(!impl_->lease_admitted||impl_->started||impl_->draining)throw Conflict("Worker supervisor cannot restart");
    impl_->started=true; // Startup failure is not permission to retry this child.
    impl_->coding_owner=std::make_unique<AgentWorkerCodingOwner>(impl_->store,impl_->lease,impl_->worker->channel().scope(),
        std::move(context),std::move(policy),std::move(config),std::move(identity),std::move(gateway),impl_->deadline,cancel,std::move(process_owner),std::move(remote_owner),std::move(callable_owner));
    impl_->peer=std::make_unique<AgentWorkerRpcPeer>(impl_->worker->channel(),
        [state=impl_.get(),owner=impl_->coding_owner.get()](const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token stop){try{return owner->dispatch(method,payload,std::move(sink),stop);}catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="outcome_unrecorded")state->outcome_unrecorded=true;throw;}},impl_->deadline);
    impl_->worker->channel().send({"worker.boot","{\"version\":1,\"workspace\":\"coding\"}"},impl_->deadline,cancel);
    impl_->peer->start();
    impl_->ready=true;
}
RunState AgentWorkerSupervisor::execute(){Impl::Caller call(*impl_,true);return request_agent_worker_execution(*impl_->peer);}
void AgentWorkerSupervisor::cancel(){Impl::Caller call(*impl_,false);request_agent_worker_cancellation(*impl_->peer);}
AgentWorkerTerminalObservation AgentWorkerSupervisor::drain_completed(AgentWorkerChannel::Deadline deadline){
    std::lock_guard lock(impl_->mutex);
    if(impl_->callers||!impl_->lease_admitted||!impl_->peer)throw Conflict("Worker supervisor requires joined callers and a bound peer");
    // The retirement helper checks durable completion before closing the peer.
    try{const auto result=drain_completed_agent_worker(impl_->store,*impl_->worker,*impl_->peer,impl_->lease,deadline);
        impl_->draining=true;return result;}
    catch(...){if(!impl_->peer->alive())impl_->draining=true;throw;}
}
AgentWorkerTerminalObservation AgentWorkerSupervisor::drain_terminal(RunState outcome,const std::string& reason,AgentWorkerChannel::Deadline deadline){
    std::lock_guard lock(impl_->mutex);
    if(impl_->callers||!impl_->lease_admitted)throw Conflict("Worker supervisor requires joined callers and an admitted lease before terminal drain");
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
bool AgentWorkerSupervisor::outcome_unrecorded()const noexcept{return impl_->outcome_unrecorded.load();}
bool AgentWorkerSupervisor::alive()const{return impl_->worker&&impl_->worker->alive();}
bool AgentWorkerSupervisor::admitted()const{std::lock_guard lock(impl_->mutex);return impl_->lease_admitted;}
std::uint32_t AgentWorkerSupervisor::drain_physical(AgentWorkerChannel::Deadline deadline){
    std::lock_guard lock(impl_->mutex);if(impl_->callers)throw Conflict("Worker physical drain requires joined callers");impl_->draining=true;
    if(!impl_->worker){if(impl_->launch_attempted)throw AgentWorkerProtocolError("Worker launch requires physical reconciliation");return 0;}
    if(impl_->peer)return impl_->peer->drain(*impl_->worker,deadline);
    impl_->worker->channel().close();return impl_->worker->wait(deadline);
}
}
