#include "agent_worker_rpc_fixture.hpp"
#include "../src/agent_worker_retirement_checks.hpp"
namespace agentflow {
using namespace worker_retirement_detail;
AgentWorkerTerminalObservation drain_and_terminate_agent_worker(
    PersistenceService& store,AgentWorkerProcess& worker,FixtureWorkerRpcPeer& peer,
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
    FixtureWorkerRpcPeer& peer,const AgentWorkerLeaseSpec& lease,AgentWorkerChannel::Deadline deadline){
    process_identity(worker,peer.scope(),lease);(void)completed(store,lease);
    const auto exit=peer.drain(worker,deadline);
    return {completed(store,lease),exit};
}
}
