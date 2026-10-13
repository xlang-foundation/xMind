#pragma once
#include "agentflow/model_provider.hpp"
namespace agentflow {
// Verify one model-bound DeepSeek reasoning receipt against its assistant DTO.
void validate_deepseek_history_message(const ModelMessage&,const std::string& model);
// Explicit native DeepSeek Chat Completions policy. Reuses the native transport
// and SSE parser, but retains reasoning receipts and its documented wire fields.
std::string serialize_deepseek_request(const ChatProviderConfig&,const ModelRequest&);
}
