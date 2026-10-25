#pragma once
#include "agentflow/agent_worker_channel.hpp"

namespace agentflow {
// Packet fault transport belongs to test executables, not the xMind root.
void initialize_fixture_worker_channel();
class FixtureWorkerChannel {
public:
    struct Impl;
    explicit FixtureWorkerChannel(std::shared_ptr<Impl>);
    void send(const AgentWorkerMessage&,AgentWorkerChannel::Deadline,std::stop_token={});
    AgentWorkerMessage receive(AgentWorkerChannel::Deadline,std::stop_token={});
    AgentWorkerChannelStatistics statistics()const;
private:
    std::shared_ptr<Impl> impl_;
};
FixtureWorkerChannel& fixture_worker_channel(AgentWorkerChannel&);
}
