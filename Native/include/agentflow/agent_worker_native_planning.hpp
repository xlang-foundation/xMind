#pragma once
#include "agentflow/agent_worker_channel.hpp"
#include "agentflow/dynamic_plan_execution.hpp"
#include "xlang3/xlang3.h"
namespace agentflow {
// Backend callbacks resolve durable admission and canonical model facts. Worker
// arguments identify those facts; they never carry settings, grants or budgets.
struct NativePlanningCallbacks {
    AgentWorkerScope scope;
    std::function<DynamicExecutionResult(const std::string&,const std::string&,std::stop_token)> invoke;
    std::function<DynamicExecutionResult(const std::string&,std::stop_token)> resume;
    std::function<bool()> healthy;
    std::function<std::string()> pending;
};
struct NativePlanningAccess {std::string endpoint,ticket;std::uint32_t pid=0;};
struct NativePlanningState;
class NativePlanningBinding final {
public:
    explicit NativePlanningBinding(NativePlanningCallbacks);
    ~NativePlanningBinding();
    NativePlanningBinding(const NativePlanningBinding&)=delete;
    NativePlanningBinding& operator=(const NativePlanningBinding&)=delete;
    NativePlanningAccess access()const;
    AgentWorkerScope scope()const;
    X::Value object()const;
    bool outcome_unrecorded()const noexcept;
private:std::shared_ptr<NativePlanningState> state_;std::string ticket_;
};
struct NativePlanningApi {
    X::Value Open(std::string ticket);
    BEGIN_PACKAGE(NativePlanningApi)
        APISET().AddFunc<1>("Open",&NativePlanningApi::Open);
    END_PACKAGE
};
class NativePlanningClient final:public DynamicPlanExecution {
public:
    NativePlanningClient(NativePlanningAccess,AgentWorkerScope,AgentWorkerChannel::Deadline);
    ~NativePlanningClient();
    DynamicExecutionResult invoke(const std::string&,const ModelToolCall&,const std::string&,const std::string&,
        const AgentSettings&,const DynamicPlanCapabilities&,std::shared_ptr<RootExecutionBudget>,std::stop_token={},DynamicInstructionPrecondition={})override;
    DynamicExecutionResult resume(const std::string&,const std::string&,const AgentSettings&,const DynamicPlanCapabilities&,
        std::shared_ptr<RootExecutionBudget>,std::stop_token={},DynamicInstructionPrecondition={})override;
    bool healthy()const override;
    std::string pending(const std::string&,std::stop_token={})override;
private:
    friend class NativeWorkerModelClient;
    NativePlanningClient(X::Runtime&,X::Value,AgentWorkerScope,AgentWorkerChannel::Deadline,std::shared_ptr<void>);
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
