#pragma once
#include "agentflow/a2a_agent_registry.hpp"
#include "agentflow/a2a_client.hpp"
#include "agentflow/instruction_precondition.hpp"
#include "agentflow/model_provider.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/workspace_tools.hpp"
#include <chrono>
#include <map>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace agentflow {
struct A2aEffectUncertain : std::runtime_error {using std::runtime_error::runtime_error;};
struct A2aOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
struct A2aEffectNotDispatched : std::runtime_error {using std::runtime_error::runtime_error;};

// Per-run view of owner-registered peers. Discovery yields model-visible tools,
// but every send still passes through the local approval and operation ledger.
class A2aAgentToolRegistry {
public:
    A2aAgentToolRegistry(A2aRemoteAgentRegistry& registrations,PersistenceService& persistence,
        WorkspaceTools& workspace,std::chrono::milliseconds timeout,std::stop_token cancel={});
    ~A2aAgentToolRegistry();
    A2aAgentToolRegistry(const A2aAgentToolRegistry&)=delete;
    A2aAgentToolRegistry& operator=(const A2aAgentToolRegistry&)=delete;
    std::vector<ModelToolDefinition> definitions()const;
    std::string approval_bindings_json()const;
    std::vector<std::string> unavailable_agent_ids()const;
    std::string invoke(const std::string& operation_id,const std::string& run_id,const std::string& alias,
        const std::string& arguments,std::int64_t approval_expiry,std::chrono::steady_clock::time_point deadline,
        std::stop_token cancel={},InstructionPrecondition guidance={});
private:
    struct Entry;
    A2aRemoteAgentRegistry& registrations_;
    PersistenceService& persistence_;
    WorkspaceTools& workspace_;
    std::map<std::string,std::unique_ptr<Entry>> entries_;
    std::vector<std::string> unavailable_;
};
}
