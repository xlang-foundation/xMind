#pragma once
#include "agentflow/agent_runner.hpp"
#include "agentflow/run_executor.hpp"

namespace agentflow {
class AgentService final : public RunExecutor {
public:
    AgentService(PersistenceService& persistence,AgentSettings settings,
        std::size_t workers=2,std::size_t max_pending=128);
    ~AgentService();
    AgentService(const AgentService&)=delete;
    AgentService& operator=(const AgentService&)=delete;
    Run submit(std::string id,std::string session_id,std::string prompt) override;
    void cancel(const std::string& id) override;
    bool healthy() const override;
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
