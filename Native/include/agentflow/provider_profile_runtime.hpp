#pragma once
#include "agentflow/execution_platform.hpp"
#include "agentflow/provider_profiles.hpp"
#include "agentflow/provider_catalogue.hpp"
#include "agentflow/provider_profile_setup.hpp"
#include "agentflow/provider_model_policy.hpp"
#include <filesystem>
#include <map>
namespace agentflow {
// Native backend policy. Capabilities and credential destinations never come
// from a view; the selected model identity is supplied by its saved profile.
struct ProviderProfileExecutionPolicy {
    ProviderProfileRoute route;
    ChatProviderConfig provider;
    std::optional<ProviderCataloguePolicy> catalogue;
    // Optional per-model tool policy supplied by the backend, never by views or
    // account catalogue metadata. Unknown models retain provider.tools.
    std::map<std::string,Capability> model_tools;
    std::optional<ContextRuntimePolicy> context;
    // Optional exact native streamed-text declarations for this execution wire.
    // An enrolled route with this policy rejects unlisted models in every path.
    std::optional<ProviderModelPolicy> model_capabilities;
};
// Shared single/graph execution platform with backend-owned model profiles.
// Connection profiles (Local/Nexus) belong to a separate transport boundary.
class ProviderProfileRuntime final : public RunExecutor,public GraphExecution,public ProviderProfileSetup {
public:
    ProviderProfileRuntime(PersistenceService& store,AgentSettings base,
        std::vector<ProviderProfileExecutionPolicy> policy,std::size_t workers=2,std::size_t capacity=128);
    ~ProviderProfileRuntime();
    ProviderProfileRuntimeMetadata configuration() const override;
    std::vector<ProviderProfileRouteMetadata> profile_routes() const override;
    std::vector<std::string> discover_models(std::string id,std::string route,SecretBytes key,
        std::int64_t expected_revision,std::stop_token cancel={}) override;
    ProviderProfileRuntimeMetadata save_profile(std::string id,std::string route,std::string model,
        SecretBytes key,std::int64_t expected_revision,bool activate=false) override;
    ProviderProfileRuntimeMetadata select_profile(std::string id,std::int64_t expected_revision) override;
    // Trusted migration caller must validate the legacy source record against
    // backend policy. This preserves its owned encrypted reference and revision.
    ProviderProfileRuntimeMetadata import_existing_profile(std::string id,std::string route,std::string model,
        std::string credential_id,std::int64_t initial_revision);
    // Backend startup migration. Existing profile registries take precedence;
    // absent legacy records are a no-op. Invalid legacy state fails closed.
    bool import_legacy_configuration(std::string id="openai");
    // Trusted local startup file only. Keys are encrypted by the native batch;
    // absent models preserve existing choices and new key-only profiles remain
    // unconfigured until model discovery/selection through the normal API.
    ProviderProfileRuntimeMetadata import_yaml_configuration(const std::filesystem::path& absolute_path,
        std::int64_t expected_revision) override;
    Run submit(std::string id,std::string session,std::string prompt) override;
    Run submit_model(std::string id,std::string session,std::string prompt,std::string model) override;
    Run submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity) override;
    std::vector<std::string> models() const override;
    ExecutionWorkspaceMetadata execution_workspace()const override;
    bool supports_skill_catalogue()const override;
    bool supports_session_skills()const override{return supports_skill_catalogue();}
    bool supports_agent_selection()const override{return true;}
    std::vector<AgentDefinitionMetadata> agent_definitions()const override;
    SessionAgentState session_agent(const std::string& session)const override;
    SessionAgentState select_session_agent(const std::string& session,std::optional<AgentDefinitionMetadata> selected,std::int64_t expected_revision)override;
    WorkspaceSkillCatalogue workspace_skills()const override;
    WorkspaceSessionSkills session_skills(const std::string& session)const override;
    WorkspaceSessionSkills replace_session_skills(const std::string& session,std::vector<std::string> ids,std::int64_t revision,WorkspaceAdmission expected)override;
    Run submit_workspace(std::string id,std::string session,std::string prompt,std::string model,
        WorkspaceAdmission expected,std::optional<ProviderProfileAdmission> profile={})override;
    Run submit_graph_workspace(std::string id,std::string session,std::string graph,std::int64_t revision,
        std::string prompt,std::string model,WorkspaceAdmission expected,std::optional<ProviderProfileAdmission> profile={})override;
    bool supports_profile_admission()const override{return true;}
    bool supports_delegation()const override;
    bool supports_dynamic_planning()const override;
    bool supports_context()const override;
    ContextControlSnapshot context_status(const std::string& session,const std::string& model={})const override;
    ContextManualStatus context_request(const std::string& session,const std::string& request,const std::string& model={})const override;
    ContextManualStatus request_context(const std::string& session,const std::string& request,
        const std::string& actor,std::int64_t expected_head_revision,const std::string& model={})override;
    ContextManualStatus request_context_profile(const std::string& session,const std::string& request,
        const std::string& actor,std::int64_t expected_head_revision,const std::string& model,ProviderProfileAdmission expected)override;
    Run plan_input(const std::string& root,const std::string& request,std::string input_json,
        const std::string& actor,std::int64_t revision,std::int64_t state_sequence) override;
    Run resume_plan(const std::string& root,const std::string& actor,
        std::int64_t revision,std::int64_t state_sequence) override;
    bool supports_graph_profile_admission()const override{return true;}
    Run submit_profile(std::string id,std::string session,std::string prompt,std::string model,ProviderProfileAdmission expected)override;
    Run submit_graph_profile(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model,ProviderProfileAdmission expected)override;
    void cancel(const std::string& id) override;
    bool healthy() const override;
    bool idle() const override;
    bool supports_file_edit_proposals()const override;
    bool available() const override;
    std::vector<GraphExecutionMetadata> graphs() const override;
    Run submit_graph(std::string id,std::string session,std::string graph,std::int64_t revision,
        std::string prompt,std::string model={}) override;
    GraphRootRecord human_input(const std::string& root,const std::string& node,
        const std::string& input,const std::string& actor,std::int64_t revision) override;
    Run resume_graph(const std::string& root,const std::string& actor,std::int64_t revision)override;
    GraphContextMetadata graph_context(const std::string& root)const override;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
