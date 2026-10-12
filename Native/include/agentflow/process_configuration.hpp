#pragma once
#include "agentflow/process_executor.hpp"
#include <functional>
namespace agentflow {
// Trusted startup/offline administrator registry. Database I/O uses embedded
// xlang3 through PersistenceService. Views/models cannot change profiles.
// Revisions and executable bindings are assigned by the backend. Removed IDs
// are permanently retired so unresolved profile resources cannot be reused.
class ProcessConfigurationStore {
public:
    explicit ProcessConfigurationStore(PersistenceService& store):store_(store) {}
    std::vector<ProcessProfile> load();
    // Bootstrap a built-in profile set only when this installation has never
    // stored a process registry. An explicitly stored empty registry stays empty.
    std::vector<ProcessProfile> load_or_initialize(const std::function<std::string()>& defaults);
    std::vector<ProcessProfile> apply(const std::string& desired_json,bool replacement_bootstrap=false);
private:
    PersistenceService& store_;
};
}
