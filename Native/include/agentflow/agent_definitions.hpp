#pragma once
#include "agentflow/agent_definition_records.hpp"
#include "agentflow/persistence_service.hpp"

namespace agentflow {

// Reusable named agent behavior. This definition selects prompt guidance and
// an optional model only; it does not grant tools, workspace access or effects.
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
