#pragma once
#include "agentflow/model_provider.hpp"
namespace agentflow {
// Owned native DTOs, not provider configuration or credential envelopes.
// Opaque provider strings/canonical windows retain exact supplied bytes.
std::string encode_agent_model_request(const ModelRequest&);
ModelRequest decode_agent_model_request(const std::string&);
std::string encode_agent_model_completion(const ModelCompletion&);
ModelCompletion decode_agent_model_completion(const std::string&);
std::string encode_agent_model_event(const ModelEvent&);
ModelEvent decode_agent_model_event(const std::string&);
}
