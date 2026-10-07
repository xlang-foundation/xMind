#pragma once
#include "agentflow/mcp_wire.hpp"
#include <optional>
#include <vector>

namespace agentflow {
struct McpPendingRequest {std::string id,method;};
struct McpPreparedRequest {McpPendingRequest request;std::string frame;};
struct McpCorrelatedReply {McpPendingRequest request;McpWireMessage response;};
// One owner thread. This is correlation, not authorization or a peer-effect
// journal. A cancelled/lost tools/call can still have executed remotely.
class McpRequestTracker {
public:
    explicit McpRequestTracker(std::size_t capacity=128);
    ~McpRequestTracker();
    McpRequestTracker(const McpRequestTracker&)=delete;
    McpRequestTracker& operator=(const McpRequestTracker&)=delete;
    McpPreparedRequest prepare(std::string method,std::string_view params_json,
        McpWireEra era,const McpClientIdentity& identity={});
    // Receives a decoded response; notifications/peer requests use a separate
    // path. Late cancelled/abandoned replies are ignored within a bounded window.
    // Duplicates and unknown IDs are protocol failures, never guessed matches.
    std::optional<McpCorrelatedReply> receive(const McpWireMessage& message);
    std::string cancel(const std::string& id);
    McpPendingRequest abandon(const std::string& id); // local retirement, no wire cancellation
    // Transport loss returns outstanding identities for owning runtime recovery.
    // No replay, peer error, cancellation success or effect outcome is invented.
    std::vector<McpPendingRequest> abandon_all();
    std::size_t pending() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
