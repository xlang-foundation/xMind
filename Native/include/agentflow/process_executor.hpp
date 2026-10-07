#pragma once
#include "agentflow/process.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/workspace_tools.hpp"

namespace agentflow {
struct ProcessOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
struct ProcessProfile {
    std::string id,executable;
    std::int64_t revision;
    std::vector<std::string> prefix_arguments;
    std::chrono::milliseconds max_timeout{120000};
    std::string executable_id;
};
// Backend-owned immutable profile snapshot. The model chooses a registered ID
// and literal arguments, never an executable, environment, identity or grant.
// Normal/nonzero exits acknowledge process lifecycle, not independent effects.
// Interrupted dispatch remains uncertain; unrecorded outcomes require fail-stop.
class ProcessExecutor {
public:
    ProcessExecutor(PersistenceService& store,WorkspaceTools& workspace,
        std::string workspace_root,std::vector<ProcessProfile> profiles);
    ModelToolDefinition definition() const;
    std::string invoke(const std::string& operation_id,const std::string& run_id,
        const std::string& arguments_json,std::int64_t expires_unix_ms,std::stop_token cancel={});
private:
    PersistenceService& store_;
    WorkspaceTools& workspace_;
    std::string root_;
    std::vector<ProcessProfile> profiles_;
};
}
