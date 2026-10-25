#pragma once
#include "agentflow/process.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>

namespace agentflow {
struct AgentWorkerProtocolError:std::runtime_error {using std::runtime_error::runtime_error;};
struct AgentWorkerChannelCancelled:std::runtime_error {using std::runtime_error::runtime_error;};
struct AgentWorkerChannelTimeout:std::runtime_error {using std::runtime_error::runtime_error;};
struct AgentWorkerScope {
    std::string root_id,run_id,workspace_id,generation;
    std::int64_t workspace_revision=0;
    bool operator==(const AgentWorkerScope&)const=default;
};
struct AgentWorkerServiceAccess {std::string endpoint,ticket;std::uint32_t pid=0;};
struct AgentWorkerStartup {bool coding=false;AgentWorkerServiceAccess model;};
struct AgentWorkerPeer {std::string endpoint;std::uint32_t pid=0;std::uint64_t creation_time=0;};
// One reader and one writer may run concurrently; callers join them before
// destruction. Any I/O/protocol failure poisons the channel; never retry an
// effect based on transport failure. Native remote objects use xlang3 LRPC;
// rendezvous waits honor cancellation and the captured deadline.
class AgentWorkerChannel {
public:
    using Deadline=std::chrono::steady_clock::time_point;
    ~AgentWorkerChannel();
    AgentWorkerChannel(const AgentWorkerChannel&)=delete;
    AgentWorkerChannel& operator=(const AgentWorkerChannel&)=delete;
    // Private child entry only: receives bootstrap from an inherited mapping
    // and calls the parent's SDK remote object with its actual ready identity.
    static std::unique_ptr<AgentWorkerChannel> accept_inherited(std::uintptr_t,Deadline,std::stop_token={});
    const AgentWorkerScope& scope()const noexcept;
    AgentWorkerPeer peer()const;
    bool peer_closed()const noexcept;
    // Owner shutdown only, after readers have joined and writers are excluded.
    // Closing the remote object notifies the peer; it does not kill a PID.
    void close()noexcept;
    void configure_model(const AgentWorkerStartup&,Deadline,std::stop_token={});
    AgentWorkerStartup receive_model(Deadline,std::stop_token={});
    void publish_execution(const AgentWorkerServiceAccess&,Deadline,std::stop_token={});
    AgentWorkerServiceAccess receive_execution(Deadline,std::stop_token={});
    void wait_peer_closed(Deadline,std::stop_token={});
private:
    struct Impl;std::unique_ptr<Impl> impl_;
    explicit AgentWorkerChannel(std::unique_ptr<Impl>);
    friend class AgentWorkerProcess;
};

// Owns only the child it created, its exact image binding and kill-on-close job.
// No global PID lookup, automatic restart, effect retry or persistence access.
// Process ownership is a lifecycle boundary, not an OS filesystem sandbox.
class AgentWorkerProcess {
public:
    ~AgentWorkerProcess();
    AgentWorkerProcess(const AgentWorkerProcess&)=delete;
    AgentWorkerProcess& operator=(const AgentWorkerProcess&)=delete;
    // Retain ownership before startup can create a child or fail. A failed
    // start cannot be retried; its same owner supports explicit retirement.
    static std::unique_ptr<AgentWorkerProcess> prepare(std::shared_ptr<const BoundProcessExecutable>,AgentWorkerScope);
    void start(AgentWorkerChannel::Deadline,std::stop_token={});
    static std::unique_ptr<AgentWorkerProcess> launch(std::shared_ptr<const BoundProcessExecutable>,AgentWorkerScope,AgentWorkerChannel::Deadline,std::stop_token={});
    AgentWorkerChannel& channel()noexcept;
    std::uint32_t pid()const noexcept;
    std::uint64_t creation_time()const noexcept;
    const std::string& executable_identity()const noexcept;
    bool alive()const;
    std::uint32_t wait(AgentWorkerChannel::Deadline,std::stop_token={});
    // Explicit owner retirement, never inferred from an observation timeout.
    void terminate();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
    explicit AgentWorkerProcess(std::unique_ptr<Impl>);
};
}
