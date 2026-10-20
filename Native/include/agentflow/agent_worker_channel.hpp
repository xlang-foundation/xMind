#pragma once
#include "agentflow/secret_protection.hpp"
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
enum class AgentWorkerSide {backend,worker};
struct AgentWorkerMessage {std::string operation,payload;};
struct AgentWorkerChannelStatistics {
    std::uint64_t pending_reads=0,pending_writes=0,cancelled_reads=0,cancelled_writes=0;
};
// Bounded, direction-separated and strictly sequenced authentication. A bad
// frame permanently poisons this codec. No method/authority dispatch lives here.
class AgentWorkerFrameCodec {
public:
    static constexpr std::size_t max_frame_bytes=4*1024*1024;
    AgentWorkerFrameCodec(AgentWorkerScope,SecretBytes,AgentWorkerSide);
    ~AgentWorkerFrameCodec();
    AgentWorkerFrameCodec(const AgentWorkerFrameCodec&)=delete;
    AgentWorkerFrameCodec& operator=(const AgentWorkerFrameCodec&)=delete;
    std::string encode(const AgentWorkerMessage&);
    AgentWorkerMessage decode(const std::string&);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
SecretBytes new_agent_worker_channel_key();

// One reader and one writer may run concurrently; callers join them before
// destruction. Any I/O/protocol failure poisons the channel; never retry an
// effect based on transport failure. Deadlines govern actual overlapped I/O.
class AgentWorkerChannel {
public:
    using Deadline=std::chrono::steady_clock::time_point;
    ~AgentWorkerChannel();
    AgentWorkerChannel(const AgentWorkerChannel&)=delete;
    AgentWorkerChannel& operator=(const AgentWorkerChannel&)=delete;
    // Private child entry only: receives a bootstrap through the inherited
    // handle and sends an authenticated ready frame containing its real PID.
    static std::unique_ptr<AgentWorkerChannel> accept_inherited(std::uintptr_t,Deadline,std::stop_token={});
    const AgentWorkerScope& scope()const noexcept;
    AgentWorkerChannelStatistics statistics()const noexcept;
    void send(const AgentWorkerMessage&,Deadline,std::stop_token={});
    AgentWorkerMessage receive(Deadline,std::stop_token={});
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
