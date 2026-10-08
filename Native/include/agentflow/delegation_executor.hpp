#pragma once
#include "agentflow/agent_runner.hpp"
#include "agentflow/root_execution_budget.hpp"

namespace agentflow {
struct DelegationCapacityUnavailable : std::runtime_error {using std::runtime_error::runtime_error;};
struct DelegationOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
// Shared bounded leaf pool, independent of occupied AgentService root workers.
// Persistence and immutable execution generation outlive its jobs.
class DelegationExecutor {
public:
    DelegationExecutor(PersistenceService& store,std::size_t workers=4,std::size_t capacity=64);
    ~DelegationExecutor();
    DelegationExecutor(const DelegationExecutor&)=delete;
    DelegationExecutor& operator=(const DelegationExecutor&)=delete;
    static ModelToolDefinition definition();
    std::string invoke(const std::string& parent,const ModelToolCall& call,
        const std::string& actual_assistant_json,const AgentSettings& frozen,
        std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel={});
    bool healthy() const;
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
