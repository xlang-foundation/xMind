#pragma once
#include "agentflow/permission_waiter.hpp"
#include "agentflow/workspace_tools.hpp"

namespace agentflow {
struct EditOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
// Owning backend adapter. Never called by a view directly. The durable proposal
// contains the exact before/after bytes; controllers authorize that payload.
// Journal failures after claim require stopping execution and recovery, never retry.
class EditExecutor {
public:
    EditExecutor(PersistenceService& store,WorkspaceTools& workspace):store_(store),workspace_(workspace) {}
    WorkspaceSnapshot execute(const std::string& operation_id,const std::string& run_id,
        WorkspaceEditPlan plan,std::int64_t expires_unix_ms,std::stop_token cancel={});
private:
    PersistenceService& store_;
    WorkspaceTools& workspace_;
};
}
