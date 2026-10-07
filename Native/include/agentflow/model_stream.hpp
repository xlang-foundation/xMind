#pragma once
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace agentflow {
struct ModelProtocolError : std::runtime_error {using std::runtime_error::runtime_error;};
struct ModelEvent {std::string kind,json;};
struct ModelToolCall {std::string id,name,arguments_json;};
struct ModelCompletion {
    std::string content,refusal,finish_reason,usage_json="null";
    std::vector<ModelToolCall> tool_calls;
};
// Incremental Chat Completions wire adapter. No network or simulated model.
// feed receives actual SSE bytes from a provider transport. A result is available
// only after the provider's finish chunk and [DONE] marker. Own on one thread.
class ChatCompletionStream {
public:
    using Sink=std::function<void(const ModelEvent&)>;
    explicit ChatCompletionStream(Sink sink);
    ~ChatCompletionStream();
    ChatCompletionStream(const ChatCompletionStream&)=delete;
    ChatCompletionStream& operator=(const ChatCompletionStream&)=delete;
    void feed(std::string_view bytes);
    ModelCompletion finish(); // Validate EOF; incomplete streams fail explicitly.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
