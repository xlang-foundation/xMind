#pragma once
#include "agentflow/graph.hpp"
#include "agentflow/agent_runner.hpp"
#include "agentflow/run_executor.hpp"
namespace agentflow {
struct GraphMcpUnavailable : RunUnavailable {using RunUnavailable::RunUnavailable;};
struct GraphContextUnavailable : RunUnavailable {using RunUnavailable::RunUnavailable;};
// Executes only backend-registered graph plans, using the shared agent engine
// and native tool/effect handlers. Persistence must outlive workers.
class GraphRunner {
public:
    GraphRunner(PersistenceService& store,AgentSettings settings,std::size_t max_parallel=2);
    ~GraphRunner();
    void validate(const GraphPlan& plan,const std::string& model_id={}) const;
    std::vector<std::string> models() const;
    std::string provider_context(const std::string& model_id={}) const;
    std::optional<RootBudgetSpec> execution_budget(const GraphPlan&,const std::string& model_id={}) const;
    std::optional<GraphContextSpec> execution_context(const GraphPlan&,const std::string& model_id={}) const;
    std::optional<GraphContextOwnerRecord> captured_context(const std::string& root_id) const;
    // Metadata-only adoption/input preflight. Never opens a clock or a peer.
    void validate_context_owner(const std::string& root_id) const;
    // Controller retirement of queued or clean closed paused ownership only.
    Run retire(const std::string& root_id,RunState terminal,const std::string& reason_json);
    // Service shutdown preserves a committed human pause for the next owner.
    // Explicit cancellation is still retired by that service's controller.
    Run execute(const std::string& root_id,std::stop_token cancel={},bool preserve_human_pause=false);
private:
    Run tool(const std::string& child_id,GraphPreparedNode node,std::stop_token cancel,
        std::chrono::steady_clock::time_point deadline,bool shared_context=false);
    PersistenceService& store_;
    AgentSettings settings_;
    std::size_t limit_;
    std::unique_ptr<AgentRunner> agents_;
    std::unique_ptr<WorkspaceTools> workspace_;
    std::unique_ptr<ProcessExecutor> process_;
};
}
