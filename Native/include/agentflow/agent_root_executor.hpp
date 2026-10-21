#pragma once
#include "agentflow/agent_definition_records.hpp"
#include "agentflow/records.hpp"
#include <stop_token>
#include <memory>

namespace agentflow {
struct WorkspaceMembershipSnapshot;
struct AgentRootExecutionRequest {
    std::string run_id,model_id,agent_instructions;
    std::optional<std::vector<std::string>> agent_tools;
    std::vector<AgentPermissionRule> agent_permissions;
    std::shared_ptr<const WorkspaceMembershipSnapshot> workspace;
    std::string placement;
};
// Trusted backend dispatch port, not a client/plugin callback. Settings and
// capabilities are captured by its backend factory. Validation runs before new
// durable admission. execute must retain physical ownership until actual drain;
// it may return/throw only after drain or before launching any child. Observation
// delay never permits returning while a child still holds physical capacity.
class AgentRootExecutor {
public:
    virtual ~AgentRootExecutor()=default;
    virtual void validate(const AgentRootExecutionRequest&)const=0;
    virtual Run execute(const AgentRootExecutionRequest&,std::stop_token)=0;
};
}
