#pragma once
#include "agentflow/model_provider.hpp"
namespace agentflow {
// Public model controls captured by the backend owner. No destination, secret,
// private deadline, instruction text, callback or tool grant is carried here.
struct AgentWorkerModelAdmission {
    std::string model_id;
    ProviderWire wire=ProviderWire::chat_completions;
    Capability tools=Capability::unknown,stream_usage=Capability::unknown,output_limit=Capability::unknown,reasoning=Capability::unknown;
    std::optional<ReasoningEffort> reasoning_effort;
    ChatDialect chat_dialect=ChatDialect::openai;
    bool include_usage=false;
    std::optional<std::int64_t> max_output_tokens;
    bool owned_request_window=false;
};
std::string encode_agent_worker_model_admission(const AgentWorkerModelAdmission&);
AgentWorkerModelAdmission decode_agent_worker_model_admission(const std::string&);
// A local proxy config, not the backend provider config. Request usage follows
// captured policy; endpoint is always worker://captured. Backend deadlines stay
// authoritative and are never reconstructed from this value.
ChatProviderConfig agent_worker_model_config(const AgentWorkerModelAdmission&);
}
