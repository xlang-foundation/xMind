#pragma once
#include "agentflow/agent_model_gateway.hpp"
#include "agentflow/agent_worker_records.hpp"
#include <mutex>

namespace agentflow {
struct AgentWorkerModelOutcomeUnrecorded:std::runtime_error {using std::runtime_error::runtime_error;};
struct AgentWorkerModelPolicy {
    bool include_usage=false;
    std::optional<std::int64_t> max_output_tokens;
    std::string assistant_provider_context_json;
};
// Backend-only model authority. The child can supply an owned request and exact
// funded attempt, never provider configuration, credentials or a replacement
// tool catalogue. Streaming/response RPC encoding is a separate adapter.
class AgentWorkerModelHost {
public:
    AgentWorkerModelHost(PersistenceService&,AgentWorkerLeaseSpec,ChatProviderConfig,
        std::vector<ModelToolDefinition> admitted_tools,AgentWorkerModelPolicy,std::shared_ptr<AgentModelGateway>,
        std::chrono::steady_clock::time_point root_deadline,std::stop_token owner_cancel={});
    ModelCompletion complete(const std::string& attempt,const ModelRequest&,ChatCompletionStream::Sink,std::stop_token={});
    const AgentWorkerLeaseSpec& lease()const noexcept{return lease_;}
private:
    PersistenceService& store_;
    AgentWorkerLeaseSpec lease_;
    ChatProviderConfig provider_;
    std::vector<ModelToolDefinition> tools_;
    AgentWorkerModelPolicy policy_;
    std::shared_ptr<AgentModelGateway> gateway_;
    std::chrono::steady_clock::time_point deadline_;
    std::stop_token owner_cancel_;
    std::mutex invocation_;
};
}
