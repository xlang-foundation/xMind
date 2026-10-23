#pragma once
#include "agentflow/agent_worker_client_context.hpp"
#include <functional>

namespace agentflow {
class NativeExecutionClient;
using FixtureWorkerRpcHandler=std::function<std::string(const std::string& method,const std::string& payload,AgentWorkerRpcSink,std::stop_token)>;
// Test-only packet fault fixture. Product workers use the xmind SDK root.
// Multiplexed owned-value transport, not method authorization. One bounded
// reader routes replies/events; handlers run separately, so a streaming callback
// may make a nested synchronous journal call. Backend handlers must authorize
// every method against captured admission and honor cancellation/deadlines.
// This object exclusively owns channel I/O. Owner calls stop(), then joins its
// caller threads before destruction; stop must not run inside a handler/sink.
class FixtureWorkerRpcPeer:public AgentWorkerClientContext {
public:
    FixtureWorkerRpcPeer(AgentWorkerChannel&,FixtureWorkerRpcHandler,AgentWorkerChannel::Deadline,std::shared_ptr<NativeWorkerModelClient> sdk_services={});
    ~FixtureWorkerRpcPeer()override;
    FixtureWorkerRpcPeer(const FixtureWorkerRpcPeer&)=delete;
    FixtureWorkerRpcPeer& operator=(const FixtureWorkerRpcPeer&)=delete;
    // Start only after handler dependencies (including nested-call adapters)
    // are fully constructed. Start once; a closed session cannot restart.
    void start();
    std::shared_ptr<NativeWorkerModelClient> native_model()const override;
    void bind_native_execution(std::shared_ptr<NativeExecutionClient>);
    std::shared_ptr<NativeExecutionClient> native_execution()const;
    AgentWorkerChannel::Deadline execution_deadline()const noexcept;
    std::string call(const std::string& method,const std::string& payload,AgentWorkerRpcSink={},std::stop_token={})override;
    bool alive()const;
    void wait_closed();
    void stop();
    // Owner thread only, after external call threads have joined. Stops and
    // joins handlers, closes this exact child's channel, then observes its exit.
    // A wait timeout retains process ownership and never kills or restarts it.
    std::uint32_t drain(AgentWorkerProcess&,AgentWorkerChannel::Deadline);
    const AgentWorkerScope& scope()const noexcept override;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}

#include "agentflow/agent_worker_retirement.hpp"
namespace agentflow {
AgentWorkerTerminalObservation drain_and_terminate_agent_worker(PersistenceService&,AgentWorkerProcess&,FixtureWorkerRpcPeer&,const AgentWorkerLeaseSpec&,RunState,const std::string&,AgentWorkerChannel::Deadline);
AgentWorkerTerminalObservation drain_completed_agent_worker(PersistenceService&,AgentWorkerProcess&,FixtureWorkerRpcPeer&,const AgentWorkerLeaseSpec&,AgentWorkerChannel::Deadline);
}
