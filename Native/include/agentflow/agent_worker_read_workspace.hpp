#pragma once
#include "agentflow/agent_runner_read_workspace.hpp"
#include "agentflow/agent_runner_coding_workspace.hpp"
#include "agentflow/agent_worker_rpc.hpp"

namespace agentflow {
class AgentWorkerOrdinaryJournal;
// Captured ordinary read workspace over owned RPC values. Does not open native
// roots, load local guidance or create policy/grants in the child. Backend
// request admission and native read authorization remain authoritative. The
// peer must outlive this adapter and every read host obtained from it.
std::shared_ptr<AgentRunnerReadWorkspace> make_agent_worker_read_workspace(
    AgentWorkerRpcPeer&,std::shared_ptr<AgentWorkerOrdinaryJournal>);
std::shared_ptr<AgentRunnerCodingWorkspace> make_agent_worker_coding_workspace(
    AgentWorkerRpcPeer&,std::shared_ptr<AgentWorkerOrdinaryJournal>);
}
