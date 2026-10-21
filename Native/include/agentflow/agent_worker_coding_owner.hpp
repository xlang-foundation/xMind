#pragma once
#include "agentflow/agent_worker_read_owner.hpp"
#include "agentflow/process_executor.hpp"

namespace agentflow {
struct NativeWorkerModelAccess;
class AgentWorkerCodingOwner final {
public:
    AgentWorkerCodingOwner(PersistenceService&,AgentWorkerLeaseSpec,const AgentWorkerScope&,
        AgentReadToolOwnerContext,AgentWorkerReadRequestPolicy,ChatProviderConfig,std::string,
        std::shared_ptr<AgentModelGateway>,AgentWorkerChannel::Deadline,std::stop_token={},std::shared_ptr<AgentProcessToolHost> process_owner={},std::optional<AgentRemoteToolOwnerContext> remote_owner={},std::optional<AgentCallableToolOwnerContext> callable_owner={});
    ~AgentWorkerCodingOwner();
    NativeWorkerModelAccess native_model_access()const;
    std::string dispatch(const std::string&,const std::string&,AgentWorkerRpcSink,std::stop_token={});
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
