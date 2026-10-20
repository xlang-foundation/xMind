#include "agentflow/agent_backend_catalog.hpp"
#include "agentflow/agent_definitions.hpp"

namespace agentflow {
namespace {
class PersistenceAgentBackendCatalog final:public AgentBackendCatalog {
public:
    explicit PersistenceAgentBackendCatalog(PersistenceService& store):store_(store){}
    std::vector<AgentAuthorityCredentialVersion> credential_versions(const AgentSettings& settings)override{
        return agent_authority_credentials(store_,settings);
    }
    AgentDefinitionCatalog agent_definitions()override{return AgentDefinitionStore(store_).load();}
private:
    PersistenceService& store_;
};
}
std::shared_ptr<AgentBackendCatalog> make_persistence_agent_backend_catalog(PersistenceService& store){
    return std::make_shared<PersistenceAgentBackendCatalog>(store);
}
}
