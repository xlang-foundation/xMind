#pragma once
#include "agentflow/model_stream.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace agentflow {
struct ChatProviderConfig;
struct ModelRequest;
class SecretBytes;

enum class ResponsesContextErrorCode {
    invalid_json, exceeds_limits, unsupported_item, invalid_identity,
    invalid_correlation, invalid_compaction_response, invalid_usage,
    invalid_token_count, invalid_request, invalid_endpoint
};
// Fixed native diagnostics only. Opaque state, input and provider error bodies
// must never be copied into exception messages or public context projections.
class ResponsesContextError : public ModelProtocolError {
public:
    explicit ResponsesContextError(ResponsesContextErrorCode code);
    ResponsesContextErrorCode code() const noexcept {return code_;}
private:
    ResponsesContextErrorCode code_;
};

// A private provider-native canonical input window, not an assistant message
// receipt or a tool authority. Construction validates the entire window. The
// exact array, including opaque strings, escapes and number lexemes, is kept.
class ResponsesCanonicalWindow {
public:
    const std::string& items_json() const noexcept {return items_json_;}
private:
    explicit ResponsesCanonicalWindow(std::string value):items_json_(std::move(value)) {}
    std::string items_json_;
    friend ResponsesCanonicalWindow parse_responses_canonical_window(std::string_view);
};
inline constexpr std::size_t responses_context_window_limit=4*1024*1024;
inline constexpr std::size_t responses_context_response_limit=8*1024*1024;

struct ResponsesCompactionResult {
    ResponsesCanonicalWindow window;
    std::string response_id;
    std::string usage_json="null";
    std::optional<std::int64_t> created_at;
    // Exact validated private response for a dedicated compaction-attempt
    // ledger. This is not an ordinary assistant receipt or a renderer DTO.
    std::string response_json;
};
struct ResponsesTokenCount {
    std::int64_t input_tokens;
    std::string response_json; // Exact private count acknowledgement, not usage.
};

// Pure bounded native codecs. Unsupported canonical item kinds fail rather
// than being discarded. A complete function-call/result group is required.
ResponsesCanonicalWindow parse_responses_canonical_window(std::string_view array);
ResponsesCompactionResult parse_responses_compaction_response(std::string_view response);
ResponsesTokenCount parse_responses_input_tokens_response(std::string_view response);
// Bind the validated result to this exact source request. All ordered user
// messages must remain unchanged, including IDs already carried by a window.
void validate_responses_compaction_result(const ChatProviderConfig&,const ModelRequest&,
    const ResponsesCompactionResult&);
// Repository verification uses the exact saved POST body without rebuilding
// ModelRequest or reserializing any private canonical state.
void validate_responses_compaction_result(std::string_view exact_serialized_request,
    const ResponsesCompactionResult&);
std::string serialize_responses_compaction_request(const ChatProviderConfig&,const ModelRequest&);
std::string serialize_responses_input_tokens_request(const ChatProviderConfig&,const ModelRequest&);

// Windows transport adapters are in a separate translation unit. Both derive
// only their fixed suffix from the backend-bound /responses endpoint, share its
// credential/deadline policy, and perform no automatic retry or persistence.
ResponsesCompactionResult compact_responses_context(const ChatProviderConfig&,const ModelRequest&,
    const SecretBytes* bearer,std::stop_token cancel={});
ResponsesTokenCount count_responses_context(const ChatProviderConfig&,const ModelRequest&,
    const SecretBytes* bearer,std::stop_token cancel={});
}
