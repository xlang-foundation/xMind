#pragma once
#include "agentflow/edit_executor.hpp"

namespace agentflow {
struct PatchFileIdentity {std::string patch_id;std::size_t index=0,file_count=0;};
// One native, immutable per-file patch proposal/claim/outcome. Batch orchestration
// must prepare every file before dispatch and retain earlier outcomes on failure.
class PatchFileExecutor {
public:
    PatchFileExecutor(PersistenceService& store,WorkspaceTools& workspace):store_(store),workspace_(workspace){}
    OperationSpec proposal(const std::string& operation_id,const std::string& run_id,
        const WorkspacePatchFilePlan& plan,const PatchFileIdentity& identity,
        const InstructionPrecondition& guidance={}) const;
    std::string execute(const std::string& operation_id,const std::string& run_id,
        WorkspacePatchFilePlan plan,PatchFileIdentity identity,
        std::int64_t expires_unix_ms,std::stop_token cancel={},InstructionPrecondition guidance={});
private:
    PersistenceService& store_;WorkspaceTools& workspace_;
};
}
