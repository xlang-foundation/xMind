#pragma once
#include "agentflow/process_executor.hpp"
namespace agentflow {
// Trusted startup/offline administrator registry. Database I/O uses embedded
// xlang3 through PersistenceService. Views/models cannot change profiles.
// Revisions and executable bindings are assigned by the backend. Removed IDs
// are permanently retired so unresolved profile resources cannot be reused.
class ProcessConfigurationStore {
public:
    explicit ProcessConfigurationStore(PersistenceService& store):store_(store) {}
    std::vector<ProcessProfile> load();
    std::vector<ProcessProfile> apply(const std::string& desired_json);
private:
    PersistenceService& store_;
};
}
