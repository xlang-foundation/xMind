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
    bool supports_agent_selection()const override{return true;}
    std::vector<AgentDefinitionMetadata> agent_definitions()const override;
    SessionAgentState session_agent(const std::string& session)const override;
    SessionAgentState select_session_agent(const std::string& session,std::optional<AgentDefinitionMetadata> selected,std::int64_t expected_revision)override;
    bool supports_delegation() const override;
    bool supports_dynamic_planning() const override;
    bool supports_context() const override;
    ContextControlSnapshot context_status(const std::string& session,const std::string& model={})const override;
    ContextManualStatus context_request(const std::string& session,const std::string& request,const std::string& model={})const override;
    ContextManualStatus request_context(const std::string& session,const std::string& request,
        const std::string& actor,std::int64_t expected_head_revision,const std::string& model={})override;
    Run plan_input(const std::string& root,const std::string& request,std::string input_json,
        const std::string& authenticated_actor,std::int64_t expected_revision,std::int64_t expected_state_sequence) override;
    Run resume_plan(const std::string& root,const std::string& authenticated_actor,
        std::int64_t expected_revision,std::int64_t expected_state_sequence) override;
    void cancel(const std::string& id) override;
    bool healthy() const override;
    bool idle() const override;
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
