#pragma once
#include "agentflow/mcp_wire.hpp"
#include <memory>
#include <string_view>

namespace agentflow {
// One HTTP response's incremental UTF-8 JSON/SSE decoder. It does not correlate
// requests, reconnect, replay, own approvals or infer an effect outcome.
class McpHttpMessageStream {
public:
    McpHttpMessageStream(std::string_view media_type,McpLineStream::Sink sink);
    ~McpHttpMessageStream();
    McpHttpMessageStream(const McpHttpMessageStream&)=delete;
    McpHttpMessageStream& operator=(const McpHttpMessageStream&)=delete;
    void feed(std::string_view bytes);
    void finish();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
