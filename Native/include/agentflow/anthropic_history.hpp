#pragma once
#include "agentflow/model_provider.hpp"

namespace agentflow {
// Keep ordered provider blocks as a JSON string inside the durable receipt,
// preserving numeric/escaped tool input tokens through agent JSON round trips.
std::string anthropic_content_receipt(std::string content_json,
    std::string native_finish,std::string provider_finish);
// Verify an assistant receipt against visible text and ordered native calls.
// Signatures/redacted data stay opaque; this performs no cryptographic check.
// Foreign, empty or incompatible outgoing history fails before transport.
std::string anthropic_history_content(const ModelMessage& message);
}
