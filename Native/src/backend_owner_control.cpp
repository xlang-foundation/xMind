#include "agentflow/backend_owner_control.hpp"
#include "agentflow/graph_service.hpp"
#include <mutex>

namespace agentflow {
BackendOwnerControl::BackendOwnerControl(PersistenceService& store,RunExecutor& executor,VerifiedRuntimeGeneration& runtime,GraphExecution* graphs)
    :store_(store),executor_(executor),runtime_(runtime),graphs_(graphs){
    runtime_.require_current_server();if(!executor_.execution_workspace().configured)throw RunUnavailable("Native owner controls require a captured execution workspace");generation_=store_.backend_owner().get().generation;
}
BackendOwnerState BackendOwnerControl::status()const{auto result=store_.backend_owner().get();if(result.generation!=generation_)throw Conflict("Native owner generation changed");return result;}
std::shared_lock<std::shared_mutex> BackendOwnerControl::admit(){std::shared_lock lock(admission_,std::try_to_lock);if(!lock.owns_lock())throw PersistenceBusy("Native owner command is in flight");if(status().quiesced)throw BackendQuiesced("Native backend admission is closed");return lock;}
void BackendOwnerControl::workspace(const WorkspaceAdmission& expected)const{validate_workspace_admission(expected,executor_.execution_workspace());}
void BackendOwnerControl::idle()const{if(!executor_.healthy()||!executor_.idle()||(graphs_&&(!graphs_->healthy()||!graphs_->idle())))throw Conflict("Native execution must be healthy and idle before owner control");}
BackendOwnerState BackendOwnerControl::quiesce(BackendOwnerPrecondition expected,WorkspaceAdmission authority){std::unique_lock lock(admission_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Native admission is in flight");workspace(authority);idle();runtime_.require_current_server();if(expected.generation!=generation_)throw Conflict("Native owner generation changed");return store_.quiesce_backend(std::move(expected)).get();}
BackendOwnerState BackendOwnerControl::resume(BackendOwnerReceipt expected,WorkspaceAdmission authority){std::unique_lock lock(admission_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Native admission is in flight");workspace(authority);idle();runtime_.require_current_server();if(expected.generation!=generation_)throw Conflict("Native owner generation changed");return store_.resume_backend(std::move(expected)).get();}
}
