#pragma once
#include "agentflow/patch_executor.hpp"

namespace agentflow {
class RestoreExecutor {
public:
    using PathAuthorizer=std::function<void(const std::string&)>;
    using GuidanceFactory=std::function<InstructionPrecondition(const std::vector<std::string>&,std::stop_token)>;
    RestoreExecutor(PersistenceService& store,WorkspaceTools& workspace,std::string owner_workspace={},
        std::string folder_id={},std::int64_t workspace_revision=1)
        :store_(store),workspace_(workspace),owner_workspace_(owner_workspace.empty()?workspace.identity():std::move(owner_workspace)),
         folder_id_(folder_id.empty()?workspace.identity():std::move(folder_id)),workspace_revision_(workspace_revision){}
    static ModelToolDefinition definition();
    std::string invoke(const std::string& patch_id,const std::string& run_id,const std::string& arguments_json,
        std::int64_t expires_unix_ms,std::stop_token cancel={},PathAuthorizer authorize={},GuidanceFactory guidance={});
private:
    PersistenceService& store_;WorkspaceTools& workspace_;std::string owner_workspace_,folder_id_;std::int64_t workspace_revision_=1;
};
}
