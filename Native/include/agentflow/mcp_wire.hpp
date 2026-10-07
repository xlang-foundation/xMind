#pragma once
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace agentflow {
struct McpProtocolError : std::runtime_error {using std::runtime_error::runtime_error;};
enum class McpMessageKind {request,notification,result,error};
enum class McpWireEra {modern,legacy};
struct McpWireMessage {
    McpMessageKind kind;
    // JSON spelling preserves numeric vs string IDs, including 64-bit integers.
    // Empty/null error IDs are uncorrelated diagnostics, never pending results.
    std::string id_json,method,payload_json,raw_json;
};
struct McpClientIdentity {
    std::string name="xMind",version="0.1.0",capabilities_json="{}";
};
// Transport-neutral newline framing only. One owner thread. The client layer
// owns request correlation, era negotiation, permissions and process lifetime.
class McpLineStream {
public:
    using Sink=std::function<void(const McpWireMessage&)>;
    explicit McpLineStream(Sink sink);
    ~McpLineStream();
    McpLineStream(const McpLineStream&)=delete;
    McpLineStream& operator=(const McpLineStream&)=delete;
    void feed(std::string_view bytes);
    void finish(); // Incomplete EOF fails; failed streams cannot be reused.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Modern requests carry the pinned 2026-07-28 fields on each request. Legacy
// wire encoding is explicit; this does not select a version or run initialize.
std::string mcp_request(std::string id,std::string method,std::string_view params_json,
    McpWireEra era,const McpClientIdentity& identity={});
std::string mcp_notification(std::string method,std::string_view params_json="{}");
}
