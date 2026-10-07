#pragma once
#include "agentflow/edit_executor.hpp"
namespace agentflow {
// Uses the same claimed file-effect ownership/fail-stop contract as edits.
class CreateExecutor {
public:
    CreateExecutor(PersistenceService& store,WorkspaceTools& workspace):store_(store),workspace_(workspace){}
    static ModelToolDefinition definition();
    std::string invoke(const std::string& operation_id,const std::string& run_id,const std::string& arguments_json,std::int64_t expires_unix_ms,std::stop_token cancel={});
    WorkspaceSnapshot execute(const std::string& operation_id,const std::string& run_id,WorkspaceCreatePlan plan,std::int64_t expires_unix_ms,std::stop_token cancel={});
private:
    PersistenceService& store_;WorkspaceTools& workspace_;
};
}
