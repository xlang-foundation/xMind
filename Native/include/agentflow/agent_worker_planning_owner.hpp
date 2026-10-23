#pragma once
#include "agentflow/agent_worker_native_planning.hpp"

namespace agentflow {
class PersistenceService;
struct AgentWorkerLeaseSpec;
struct AgentSettings;
class AgentWorkerContextWindow;
class NativeChildSlot;
struct AgentWorkerPlanningServices {
    NativePlanningCallbacks callbacks;
    std::shared_ptr<RootExecutionBudget> budget;
};
// Backend-only composition for the existing registered Planning service. The
// child identifies stored facts; these callbacks retain all execution authority.
NativePlanningCallbacks make_agent_worker_planning_callbacks(
    PersistenceService&,AgentWorkerLeaseSpec,AgentSettings,
    std::shared_ptr<DynamicPlanExecution>,std::shared_ptr<RootExecutionBudget>,
    AgentWorkerChannel::Deadline,std::stop_token owner_cancel={},DynamicInstructionPrecondition={},std::shared_ptr<NativeChildSlot> slot={});
NativePlanningCallbacks bind_agent_worker_planning_context(NativePlanningCallbacks,
    PersistenceService&,AgentWorkerLeaseSpec,AgentWorkerContextWindow*,DynamicInstructionPrecondition={});
}
