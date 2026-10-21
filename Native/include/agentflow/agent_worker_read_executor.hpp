#pragma once
#include "agentflow/agent_root_executor.hpp"
#include "agentflow/agent_runner.hpp"
#include "agentflow/agent_worker_channel.hpp"

namespace agentflow {
AgentDelegatedExecution make_agent_worker_delegated_executor(PersistenceService&,std::shared_ptr<const BoundProcessExecutable>,std::shared_ptr<AgentModelGateway> gateway={});
// Trusted backend factory. Captures settings and executable once; no client
// supplies credentials, executable paths or per-dispatch replacement policy.
std::shared_ptr<AgentRootExecutor> make_agent_worker_read_executor(
    PersistenceService&,AgentSettings,std::shared_ptr<const BoundProcessExecutable>);
std::shared_ptr<AgentRootExecutor> make_agent_worker_coding_executor(
    PersistenceService&,AgentSettings,std::shared_ptr<const BoundProcessExecutable>);
}
