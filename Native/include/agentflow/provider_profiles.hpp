#pragma once
#include "agentflow/model_provider.hpp"
#include "agentflow/persistence_service.hpp"
namespace agentflow {
// Backend policy; clients select route identities, never arbitrary destinations.
struct ProviderProfileRoute {
    std::string id,provider,endpoint,credential_scope,credential_purpose;
    ProviderWire wire=ProviderWire::chat_completions;
};
struct SavedProviderProfile {
    std::string id,route_id,model,credential_id;
    std::int64_t revision=0;
};
struct ProviderProfileSnapshot {
    std::int64_t revision=0;
    std::string active;
    std::vector<SavedProviderProfile> profiles;
};
// Backend-owned encrypted profile registry. Never serialize credential references
// directly as client metadata. Does not publish or replace an execution service.
// The owner must validate candidate execution policy before activating a profile.
class ProviderProfiles {
public:
    ProviderProfiles(PersistenceService& store,std::vector<ProviderProfileRoute> policy);
    ProviderProfileSnapshot snapshot() const;
    ProviderProfileSnapshot save(std::string id,std::string route,std::string model,SecretBytes key,
        std::int64_t expected_revision,bool activate=false);
    ProviderProfileSnapshot select(std::string id,std::int64_t expected_revision);
    SecretBytes credential(const std::string& id) const;
private:
    struct State;
    State load() const;
    const ProviderProfileRoute& route(const std::string& id) const;
    PersistenceService& store_;
    std::vector<ProviderProfileRoute> policy_;
};
}
