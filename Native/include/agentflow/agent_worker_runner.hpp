#pragma once
#include "agentflow/agent_worker_run_journal.hpp"

namespace agentflow {
struct AgentWorkerRunResult {
    Run run;
    AgentWorkerJournalProgress progress;
};
// Execute the captured ordinary read root using only native RPC adapters.
// The peer must remain alive through this call and supervisor drain. Failure
// and cancellation propagate, including AgentTerminalHandoff; no Run is forged.
// Backend-owned request windows are required; no child policy is substituted.
AgentWorkerRunResult execute_agent_worker_read_root(AgentWorkerRpcPeer&,std::stop_token cancel={});
}
