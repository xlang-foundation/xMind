#pragma once
#include "agentflow/execution_platform.hpp"
#include "agentflow/provider_profiles.hpp"
#include "agentflow/provider_catalogue.hpp"
namespace agentflow {
// Native backend policy. Capabilities and credential destinations never come
// from a view; the selected model identity is supplied by its saved profile.
struct ProviderProfileExecutionPolicy {
    ProviderProfileRoute route;
    ChatProviderConfig provider;
    std::optional<ProviderCataloguePolicy> catalogue;
};
struct ProviderProfileMetadata {
    std::string id,route_id,provider,model;
    std::int64_t revision=0;
};
struct ProviderProfileRuntimeMetadata {
    std::int64_t revision=0;
    std::string active;
    std::vector<ProviderProfileMetadata> profiles;
};
// Shared single/graph execution platform with backend-owned model profiles.
// Connection profiles (Local/Nexus) belong to a separate transport boundary.
class ProviderProfileRuntime final : public RunExecutor,public GraphExecution {
public:
    ProviderProfileRuntime(PersistenceService& store,AgentSettings base,
        std::vector<ProviderProfileExecutionPolicy> policy,std::size_t workers=2,std::size_t capacity=128);
    ~ProviderProfileRuntime();
    ProviderProfileRuntimeMetadata configuration() const;
    std::vector<std::string> discover_models(std::string id,std::string route,SecretBytes key,
        std::int64_t expected_revision,std::stop_token cancel={});
    ProviderProfileRuntimeMetadata save_profile(std::string id,std::string route,std::string model,
        SecretBytes key,std::int64_t expected_revision,bool activate=false);
    ProviderProfileRuntimeMetadata select_profile(std::string id,std::int64_t expected_revision);
    // Trusted migration caller must validate the legacy source record against
    // backend policy. This preserves its owned encrypted reference and revision.
    ProviderProfileRuntimeMetadata import_existing_profile(std::string id,std::string route,std::string model,
        std::string credential_id,std::int64_t initial_revision);
    Run submit(std::string id,std::string session,std::string prompt) override;
    Run submit_model(std::string id,std::string session,std::string prompt,std::string model) override;
    Run submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity) override;
    std::vector<std::string> models() const override;
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
