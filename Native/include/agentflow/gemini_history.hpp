#pragma once
#include "agentflow/gemini_request.hpp"
#include "agentflow/gemini_stream.hpp"

namespace agentflow {
// Adapt validated GenerateContent history to the native agent DTO. Native call
// identities come from the backend factory; optional provider IDs stay separate.
// The receipt keeps the original parts array as a string so persistence cannot
// round argument/metadata numbers while parsing and dumping provider_items.
ModelCompletion gemini_model_completion(const GeminiCompletion& completion,
    const std::function<std::string()>& internal_id_factory);
// Normalize only supplied provider counts for native completion/event consumers.
// Missing counts remain absent; no tokens are estimated or summed.
std::string gemini_model_usage(const std::string& usage_json);
// Replay native conversation history through its checked Gemini receipts.
// Tool outputs remain ordinary strings inside functionResponse.response objects.
GeminiRequest gemini_model_request(const ModelRequest& request,Capability function_calls);
}
