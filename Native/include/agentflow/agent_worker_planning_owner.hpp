#pragma once
#include "agentflow/agent_worker_native_planning.hpp"

namespace agentflow {
class PersistenceService;
struct AgentWorkerLeaseSpec;
struct AgentSettings;
// Backend-only composition for the existing registered Planning service. The
// child identifies stored facts; these callbacks retain all execution authority.
NativePlanningCallbacks make_agent_worker_planning_callbacks(
    PersistenceService&,AgentWorkerLeaseSpec,AgentSettings,
    std::shared_ptr<DynamicPlanExecution>,std::shared_ptr<RootExecutionBudget>,
    AgentWorkerChannel::Deadline,std::stop_token owner_cancel={},DynamicInstructionPrecondition={});
}
