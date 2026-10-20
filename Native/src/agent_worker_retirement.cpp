#include "agentflow/agent_worker_retirement.hpp"
#include "agentflow/persistence_service.hpp"
#include <set>

namespace agentflow {
AgentWorkerTerminalObservation drain_and_terminate_agent_worker(
    PersistenceService& store,AgentWorkerProcess& worker,AgentWorkerRpcPeer& peer,
    const AgentWorkerLeaseSpec& lease,RunState outcome,const std::string& reason,
    AgentWorkerChannel::Deadline deadline){
    const std::set<std::string> failures{"deadline","provider_failure","worker_exit","dispatch_error"};
    if(!((outcome==RunState::cancelled&&reason=="cancelled")||(outcome==RunState::failed&&failures.contains(reason))))
        throw std::invalid_argument("Invalid worker terminal observation");
    const AgentWorkerScope expected{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision};
    if(worker.pid()!=lease.pid||std::to_string(worker.creation_time())!=lease.creation_time||worker.executable_identity()!=lease.executable_identity||
       worker.channel().scope()!=expected||peer.scope()!=expected)
        throw Conflict("Worker drain differs from its captured process and channel");
    // Revalidate current durable authority before shutting down the captured
    // process. The terminal transaction repeats ownership validation after drain.
    store.read_agent_worker_journal(lease,{AgentWorkerJournalReadKind::run,lease.owner_run_id}).get();
    const auto exit=peer.drain(worker,deadline);
    return {store.terminate_agent_worker(lease,outcome,reason).get(),exit};
}
}
