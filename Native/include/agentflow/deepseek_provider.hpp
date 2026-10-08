#pragma once
#include "agentflow/model_provider.hpp"
namespace agentflow {
// Explicit native DeepSeek Chat Completions policy. Reuses the native transport
// and SSE parser, but retains reasoning receipts and its documented wire fields.
std::string serialize_deepseek_request(const ChatProviderConfig&,const ModelRequest&);
}
