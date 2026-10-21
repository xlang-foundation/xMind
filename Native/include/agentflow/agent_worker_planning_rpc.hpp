#pragma once
#include "agentflow/agent_worker_rpc.hpp"
#include "agentflow/dynamic_plan_execution.hpp"

namespace agentflow {
// Backend-only binding. These callbacks must resolve durable admission and the
// actual canonical model call before entering the native scheduler. The codec
// is not an authority grant; native settings/budgets/guidance never cross IPC.
struct AgentWorkerPlanningBinding {
    AgentWorkerScope scope;
    std::function<DynamicExecutionResult(const std::string& attempt,const std::string& call,std::stop_token)> invoke;
    std::function<DynamicExecutionResult(const std::string& accepted_call,std::stop_token)> resume;
    std::function<bool()> healthy;
};
AgentWorkerRpcHandler bind_agent_worker_planning_rpc(AgentWorkerPlanningBinding,const AgentWorkerScope&);
class AgentWorkerPlanningClient final:public DynamicPlanExecution {
public:
    explicit AgentWorkerPlanningClient(AgentWorkerRpcPeer& peer):peer_(peer){}
    DynamicExecutionResult invoke(const std::string&,const ModelToolCall&,const std::string&,const std::string&,
        const AgentSettings&,const DynamicPlanCapabilities&,std::shared_ptr<RootExecutionBudget>,std::stop_token={},DynamicInstructionPrecondition={})override;
    DynamicExecutionResult resume(const std::string&,const std::string&,const AgentSettings&,const DynamicPlanCapabilities&,
        std::shared_ptr<RootExecutionBudget>,std::stop_token={},DynamicInstructionPrecondition={})override;
    bool healthy()const override;
private:
    DynamicExecutionResult request(const std::string&,const std::string&,std::stop_token);
    AgentWorkerRpcPeer& peer_;
};
}
