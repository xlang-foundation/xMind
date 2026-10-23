#include "agentflow/agent_worker_retirement.hpp"
#include "agent_worker_retirement_checks.hpp"
#include "agentflow/agent_worker_native_execution.hpp"
namespace agentflow {
using namespace worker_retirement_detail;
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
