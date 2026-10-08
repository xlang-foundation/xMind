#pragma once
#include "agentflow/model_provider.hpp"

namespace agentflow {
enum class GeminiRole {user,model};
enum class GeminiPartKind {text,function_call,function_response,signature};
struct GeminiPart {
    GeminiPartKind kind=GeminiPartKind::text;
    std::string text,name,object_json="{}";
    std::optional<std::string> call_id,thought_signature;
    // Preserve absent/false/true independently when replaying model parts.
    std::optional<bool> thought;
    // Provider replay must distinguish absent args from an explicit empty object.
    bool arguments_omitted=false;
    std::optional<std::string> part_metadata_json;
};
struct GeminiContent {GeminiRole role=GeminiRole::user;std::vector<GeminiPart> parts;};
struct GeminiRequest {
    std::vector<std::string> system_instructions;
    std::vector<GeminiContent> contents;
    std::vector<ModelToolDefinition> tools;
    Capability function_calls=Capability::unknown;
    std::optional<std::int64_t> max_output_tokens;
};
// Native GenerateContent body component. Endpoint/model binding, authentication,
// streaming and engine-history adaptation remain separate backend work.
// Replay provider IDs/signatures exactly; never invent either value.
std::string serialize_gemini_request(const GeminiRequest& request);
}
