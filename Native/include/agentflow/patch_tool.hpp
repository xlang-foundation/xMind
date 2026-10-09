#pragma once
#include "agentflow/patch_executor.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include <optional>
namespace agentflow {
struct PreparedPatch {
    WorkspacePatchPlan plan;
    std::vector<InstructionPrecondition> guidance;
};
// Native model adapter. Parsing and guidance discovery never create proposals
// or effects. A missing delivered scope requires a new actual model request.
class PatchTool {
public:
    PatchTool(PersistenceService& store,WorkspaceTools& workspace,RepositoryInstructionContext& context):store_(store),workspace_(workspace),context_(context){}
    static ModelToolDefinition definition();
    std::optional<PreparedPatch> prepare(const std::string& arguments_json,std::stop_token cancel={});
    std::string execute(std::string patch_id,std::string run_id,PreparedPatch prepared,std::int64_t expiry,std::stop_token cancel={});
private:
    std::vector<std::string> directories(const WorkspacePatchFilePlan& plan,std::stop_token cancel);
    std::optional<InstructionPrecondition> guidance(const std::vector<std::string>& directories,std::stop_token cancel);
    PersistenceService& store_;WorkspaceTools& workspace_;RepositoryInstructionContext& context_;
};
}
