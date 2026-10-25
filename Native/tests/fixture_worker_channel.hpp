#pragma once
#include "agentflow/agent_worker_channel.hpp"

namespace agentflow {
struct AgentWorkerMessage {std::string operation,payload;};
struct FixtureWorkerChannelStatistics {std::uint64_t pending_reads=0,pending_writes=0,cancelled_reads=0,cancelled_writes=0;};
// Packet fault transport belongs to test executables, not the xMind root.
void initialize_fixture_worker_channel();
class FixtureWorkerChannel {
public:
    static constexpr std::size_t max_message_bytes=4*1024*1024;
    struct Impl;
    explicit FixtureWorkerChannel(std::shared_ptr<Impl>);
    void send(const AgentWorkerMessage&,AgentWorkerChannel::Deadline,std::stop_token={});
    AgentWorkerMessage receive(AgentWorkerChannel::Deadline,std::stop_token={});
    FixtureWorkerChannelStatistics statistics()const;
private:
    std::shared_ptr<Impl> impl_;
};
FixtureWorkerChannel& fixture_worker_channel(AgentWorkerChannel&);
}
