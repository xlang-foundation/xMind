#pragma once
#include "agentflow/agent_worker_read_context.hpp"
#include "agentflow/agent_worker_file_rpc.hpp"
#include "agentflow/agent_worker_process_rpc.hpp"
#include "agentflow/agent_worker_remote_rpc.hpp"
#include "agentflow/agent_worker_callable_rpc.hpp"
#include "agentflow/process_executor.hpp"
#include <map>
#include <set>

namespace agentflow {
// Backend-only composition. File operations share the guidance window actually
// admitted to the model and the same serialized native workspace ownership.
class AgentWorkerCodingContext final:public AgentReadToolHost {
public:
    AgentWorkerCodingContext(PersistenceService&,AgentWorkerLeaseSpec,AgentReadToolOwnerContext,
        AgentWorkerReadRequestPolicy,AgentWorkerChannel::Deadline,std::stop_token={},std::shared_ptr<AgentProcessToolHost> process_owner={},std::optional<AgentRemoteToolOwnerContext> remote_owner={},std::optional<AgentCallableToolOwnerContext> callable_owner={});
    ModelRequest prepare(std::stop_token={});
    void admit(const std::string&,const ModelRequest&,std::stop_token={});
    AgentReadToolResult invoke(const AgentWorkspaceInvocation&,std::stop_token={})override;
    AgentWorkerRpcHandler bind_files(const AgentWorkerScope&);
    AgentWorkerRpcHandler bind_processes(const AgentWorkerScope&);
    AgentWorkerRpcHandler bind_callables(const AgentWorkerScope&);
    AgentWorkerRpcHandler bind_remotes(const AgentWorkerScope&);
    AgentWorkerFileHost& files()noexcept{return *files_;}
private:
    AgentWorkerReadContext reads_;
    std::vector<ModelToolDefinition> tools_;
    std::unique_ptr<AgentWorkerFileHost> files_;
    std::shared_ptr<AgentProcessToolHost> process_;
    std::unique_ptr<AgentWorkerProcessHost> processes_;
    std::unique_ptr<AgentWorkerRemoteHost> remotes_;
    std::unique_ptr<AgentWorkerCallableHost> callables_;
    std::set<std::string> callable_names_;
    std::function<std::vector<ModelToolDefinition>(std::stop_token)> refresh_catalogue_;
    bool remote_catalogue_invalidated_=false;
    std::vector<std::string> joined_process_notes_;
    std::map<std::string,bool> joined_attempts_;
    bool join_outcome_unrecorded_=false;
};
}
