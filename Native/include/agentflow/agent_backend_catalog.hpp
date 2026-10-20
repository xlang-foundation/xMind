#pragma once
#include "agentflow/agent_authority.hpp"
#include "agentflow/agent_definition_records.hpp"
#include <memory>

namespace agentflow {
struct AgentSettings;
class PersistenceService;
// Trusted owner-side catalogue port. It carries configured credential versions
// and agent definitions, never credential material or a persistence handle.
// A future worker adapter must bind queries to its captured admission scope.
class AgentBackendCatalog {
public:
    virtual ~AgentBackendCatalog()=default;
    virtual std::vector<AgentAuthorityCredentialVersion> credential_versions(const AgentSettings&)=0;
    virtual AgentDefinitionCatalog agent_definitions()=0;
};
std::shared_ptr<AgentBackendCatalog> make_persistence_agent_backend_catalog(PersistenceService&);
}
