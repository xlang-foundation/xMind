#include "agentflow/agent_worker_retirement.hpp"
#include "agentflow/persistence_service.hpp"
#include <set>
#include "agentflow/agent_worker_native_execution.hpp"

namespace agentflow {
namespace {
void process_identity(AgentWorkerProcess& worker,const AgentWorkerScope& scope,const AgentWorkerLeaseSpec& lease){
    const AgentWorkerScope expected{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision};
    if(worker.pid()!=lease.pid||std::to_string(worker.creation_time())!=lease.creation_time||worker.executable_identity()!=lease.executable_identity||
       worker.channel().scope()!=expected||scope!=expected)
        throw Conflict("Worker drain differs from its captured process and channel");
}
Run completed(PersistenceService& store,const AgentWorkerLeaseSpec& lease){
    const auto captured=store.agent_worker_lease(lease.generation).get();
    const auto run=store.run(lease.owner_run_id).get();
    if(captured.spec!=lease||captured.state!="retired"||captured.retirement_reason!="completed"||
       run.id!=lease.owner_run_id||run.state!=RunState::completed||run.graph_root||(lease.root_run_id==lease.owner_run_id?!run.parent_id.empty():(run.parent_id.empty()||store.child_admission(run.id).get().root_run_id!=lease.root_run_id)))
        throw Conflict("Worker drain has no captured canonical completion");
    if(lease.root_run_id!=lease.owner_run_id){const auto child=store.child_admission(run.id).get();if(child.root_run_id!=lease.root_run_id||!((child.kind==ChildAdmissionKind::delegated_leaf&&child.preset_id=="workspace.inspect"&&child.preset_revision==1)||(child.kind==ChildAdmissionKind::graph_agent&&!child.graph_node_path.empty()&&store.root_budget(lease.root_run_id).get().spec.policy_id=="native.graph-context")))throw Conflict("Completed worker differs from captured child admission");}
    return run;
}
}
AgentWorkerTerminalObservation drain_and_terminate_agent_worker(
    PersistenceService& store,AgentWorkerProcess& worker,AgentWorkerRpcPeer& peer,
    const AgentWorkerLeaseSpec& lease,RunState outcome,const std::string& reason,
    AgentWorkerChannel::Deadline deadline){
    const std::set<std::string> failures{"deadline","provider_failure","worker_exit","dispatch_error"};
    if(!((outcome==RunState::cancelled&&reason=="cancelled")||(outcome==RunState::failed&&failures.contains(reason))))
        throw std::invalid_argument("Invalid worker terminal observation");
    process_identity(worker,peer.scope(),lease);
    // Revalidate current durable authority before shutting down the captured
    // process. The terminal transaction repeats ownership validation after drain.
    store.read_agent_worker_journal(lease,{AgentWorkerJournalReadKind::run,lease.owner_run_id}).get();
    const auto exit=peer.drain(worker,deadline);
    return {store.terminate_agent_worker(lease,outcome,reason).get(),exit};
}
AgentWorkerTerminalObservation drain_completed_agent_worker(PersistenceService& store,AgentWorkerProcess& worker,
    AgentWorkerRpcPeer& peer,const AgentWorkerLeaseSpec& lease,AgentWorkerChannel::Deadline deadline){
    process_identity(worker,peer.scope(),lease);(void)completed(store,lease);
    const auto exit=peer.drain(worker,deadline);
    return {completed(store,lease),exit};
}
AgentWorkerTerminalObservation drain_and_terminate_agent_worker(
    PersistenceService& store,AgentWorkerProcess& worker,NativeExecutionClient& control,
    const AgentWorkerLeaseSpec& lease,RunState outcome,const std::string& reason,AgentWorkerChannel::Deadline deadline){
    const std::set<std::string> failures{"deadline","provider_failure","worker_exit","dispatch_error"};
    if(!((outcome==RunState::cancelled&&reason=="cancelled")||(outcome==RunState::failed&&failures.contains(reason))))throw std::invalid_argument("Invalid worker terminal observation");
    process_identity(worker,control.scope(),lease);
    store.read_agent_worker_journal(lease,{AgentWorkerJournalReadKind::run,lease.owner_run_id}).get();
    control.close();worker.channel().close();const auto exit=worker.wait(deadline);
    return {store.terminate_agent_worker(lease,outcome,reason).get(),exit};
}
AgentWorkerTerminalObservation drain_completed_agent_worker(PersistenceService& store,AgentWorkerProcess& worker,
    NativeExecutionClient& control,const AgentWorkerLeaseSpec& lease,AgentWorkerChannel::Deadline deadline){
    process_identity(worker,control.scope(),lease);(void)completed(store,lease);
    control.close();worker.channel().close();const auto exit=worker.wait(deadline);
    return {completed(store,lease),exit};
}

}
