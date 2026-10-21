#pragma once
#include "agentflow/agent_worker_read_context.hpp"
#include "agentflow/root_execution_budget.hpp"
#include "agentflow/agent_worker_channel.hpp"
#include <functional>

namespace agentflow {
// Backend-only context authority. No policy, credential, capacity or context
// journal handle is reconstructed from values supplied by a child.
class AgentWorkerContextWindow final {
public:
    using Preview=std::function<ModelRequest(std::stop_token)>;
    using Admit=std::function<void(const std::string&,const ModelRequest&,std::stop_token)>;
    AgentWorkerContextWindow(PersistenceService&,AgentWorkerLeaseSpec,ChatProviderConfig,
        AgentWorkerContextPolicy,std::shared_ptr<AgentModelGateway>,Preview,Admit,
        AgentWorkerChannel::Deadline,std::stop_token);
    ModelRequest prepare(std::stop_token);
    void admit(const std::string&,const ModelRequest&,std::stop_token);
    bool reject(const std::string&,const ModelRequest&,std::int64_t,std::stop_token);
    ModelRequest rebuild(const std::string&,const ModelRequest&,std::stop_token);
    ModelCallReservation reserve(const std::string&,std::stop_token);
    ModelCallReservation start(const std::string&,std::stop_token);
    ModelCallReservation finish(const std::string&,std::stop_token);
private:
    void check(std::stop_token);
    PersistenceService& store_;
    AgentWorkerLeaseSpec lease_;
    ChatProviderConfig provider_;
    AgentWorkerContextPolicy policy_;
    std::shared_ptr<AgentModelGateway> gateway_;
    Preview preview_;
    Admit admit_;
    AgentWorkerChannel::Deadline deadline_;
    std::stop_token owner_cancel_;
    RootExecutionBudget budget_;
    ContextManager manager_;
    ContextOwner owner_;
    std::optional<PreparedModelContext> prepared_;
    ModelRequest raw_;
    bool handed_out_=false,rejected_=false,rebuild_used_=false;
    std::mutex mutex_;
};
}
