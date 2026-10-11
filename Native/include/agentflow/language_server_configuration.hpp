#pragma once
#include "agentflow/language_server.hpp"
#include "agentflow/persistence_service.hpp"

namespace agentflow {
// Trusted startup/offline administrator registry. Executables and environments
// never come from model arguments; revisions and file identities are backend-owned.
class LanguageServerConfigurationStore {
public:
    explicit LanguageServerConfigurationStore(PersistenceService& store):store_(store){}
    std::vector<LanguageServerProfile> load();
    std::vector<LanguageServerProfile> apply(const std::string& source);
private:
    PersistenceService& store_;
};
}
