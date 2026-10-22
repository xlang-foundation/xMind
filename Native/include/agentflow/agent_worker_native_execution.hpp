#pragma once
#include "agentflow/agent_worker_channel.hpp"
#include "agentflow/records.hpp"
#include "xlang3/xlang3.h"
#include <functional>
namespace agentflow {
struct NativeExecutionState;
struct NativeExecutionAccess {std::string endpoint,ticket;std::uint32_t pid=0;};
class NativeExecutionBinding final {
public:
    NativeExecutionBinding(AgentWorkerScope,std::function<RunState(std::stop_token)>);
    ~NativeExecutionBinding();
    NativeExecutionBinding(const NativeExecutionBinding&)=delete;
    NativeExecutionBinding& operator=(const NativeExecutionBinding&)=delete;
    NativeExecutionAccess access()const;
    void wait_closed(AgentWorkerChannel::Deadline,std::stop_token={});
private:std::shared_ptr<NativeExecutionState> state_;std::string ticket_;
};
struct NativeExecutionsApi {
    X::Value Open(std::string ticket);
    BEGIN_PACKAGE(NativeExecutionsApi)
        APISET().AddFunc<1>("Open",&NativeExecutionsApi::Open);
    END_PACKAGE
};
// Parent-side normal SDK method handles, pinned to an exact child PID and scope.
// Replies are terminal intent only; backend persistence/physical drain are separate.
class NativeExecutionClient final {
public:
    NativeExecutionClient(NativeExecutionAccess,AgentWorkerScope);
    ~NativeExecutionClient();
    RunState execute(AgentWorkerChannel::Deadline,std::stop_token={});
    void cancel();
    void close()noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
