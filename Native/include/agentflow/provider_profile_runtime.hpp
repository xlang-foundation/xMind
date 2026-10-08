#pragma once
#include "agentflow/execution_platform.hpp"
#include "agentflow/provider_profiles.hpp"
#include "agentflow/provider_catalogue.hpp"
#include "agentflow/provider_profile_setup.hpp"
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
    Run submit(std::string id,std::string session,std::string prompt) override;
    Run submit_model(std::string id,std::string session,std::string prompt,std::string model) override;
    Run submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity) override;
    std::vector<std::string> models() const override;
    bool supports_profile_admission()const override{return true;}
    bool supports_graph_profile_admission()const override{return true;}
    Run submit_profile(std::string id,std::string session,std::string prompt,std::string model,ProviderProfileAdmission expected)override;
    Run submit_graph_profile(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model,ProviderProfileAdmission expected)override;
    void cancel(const std::string& id) override;
    bool healthy() const override;
    bool available() const override;
    std::vector<GraphExecutionMetadata> graphs() const override;
    Run submit_graph(std::string id,std::string session,std::string graph,std::int64_t revision,
        std::string prompt,std::string model={}) override;
    GraphRootRecord human_input(const std::string& root,const std::string& node,
        const std::string& input,const std::string& actor,std::int64_t revision) override;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
