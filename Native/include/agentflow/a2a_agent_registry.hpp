#pragma once
#include "agentflow/persistence_service.hpp"
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace agentflow {
struct A2aRemoteAgentDescriptor {
    std::string id,card_url;
    std::int64_t revision=0;
    bool enabled=false,has_bearer=false;
};
struct A2aResolvedRemoteAgent {
    A2aRemoteAgentDescriptor descriptor;
    std::optional<SecretBytes> bearer;
};
struct A2aRemoteAgentCatalog {
    std::int64_t revision=0;
    std::vector<A2aRemoteAgentDescriptor> entries;
};

// Owner-managed remote A2A registrations. Endpoints and IDs are public
// metadata; bearer values live only in the encrypted credential repository.
class A2aRemoteAgentRegistry {
public:
    explicit A2aRemoteAgentRegistry(PersistenceService& persistence):persistence_(persistence){}
    A2aRemoteAgentCatalog list();
    A2aRemoteAgentDescriptor register_agent(std::string id,std::string card_url,bool enabled,
        std::int64_t expected_agent_revision,std::optional<SecretBytes> bearer={},bool clear_bearer=false);
    A2aRemoteAgentDescriptor set_enabled(const std::string& id,bool enabled,std::int64_t expected_agent_revision);
    void remove(const std::string& id,std::int64_t expected_agent_revision);
    A2aResolvedRemoteAgent resolve(const std::string& id);
private:
    struct StoredAgent;
    struct Snapshot;
    Snapshot snapshot();
    A2aRemoteAgentDescriptor publish(Snapshot&,std::vector<StoredAgent>,std::vector<std::string>,A2aRemoteAgentDescriptor);
    PersistenceService& persistence_;
    std::mutex mutex_;
};
}
