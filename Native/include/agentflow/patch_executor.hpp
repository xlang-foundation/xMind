#pragma once
#include "agentflow/patch_file_executor.hpp"
namespace agentflow {
// Native batch coordination, not an atomic filesystem transaction. Every file
// retains an independent durable proposal/decision/claim/actual outcome.
class PatchExecutor {
public:
    using GuidanceRefresh=std::function<InstructionPrecondition(std::size_t,const WorkspacePatchFilePlan&,std::stop_token)>;
    PatchExecutor(PersistenceService& store,WorkspaceTools& workspace):store_(store),workspace_(workspace){}
    std::string execute(std::string patch_id,std::string run_id,WorkspacePatchPlan plan,
        std::int64_t expires_unix_ms,std::stop_token cancel={},std::vector<InstructionPrecondition> guidance={},GuidanceRefresh refresh_guidance={});
private:
    PersistenceService& store_;WorkspaceTools& workspace_;
};
}
