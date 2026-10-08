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
    Run submit_model(std::string id,std::string session_id,std::string prompt,std::string model_id) override;
    Run submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity) override;
    std::vector<std::string> models() const override;
    bool supports_delegation() const override;
    void cancel(const std::string& id) override;
    bool healthy() const override;
    bool idle() const;
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
