#pragma once
#include "agentflow/model_stream.hpp"
#include <optional>

namespace agentflow {
struct GeminiFunctionCall {
    std::string name;
    std::optional<std::string> id,arguments_json;
};
struct GeminiCompletion {
    std::string content,parts_json="[]",finish_reason,provider_finish_reason;
    std::string usage_json="null",model_version,response_id;
    std::vector<GeminiFunctionCall> function_calls;
};
// GenerateContent SSE component, owned on one thread. Wire-specific history
// retains original part boundaries and JSON number tokens. No invented call IDs.
// Tools are available only from a validated finish(), never an incomplete stream.
class GeminiStream {
public:
    explicit GeminiStream(ChatCompletionStream::Sink sink);
    ~GeminiStream();
    GeminiStream(const GeminiStream&)=delete;
    GeminiStream& operator=(const GeminiStream&)=delete;
    void feed(std::string_view bytes);
    GeminiCompletion finish();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
