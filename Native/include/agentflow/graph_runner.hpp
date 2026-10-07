#pragma once
#include "agentflow/graph.hpp"
#include "agentflow/agent_runner.hpp"
#include "agentflow/run_executor.hpp"
namespace agentflow {
// Executes only backend-registered graph plans, using the shared agent engine
// and native tool/effect handlers. Persistence must outlive workers.
class GraphRunner {
public:
    GraphRunner(PersistenceService& store,AgentSettings settings,std::size_t max_parallel=2);
    ~GraphRunner();
    void validate(const GraphPlan& plan,const std::string& model_id={}) const;
    std::vector<std::string> models() const;
    // Service shutdown preserves a committed human pause for the next owner.
    // Explicit cancellation is still retired by that service's controller.
    Run execute(const std::string& root_id,std::stop_token cancel={},bool preserve_human_pause=false);
private:
    Run tool(const std::string& child_id,GraphPreparedNode node,std::stop_token cancel);
    PersistenceService& store_;
    AgentSettings settings_;
    std::size_t limit_;
    std::unique_ptr<AgentRunner> agents_;
    std::unique_ptr<WorkspaceTools> workspace_;
    std::unique_ptr<ProcessExecutor> process_;
};
}
