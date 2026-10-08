#pragma once
#include "agentflow/gemini_request.hpp"
#include "agentflow/gemini_stream.hpp"

namespace agentflow {
struct GeminiProviderConfig {
    // Backend-owned versioned base, e.g. https://generativelanguage.googleapis.com/v1beta.
    // No query credentials; model is bound into the generated resource path.
    std::string endpoint_base,model;
    std::chrono::milliseconds deadline{120000},idle_timeout{60000};
};
std::string gemini_stream_endpoint(const GeminiProviderConfig& config);
// Native request/HTTP/SSE boundary. Returns wire-specific history and usage.
// Agent history adaptation/enrollment remains separate; never executes a tool.
GeminiCompletion complete_gemini(const GeminiProviderConfig& config,const GeminiRequest& request,
    const SecretBytes* api_key,ChatCompletionStream::Sink sink,std::stop_token cancel={});
}
