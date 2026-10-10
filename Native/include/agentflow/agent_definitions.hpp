#pragma once
#include "agentflow/persistence_service.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace agentflow {

// Reusable named agent behavior. This definition selects prompt guidance and
// an optional model only; it does not grant tools, workspace access or effects.
struct AgentDefinition {
    std::string id;
    std::int64_t revision = 0;
    std::string model_id;
    std::string instructions;
};

struct AgentDefinitionCatalog {
    std::int64_t revision = 0;
    std::vector<AgentDefinition> entries;
    std::vector<std::string> retired_ids;
};

// Trusted, offline catalog import. Persistence is owned by embedded xlang3.
class AgentDefinitionStore {
public:
    explicit AgentDefinitionStore(PersistenceService& store) : store_(store) {}
    AgentDefinitionCatalog load();
    AgentDefinitionCatalog apply(const std::string& trusted_source);
private:
    PersistenceService& store_;
};

}
