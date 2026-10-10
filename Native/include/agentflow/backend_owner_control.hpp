#pragma once
#include "agentflow/persistence_service.hpp"
#include "agentflow/run_executor.hpp"
#include "agentflow/runtime_generation.hpp"
#include <shared_mutex>

namespace agentflow {
class GraphExecution;
class WorkspaceTools;
BackendOwnerTarget qualify_backend_target(VerifiedRuntimeGeneration&,const WorkspaceTools&,
    const std::string& auth_token,bool approved_edits,bool require_running_server=true);
BackendOwnerBootstrap qualify_backend_bootstrap(BackendOwnerReceipt,VerifiedRuntimeGeneration&,
    const WorkspaceTools&,const std::string& auth_token,bool approved_edits);
// Authenticated access adapters call this native boundary. Proof, persistence
// and execution must outlive it; it must outlive attached transports. Retirement
// and replacement startup are separate capabilities and are not implied here.
class BackendOwnerControl {
public:
    BackendOwnerControl(PersistenceService&,RunExecutor&,VerifiedRuntimeGeneration&,GraphExecution* = nullptr,
        const std::string& auth_token={},bool verified_managed_profile_startup=false);
    std::shared_lock<std::shared_mutex> admit();
    BackendOwnerState status()const;
    bool covers(PersistenceService& store,RunExecutor* executor,GraphExecution* graphs)const{return &store==&store_&&executor==&executor_&&graphs==graphs_;}
    BackendOwnerState quiesce(BackendOwnerPrecondition,WorkspaceAdmission);
    BackendOwnerState resume(BackendOwnerReceipt,WorkspaceAdmission);
    // Durable only; the caller must still arrange/observe actual shutdown.
    // No public retirement route until replacement startup is qualified.
    BackendOwnerState request_retirement(BackendOwnerReceipt,WorkspaceAdmission);
    bool replacement_supported()const{return !auth_binding_.empty();}
    BackendOwnerState retire_to(BackendOwnerReceipt,WorkspaceAdmission,VerifiedRuntimeGeneration&,bool approved_edits);
    BackendOwnerState activate_replacement(BackendOwnerReceipt,WorkspaceAdmission);
private:
    PersistenceService& store_;RunExecutor& executor_;VerifiedRuntimeGeneration& runtime_;
    GraphExecution* graphs_;std::string generation_,auth_binding_;std::shared_mutex admission_;
    void workspace(const WorkspaceAdmission&)const;
    void idle()const;
};
}
