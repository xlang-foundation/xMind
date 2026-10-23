#pragma once
#include "agentflow/agent_worker_client_context.hpp"
#include "agentflow/agent_worker_records.hpp"
#include "agentflow/records.hpp"

namespace agentflow {
class PersistenceService;
class NativeExecutionClient;
struct AgentWorkerTerminalObservation {Run run;std::uint32_t exit_code=0;};
// Owner-only SDK drain validates exact process/lease identity and preserves
// ownership on observation timeout; it never restarts or replays work.
// Product path: native SDK execution control, no packet peer.
AgentWorkerTerminalObservation drain_and_terminate_agent_worker(
    PersistenceService&,AgentWorkerProcess&,NativeExecutionClient&,
    const AgentWorkerLeaseSpec&,RunState,const std::string&,AgentWorkerChannel::Deadline);
AgentWorkerTerminalObservation drain_completed_agent_worker(
    PersistenceService&,AgentWorkerProcess&,NativeExecutionClient&,
    const AgentWorkerLeaseSpec&,AgentWorkerChannel::Deadline);
}
