#pragma once
#include "agentflow/model_stream.hpp"
#include "agentflow/http_stream_transport.hpp"
#include <optional>

namespace agentflow {
enum class Capability {unknown,unsupported,supported};
enum class MessageRole {system,developer,user,assistant,tool};
struct ModelMessage {
    MessageRole role;
    std::string content;
    std::vector<ModelToolCall> tool_calls;
    std::string tool_call_id;
};
struct ModelToolDefinition {std::string name,description,input_schema_json;};
struct ModelRequest {
    std::vector<ModelMessage> messages;
    std::vector<ModelToolDefinition> tools;
    bool include_usage=false;
    std::optional<std::int64_t> max_output_tokens;
};
struct ChatProviderConfig {
    std::string endpoint,model; // Explicit full endpoint and deployment/model ID.
    Capability tools=Capability::unknown,stream_usage=Capability::unknown,output_limit=Capability::unknown;
    std::chrono::milliseconds deadline{120000},idle_timeout{60000};
};
// Pure native request serialization; no model/provider availability is inferred.
std::string serialize_chat_request(const ChatProviderConfig& config,const ModelRequest& request);
// Transport-backed provider adapter. Credentials remain caller/backend owned.
// Available on Windows until other native transport implementations are added.
ModelCompletion complete_chat(const ChatProviderConfig& config,const ModelRequest& request,
    const SecretBytes* bearer,ChatCompletionStream::Sink sink,std::stop_token cancel={});
}
