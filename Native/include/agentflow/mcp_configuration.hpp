#pragma once
#include "agentflow/persistence_service.hpp"

namespace agentflow {
struct McpEnvironmentCredential {std::string name,scope,id;};
struct McpServerSetting {
    std::string id,executable,working_directory;
    std::int64_t revision=0;
    bool enabled=true;
    std::vector<std::string> arguments;
    std::vector<McpEnvironmentCredential> credentials;
};
// Startup/admin-owned configuration. Apply before creating runtime workers;
// immutable settings snapshots are used for the lifetime of that service.
// File/view metadata contains references, never credential values. Retired IDs
// cannot be reused. Dynamic/team administration requires separate scope/CAS.
class McpConfigurationStore {
public:
    explicit McpConfigurationStore(PersistenceService& store):store_(store) {}
    std::vector<McpServerSetting> load();
    std::vector<McpServerSetting> apply(const std::string& desired_json);
private:
    PersistenceService& store_;
};
// Connector-owned purpose binds an encrypted credential to this exact command,
// configuration identity and environment name; a command change needs a key
// provisioned for the new purpose. This is never supplied by a view/model.
std::string mcp_credential_purpose(const McpServerSetting& setting,const std::string& name);
}
