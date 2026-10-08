#pragma once
#include "agentflow/model_provider.hpp"
#include "agentflow/persistence_service.hpp"
#include <functional>
namespace agentflow {
// Backend policy; clients select route identities, never arbitrary destinations.
struct ProviderProfileRoute {
    std::string id,provider,endpoint,credential_scope,credential_purpose;
    ProviderWire wire=ProviderWire::chat_completions;
};
struct SavedProviderProfile {
    // Empty model is a backend-imported key-only profile awaiting selection.
    // Public save operations still require a nonempty model identity.
    std::string id,route_id,model,credential_id;
    std::int64_t revision=0;
};
struct ProviderProfileSnapshot {
    std::int64_t revision=0;
    std::string active;
    std::vector<SavedProviderProfile> profiles;
};
// Backend-only configuration input. Plaintext is move-only and never becomes
// metadata. An absent model preserves an existing choice; a new profile may
// honestly have no selected model. Empty key reuses an existing encrypted key.
struct ProviderProfileConfigEntry {
    std::string id,route_id;
    std::optional<std::string> model;
    SecretBytes key{std::span<const std::uint8_t>{}};
};
// Backend-owned encrypted profile registry. Never serialize credential references
// directly as client metadata. Does not publish or replace an execution service.
// The owner must validate candidate execution policy before activating a profile.
class ProviderProfiles {
public:
    // Backend-only prepublication hook. Build/validate a candidate service here;
    // publish it only after this operation returns successfully. A callback or
    // CAS failure must discard the candidate and retain the previous service.
    using Validator=std::function<void(const SavedProviderProfile&,const ProviderProfileRoute&)>;
    ProviderProfiles(PersistenceService& store,std::vector<ProviderProfileRoute> policy);
    ProviderProfileSnapshot snapshot() const;
    ProviderProfileSnapshot save(std::string id,std::string route,std::string model,SecretBytes key,
        std::int64_t expected_revision,bool activate=false,Validator validate={});
    // Atomic backend YAML/configuration batch. Unspecified profiles remain.
    // Native static validation covers each supplied candidate; preparation of
    // the final active execution service happens once, before the single CAS.
    // Identical configuration verifies that CAS without rotating revisions/keys.
    // An empty batch requires an explicit existing active-profile selection.
    ProviderProfileSnapshot save_config(std::vector<ProviderProfileConfigEntry> entries,
        std::optional<std::string> active_profile,std::int64_t expected_revision,
        Validator validate_each={},Validator prepare_active={});
    ProviderProfileSnapshot select(std::string id,std::int64_t expected_revision,Validator validate={});
    // Migration only: retain an existing encrypted reference and configuration
    // revision. Backend policy/ownership must match; an existing registry wins.
    // Does not rewrite the legacy configuration or rotate its credential.
    ProviderProfileSnapshot import_existing(std::string id,std::string route,std::string model,
        std::string credential_id,std::int64_t initial_revision,Validator validate={});
    SecretBytes credential(const std::string& id) const;
private:
    struct State;
    State load() const;
    const ProviderProfileRoute& route(const std::string& id) const;
    PersistenceService& store_;
    std::vector<ProviderProfileRoute> policy_;
};
}
