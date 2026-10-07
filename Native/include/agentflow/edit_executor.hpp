#pragma once
#include "agentflow/permission_waiter.hpp"
#include "agentflow/workspace_tools.hpp"
#include "agentflow/instruction_precondition.hpp"

namespace agentflow {
struct EditOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
enum class EditSnapshotMatch {before,after,different};
struct EditRecoveryInspection {
    Operation operation;
    WorkspaceFingerprint observed;
    EditSnapshotMatch match;
    bool same_file;
    std::int64_t observed_unix_ms;
};
class EditRecoveryReader {
public:
    virtual ~EditRecoveryReader()=default;
    virtual EditRecoveryInspection inspect_uncertain(const std::string& operation_id,std::stop_token cancel={})=0;
};
// Owning backend adapter. Never called by a view directly. The durable proposal
// contains the exact before/after bytes; controllers authorize that payload.
// Journal failures after claim require stopping execution and recovery, never retry.
class EditExecutor : public EditRecoveryReader {
public:
    EditExecutor(PersistenceService& store,WorkspaceTools& workspace):store_(store),workspace_(workspace) {}
    WorkspaceSnapshot execute(const std::string& operation_id,const std::string& run_id,
        WorkspaceEditPlan plan,std::int64_t expires_unix_ms,std::stop_token cancel={},InstructionPrecondition guidance={});
    static ModelToolDefinition definition();
    std::string invoke(const std::string& operation_id,const std::string& run_id,
        const std::string& arguments_json,std::int64_t expires_unix_ms,std::stop_token cancel={},InstructionPrecondition guidance={});
    // Backend-only read of a quarantined edit. Observed bytes do not prove who
    // performed an effect; this does not journal success, replay or release it.
    EditRecoveryInspection inspect_uncertain(const std::string& operation_id,std::stop_token cancel={}) override;
private:
    PersistenceService& store_;
    WorkspaceTools& workspace_;
};
}
