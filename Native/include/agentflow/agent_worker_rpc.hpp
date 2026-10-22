#pragma once
#include "agentflow/agent_worker_channel.hpp"
#include <functional>

namespace agentflow {
class NativeWorkerModelClient;
class NativeExecutionClient;
struct AgentWorkerRpcClosed:std::runtime_error {using std::runtime_error::runtime_error;};
struct AgentWorkerRpcCancelled:std::runtime_error {using std::runtime_error::runtime_error;};
struct AgentWorkerRpcTimeout:std::runtime_error {using std::runtime_error::runtime_error;};
class AgentWorkerRpcRemoteError:public std::runtime_error {
public:
    AgentWorkerRpcRemoteError(std::string code,std::string detail={});
    const std::string& code()const noexcept{return code_;}
    const std::string& detail()const noexcept{return detail_;}
private:std::string code_,detail_;
};
using AgentWorkerRpcSink=std::function<void(const std::string&)>;
using AgentWorkerRpcHandler=std::function<std::string(const std::string& method,const std::string& payload,AgentWorkerRpcSink,std::stop_token)>;
// Multiplexed owned-value transport, not method authorization. One bounded
// reader routes replies/events; handlers run separately, so a streaming callback
// may make a nested synchronous journal call. Backend handlers must authorize
// every method against captured admission and honor cancellation/deadlines.
// This object exclusively owns channel I/O. Owner calls stop(), then joins its
// caller threads before destruction; stop must not run inside a handler/sink.
class AgentWorkerRpcPeer {
public:
    AgentWorkerRpcPeer(AgentWorkerChannel&,AgentWorkerRpcHandler,AgentWorkerChannel::Deadline);
    ~AgentWorkerRpcPeer();
    AgentWorkerRpcPeer(const AgentWorkerRpcPeer&)=delete;
    AgentWorkerRpcPeer& operator=(const AgentWorkerRpcPeer&)=delete;
    // Start only after handler dependencies (including nested-call adapters)
    // are fully constructed. Start once; a closed session cannot restart.
    void start();
    void bind_native_model(std::shared_ptr<NativeWorkerModelClient>);
    std::shared_ptr<NativeWorkerModelClient> native_model()const;
    void bind_native_execution(std::shared_ptr<NativeExecutionClient>);
    std::shared_ptr<NativeExecutionClient> native_execution()const;
    AgentWorkerChannel::Deadline execution_deadline()const noexcept;
    std::string call(const std::string& method,const std::string& payload,AgentWorkerRpcSink={},std::stop_token={});
    bool alive()const;
    void wait_closed();
    void stop();
    // Owner thread only, after external call threads have joined. Stops and
    // joins handlers, closes this exact child's channel, then observes its exit.
    // A wait timeout retains process ownership and never kills or restarts it.
    std::uint32_t drain(AgentWorkerProcess&,AgentWorkerChannel::Deadline);
    const AgentWorkerScope& scope()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
