#pragma once
#include "agentflow/agent_worker_rpc.hpp"
#include "agentflow/agent_worker_records.hpp"
#include "agentflow/records.hpp"

namespace agentflow {
class PersistenceService;
struct AgentWorkerTerminalObservation {Run run;std::uint32_t exit_code=0;};
// Backend owner only; never call in an RPC handler/sink. External call threads
// must already have joined. Captured process identity and the exact channel
// are checked before shutdown. Retains ownership on timeout or recording error;
// the caller may observe the same process again, never infer death or retry work.
AgentWorkerTerminalObservation drain_and_terminate_agent_worker(
    PersistenceService&,AgentWorkerProcess&,AgentWorkerRpcPeer&,
    const AgentWorkerLeaseSpec&,RunState,const std::string& reason,
    AgentWorkerChannel::Deadline observation_deadline);
// Completion already committed atomically with lease retirement. Observe and
// drain that exact child without another terminal write, even if its reply was
// lost or it exited nonzero. Reobservation is read-only; timeout retains the
// same process/peer ownership. External call threads must already have joined.
AgentWorkerTerminalObservation drain_completed_agent_worker(
    PersistenceService&,AgentWorkerProcess&,AgentWorkerRpcPeer&,
    const AgentWorkerLeaseSpec&,AgentWorkerChannel::Deadline observation_deadline);
}
