#pragma once
#include "agentflow/model_stream.hpp"
namespace agentflow {
// Native Responses SSE decoder. Completed output items remain available for
// stateless continuation; unsupported output types and incomplete turns fail.
class ResponsesStream {
public:
    explicit ResponsesStream(ChatCompletionStream::Sink sink);
    ~ResponsesStream();
    void feed(std::string_view bytes);
    ModelCompletion finish();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
