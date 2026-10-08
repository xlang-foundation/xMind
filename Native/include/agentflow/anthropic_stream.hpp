#pragma once
#include "agentflow/model_stream.hpp"
namespace agentflow {
// Native Claude Messages SSE component, owned on one thread. No SDK or network.
class AnthropicStream {
public:
    explicit AnthropicStream(ChatCompletionStream::Sink sink);
    ~AnthropicStream();
    AnthropicStream(const AnthropicStream&)=delete;
    AnthropicStream& operator=(const AnthropicStream&)=delete;
    void feed(std::string_view bytes);
    ModelCompletion finish();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
