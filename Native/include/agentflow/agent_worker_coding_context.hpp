#pragma once
#include "agentflow/agent_worker_read_context.hpp"
#include "agentflow/agent_worker_file_rpc.hpp"
#include "agentflow/agent_worker_process_rpc.hpp"
#include "agentflow/process_executor.hpp"

namespace agentflow {
// Backend-only composition. File operations share the guidance window actually
// admitted to the model and the same serialized native workspace ownership.
class AgentWorkerCodingContext final:public AgentReadToolHost {
public:
    AgentWorkerCodingContext(PersistenceService&,AgentWorkerLeaseSpec,AgentReadToolOwnerContext,
        AgentWorkerReadRequestPolicy,AgentWorkerChannel::Deadline,std::stop_token={},std::vector<ProcessProfile> profiles={});
    ModelRequest prepare(std::stop_token={});
    void admit(const std::string&,const ModelRequest&,std::stop_token={});
    AgentReadToolResult invoke(const AgentWorkspaceInvocation&,std::stop_token={})override;
    AgentWorkerRpcHandler bind_files(const AgentWorkerScope&);
    AgentWorkerRpcHandler bind_processes(const AgentWorkerScope&);
    AgentWorkerFileHost& files()noexcept{return *files_;}
private:
    AgentWorkerReadContext reads_;
    std::vector<ModelToolDefinition> tools_;
    std::unique_ptr<AgentWorkerFileHost> files_;
    std::unique_ptr<ProcessExecutor> process_;
    std::unique_ptr<AgentWorkerProcessHost> processes_;
};
}
