#pragma once
#include "agentflow/model_stream.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "agentflow/responses_context.hpp"
#include <optional>
#include <stdexcept>

namespace agentflow {
class IncompatibleProviderHistory : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};
class ModelRequestCapacityExceeded : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};
enum class Capability {unknown,unsupported,supported};
enum class ProviderWire {chat_completions,responses,anthropic_messages,gemini_generate_content};
enum class ReasoningEffort {none,minimal,low,medium,high,xhigh,max};
enum class MessageRole {system,developer,user,assistant,tool};
struct ModelMessage {
    MessageRole role=MessageRole::user;
    std::string content;
    std::vector<ModelToolCall> tool_calls;
    std::string tool_call_id;
    std::string refusal;
    std::string provider_items_json="[]";
};
struct ModelToolDefinition {std::string name,description,input_schema_json;};
struct ModelRequest {
    std::vector<ModelMessage> messages;
    std::vector<ModelToolDefinition> tools;
    bool include_usage=false;
    std::optional<std::int64_t> max_output_tokens;
    // Private validated provider-native context. The Responses adapter places
    // it after current trusted instructions and before original tail messages.
    std::optional<ResponsesCanonicalWindow> canonical_window;
};
struct ChatProviderConfig {
    // Backend-selected endpoint and model. Gemini uses a versioned endpoint
    // base; its model resource is bound into the path by the native adapter.
    std::string endpoint,model;
    Capability tools=Capability::unknown,stream_usage=Capability::unknown,output_limit=Capability::unknown;
    std::chrono::milliseconds deadline{120000},idle_timeout{60000};
    ProviderWire wire=ProviderWire::chat_completions;
    Capability reasoning=Capability::unknown;
    std::optional<ReasoningEffort> reasoning_effort;
    // Explicit backend dialect; never inferred from an endpoint or model ID.
    ChatDialect chat_dialect=ChatDialect::openai;
};
// Validate saved native provider receipts and make a per-request canonical
// projection when conversation history crosses provider wires. The persisted
// history is never modified.
ModelRequest project_provider_history(const ChatProviderConfig& target,const ModelRequest& request);
// Explicit request-only re-projection after native receipt provenance proves an
// authority change. Validate every source receipt; retain canonical conversation
// fields and omit only the selected old provider continuations. Never mutate the
// transcript or label old opaque state as belonging to the new authority.
ModelRequest reproject_authority_history(const ModelRequest&,const std::vector<std::size_t>& message_indices);
// Only active native owners may select these checked provider originals.
// This pure step validates their wire receipts and keeps canonical public data.
ModelRequest reproject_checked_provider_history(const ModelRequest&,const std::vector<std::size_t>& message_indices);
std::string serialize_provider_history_projection(const ChatProviderConfig&,const ModelRequest&);
// Pure native request serialization; no model/provider availability is inferred.
std::string serialize_chat_request(const ChatProviderConfig& config,const ModelRequest& request);
// Mistral uses the Chat Completions shape with its documented max_tokens field.
std::string serialize_mistral_request(const ChatProviderConfig& config,const ModelRequest& request);
// Common field/correlation validation for the bounded Responses input window.
// Returns the common field representation, not a transport request.
std::string serialize_responses_request_fields(const ChatProviderConfig& config,const ModelRequest& request);
std::string serialize_responses_request(const ChatProviderConfig& config,const ModelRequest& request);
// Claude Messages request component; transport/routing enrollment is separate.
// Bounded provider default used when a saved profile chooses its model per session.
std::int64_t anthropic_default_output_tokens(const std::string& model);
std::string serialize_anthropic_request(const ChatProviderConfig& config,const ModelRequest& request);
// Transport-backed provider adapter. Credentials remain caller/backend owned.
// Available on Windows until other native transport implementations are added.
ModelCompletion complete_chat(const ChatProviderConfig& config,const ModelRequest& request,
    const SecretBytes* bearer,ChatCompletionStream::Sink sink,std::stop_token cancel={});
ModelCompletion complete_model(const ChatProviderConfig& config,const ModelRequest& request,
    const SecretBytes* bearer,ChatCompletionStream::Sink sink,std::stop_token cancel={});
}
