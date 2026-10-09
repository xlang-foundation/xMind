#pragma once
#include "agentflow/agent_service.hpp"
#include "agentflow/graph_service.hpp"
namespace agentflow {
// A configured backend execution generation. Provider changes are permitted
// only when both single-agent and graph ownership have become idle.
class ExecutionPlatform final : public RunExecutor,public GraphExecution {
public:
    ExecutionPlatform(PersistenceService& store,AgentSettings settings,
        std::size_t workers=2,std::size_t capacity=128);
    ~ExecutionPlatform();
    Run submit(std::string id,std::string session,std::string prompt) override;
    Run submit_model(std::string id,std::string session,std::string prompt,std::string model) override;
    Run submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity) override;
    std::vector<std::string> models() const override;
    ExecutionWorkspaceMetadata execution_workspace()const override;
    bool supports_skill_catalogue()const override;
    WorkspaceSkillCatalogue workspace_skills()const override;
    Run submit_workspace(std::string id,std::string session,std::string prompt,std::string model,
        WorkspaceAdmission expected,std::optional<ProviderProfileAdmission> profile={})override;
    Run submit_graph_workspace(std::string id,std::string session,std::string graph,std::int64_t revision,
        std::string prompt,std::string model,WorkspaceAdmission expected,std::optional<ProviderProfileAdmission> profile={})override;
    bool supports_delegation() const override;
    bool supports_dynamic_planning() const override;
    bool supports_context() const override;
    ContextControlSnapshot context_status(const std::string& session,const std::string& model={})const override;
    ContextManualStatus context_request(const std::string& session,const std::string& request,const std::string& model={})const override;
    ContextManualStatus request_context(const std::string& session,const std::string& request,
        const std::string& actor,std::int64_t expected_head_revision,const std::string& model={})override;
    Run plan_input(const std::string& root,const std::string& request,std::string input_json,
        const std::string& actor,std::int64_t revision,std::int64_t state_sequence) override;
    Run resume_plan(const std::string& root,const std::string& actor,
        std::int64_t revision,std::int64_t state_sequence) override;
    void cancel(const std::string& id) override;
    bool healthy() const override;
    bool available() const override;
    bool idle() const;
    std::vector<GraphExecutionMetadata> graphs() const override;
    Run submit_graph(std::string id,std::string session,std::string graph,std::int64_t revision,
        std::string prompt,std::string model={}) override;
    GraphRootRecord human_input(const std::string& root,const std::string& node,
        const std::string& input,const std::string& actor,std::int64_t revision) override;
    Run resume_graph(const std::string& root,const std::string& actor,std::int64_t revision)override;
    GraphContextMetadata graph_context(const std::string& root)const override;
private:
    PersistenceService& store_;
    std::unique_ptr<WorkspaceTools> workspace_binding_;
    std::string workspace_authority_;
    std::unique_ptr<AgentService> agents_;
    std::unique_ptr<GraphService> graphs_;
};
}
