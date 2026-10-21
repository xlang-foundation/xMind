#pragma once
#include "agentflow/agent_worker_read_owner.hpp"
#include "agentflow/process_executor.hpp"

namespace agentflow {
class AgentWorkerCodingOwner final {
public:
    AgentWorkerCodingOwner(PersistenceService&,AgentWorkerLeaseSpec,const AgentWorkerScope&,
        AgentReadToolOwnerContext,AgentWorkerReadRequestPolicy,ChatProviderConfig,std::string,
        std::shared_ptr<AgentModelGateway>,AgentWorkerChannel::Deadline,std::stop_token={},std::vector<ProcessProfile> profiles={});
    ~AgentWorkerCodingOwner();
    std::string dispatch(const std::string&,const std::string&,AgentWorkerRpcSink,std::stop_token={});
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
