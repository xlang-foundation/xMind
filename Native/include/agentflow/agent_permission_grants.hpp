#pragma once
#include "agentflow/agent_definition_records.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/operation.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agentflow {
struct AgentPermissionAskContext {
    std::string operation_id,session_id,workspace_id,policy_binding,action,resource,grant_id;
    bool operator==(const AgentPermissionAskContext&) const=default;
};
struct AgentPermissionPendingRequest {Operation operation;std::optional<AgentPermissionAskContext> context;};
struct AgentPermissionGrant {
    std::string id,workspace_id,policy_binding,action,resource;
    std::int64_t created_unix_ms=0;
};
struct AgentPermissionGrantSnapshot {
    std::int64_t revision=0;
    std::vector<AgentPermissionGrant> grants;
};

std::string agent_permission_policy_binding(
    const std::vector<std::vector<AgentPermissionRule>>& inherited,
    const std::vector<AgentPermissionRule>& selected);
std::string agent_permission_grant_identifier(const std::string& workspace_id,
    const std::string& policy_binding,const std::string& action,const std::string& resource);

// Reusable grants are local-session decisions for one workspace, one exact
// agent-policy snapshot, one tool and one normalized resource. They never
// expand native workspace membership or the backend tool allowlist.
class AgentPermissionGrantStore {
public:
    explicit AgentPermissionGrantStore(PersistenceService& persistence):persistence_(persistence){}
    std::vector<AgentPermissionPendingRequest> pending(const std::string& session_id);
    void record_request(const AgentPermissionAskContext& request);
    std::optional<AgentPermissionAskContext> request(const std::string& operation_id);
    bool allows(const std::string& session_id,const std::string& workspace_id,
        const std::string& policy_binding,const std::string& action,const std::string& resource);
    AgentPermissionGrantSnapshot list(const std::string& session_id);
    AgentPermissionGrantSnapshot grant(const std::string& session_id,const std::string& operation_id);
    AgentPermissionGrantSnapshot revoke(const std::string& session_id,const std::string& grant_id,
        std::int64_t expected_revision);
private:
    PersistenceService& persistence_;
};
}
