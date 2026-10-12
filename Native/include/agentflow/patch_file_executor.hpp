#pragma once
#include "agentflow/edit_executor.hpp"

namespace agentflow {
struct PatchFileIdentity {std::string patch_id;std::size_t index=0,file_count=0;std::string manifest_json;};
// One native, immutable per-file patch proposal/claim/outcome. Batch orchestration
// must prepare every file before dispatch and retain earlier outcomes on failure.
class PatchFileExecutor {
public:
    PatchFileExecutor(PersistenceService& store,WorkspaceTools& workspace,std::string owner_workspace={},
        std::string folder_id={},std::int64_t workspace_revision=1)
        :store_(store),workspace_(workspace),owner_workspace_(owner_workspace.empty()?workspace.identity():std::move(owner_workspace)),
         folder_id_(folder_id.empty()?workspace.identity():std::move(folder_id)),workspace_revision_(workspace_revision){}
    OperationSpec proposal(const std::string& operation_id,const std::string& run_id,
        const WorkspacePatchFilePlan& plan,const PatchFileIdentity& identity,
        const InstructionPrecondition& guidance={}) const;
    std::string execute(const std::string& operation_id,const std::string& run_id,
        WorkspacePatchFilePlan plan,PatchFileIdentity identity,
        std::int64_t expires_unix_ms,std::stop_token cancel={},InstructionPrecondition guidance={});
private:
    PersistenceService& store_;WorkspaceTools& workspace_;std::string owner_workspace_,folder_id_;std::int64_t workspace_revision_=1;
};
}
