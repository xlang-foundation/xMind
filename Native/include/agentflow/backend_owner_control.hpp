#pragma once
#include "agentflow/persistence_service.hpp"
#include "agentflow/run_executor.hpp"
#include "agentflow/runtime_generation.hpp"
#include <shared_mutex>

namespace agentflow {
class GraphExecution;
// Authenticated access adapters call this native boundary. Proof, persistence
// and execution must outlive it; it must outlive attached transports. Retirement
// and replacement startup are separate capabilities and are not implied here.
class BackendOwnerControl {
public:
    BackendOwnerControl(PersistenceService&,RunExecutor&,VerifiedRuntimeGeneration&,GraphExecution* = nullptr);
    std::shared_lock<std::shared_mutex> admit();
    BackendOwnerState status()const;
    bool covers(PersistenceService& store,RunExecutor* executor,GraphExecution* graphs)const{return &store==&store_&&executor==&executor_&&graphs==graphs_;}
    BackendOwnerState quiesce(BackendOwnerPrecondition,WorkspaceAdmission);
    BackendOwnerState resume(BackendOwnerReceipt,WorkspaceAdmission);
private:
    PersistenceService& store_;RunExecutor& executor_;VerifiedRuntimeGeneration& runtime_;
    GraphExecution* graphs_;std::string generation_;std::shared_mutex admission_;
    void workspace(const WorkspaceAdmission&)const;
    void idle()const;
};
}
