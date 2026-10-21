#pragma once
#include "agentflow/agent_model_gateway.hpp"
#include "agentflow/agent_worker_records.hpp"
#include "agentflow/agent_worker_model_admission.hpp"
#include <mutex>
#include <functional>

namespace agentflow {
struct AgentWorkerModelOutcomeUnrecorded:std::runtime_error {using std::runtime_error::runtime_error;};
struct AgentWorkerModelPolicy {
    bool include_usage=false;
    std::optional<std::int64_t> max_output_tokens;
    std::string assistant_provider_context_json;
    // Backend-only pair. Preview does not mark guidance delivered. Admission
    // runs after the durable model claim and before actual gateway dispatch.
    std::function<ModelRequest(std::stop_token)> request_context;
    std::function<void(const std::string&,const ModelRequest&,std::stop_token)> admit_context;
    std::function<bool(const std::string&,const ModelRequest&,std::int64_t,std::stop_token)> reject_context;
    std::function<ModelRequest(const std::string&,const ModelRequest&,std::stop_token)> rebuild_context;
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
    ModelRequest prepare_request(std::stop_token={});
    ModelRequest rebuild_request(const std::string&,const ModelRequest&,std::stop_token={});
    AgentWorkerModelAdmission admission(std::stop_token={});
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
