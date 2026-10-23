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
class FixtureWorkerRpcPeer:public AgentWorkerClientContext,private AgentWorkerServices {
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
    std::string call(const std::string& method,const std::string& payload,AgentWorkerRpcSink={},std::stop_token={});
    bool alive()const;
    void wait_closed();
    void stop();
    // Owner thread only, after external call threads have joined. Stops and
    // joins handlers, closes this exact child's channel, then observes its exit.
    // A wait timeout retains process ownership and never kills or restarts it.
    std::uint32_t drain(AgentWorkerProcess&,AgentWorkerChannel::Deadline);
    const AgentWorkerScope& scope()const noexcept override;
    AgentWorkerServices& services()override;
private:
    AgentWorkerModelAdmission admission(std::stop_token={}) override;
    ModelRequest prepare_request(std::stop_token={}) override;
    ModelRequest rebuild_request(const std::string&,const ModelRequest&,std::stop_token={}) override;
    ModelCompletion complete(const std::string&,const ModelRequest&,ChatCompletionStream::Sink,std::stop_token={}) override;
    AgentCallableToolResult callable_invoke(const AgentCallableInvocation&,std::stop_token={}) override;
    std::vector<std::string> callable_names(std::stop_token={}) override;
    AgentRemoteToolResult remote_invoke(const AgentRemoteToolInvocation&,std::stop_token={}) override;
    AgentProcessToolResult process_invoke(const AgentProcessToolInvocation&,std::stop_token={}) override;
    bool process_join(const std::string& attempt,std::stop_token={}) override;
    AgentFileToolResult file_invoke(const AgentFileToolInvocation&,std::stop_token={}) override;
    AgentReadToolResult read_invoke(const AgentWorkspaceInvocation&,std::stop_token={}) override;
    std::string journal_run(std::string id) override;
    std::string journal_session(std::string id) override;
    std::string journal_history(std::string id) override;
    std::string journal_events(std::string id,std::int64_t after) override;
    std::string journal_root_budget(std::string id) override;
    std::string journal_child_admission(std::string id) override;
    std::string journal_workspace_mode() override;
    std::string journal_execution_window() override;
    std::string journal_begin() override;
    std::string journal_reserve_model(std::string attempt) override;
    std::string journal_reserve_planning_continuation(std::string call,std::string attempt) override;
    std::string journal_start_model(std::string attempt) override;
    std::string journal_finish_model(std::string attempt) override;
    std::string journal_model_event(std::string attempt,std::int64_t ordinal,std::string kind,std::string json) override;
    std::string journal_tool_event(std::string attempt,std::int64_t ordinal,bool returned) override;
    std::string journal_workspace_tool_event(std::string attempt,std::int64_t ordinal,bool returned) override;
    std::string journal_commit_read_turn(std::string attempt) override;
    std::string journal_commit_workspace_turn(std::string attempt) override;
    std::string journal_complete(std::string attempt) override;
struct Impl;std::unique_ptr<Impl> impl_;
};
}

#include "agentflow/agent_worker_retirement.hpp"
namespace agentflow {
AgentWorkerTerminalObservation drain_and_terminate_agent_worker(PersistenceService&,AgentWorkerProcess&,FixtureWorkerRpcPeer&,const AgentWorkerLeaseSpec&,RunState,const std::string&,AgentWorkerChannel::Deadline);
AgentWorkerTerminalObservation drain_completed_agent_worker(PersistenceService&,AgentWorkerProcess&,FixtureWorkerRpcPeer&,const AgentWorkerLeaseSpec&,AgentWorkerChannel::Deadline);
}

// Packet callback binders exist only for failure-injection fixtures.
#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/agent_worker_read_rpc.hpp"
#include "agentflow/agent_worker_file_rpc.hpp"
#include "agentflow/agent_worker_process_rpc.hpp"
#include "agentflow/agent_worker_remote_rpc.hpp"
#include "agentflow/agent_worker_callable_rpc.hpp"
namespace agentflow {
FixtureWorkerRpcHandler bind_agent_worker_model_rpc(AgentWorkerModelHost&,const AgentWorkerScope&);
FixtureWorkerRpcHandler bind_agent_worker_journal_rpc(PersistenceService&,AgentWorkerLeaseSpec,
    const AgentWorkerScope&,AgentWorkerChannel::Deadline,std::stop_token owner_cancel={},AgentWorkerJournalModelHooks={});
FixtureWorkerRpcHandler bind_agent_worker_workspace_journal_rpc(PersistenceService&,AgentWorkerFileHost&,
    const AgentWorkerScope&,AgentWorkerChannel::Deadline,std::stop_token owner_cancel={},AgentWorkerJournalModelHooks={});
FixtureWorkerRpcHandler bind_agent_worker_read_rpc(AgentWorkerReadHost&,const AgentWorkerScope&);
FixtureWorkerRpcHandler bind_agent_worker_read_rpc(AgentReadToolHost&,const AgentWorkerLeaseSpec&,const AgentWorkerScope&);
FixtureWorkerRpcHandler bind_agent_worker_file_rpc(AgentWorkerFileHost&,const AgentWorkerScope&);
FixtureWorkerRpcHandler bind_agent_worker_file_rpc(AgentFileToolHost&,const AgentWorkerLeaseSpec&,const AgentWorkerScope&);
FixtureWorkerRpcHandler bind_agent_worker_process_rpc(AgentWorkerProcessHost&,const AgentWorkerScope&);
FixtureWorkerRpcHandler bind_agent_worker_process_rpc(AgentProcessOwnerHost&,const AgentWorkerLeaseSpec&,const AgentWorkerScope&);
FixtureWorkerRpcHandler bind_agent_worker_remote_rpc(AgentWorkerRemoteHost&,const AgentWorkerScope&);
FixtureWorkerRpcHandler bind_agent_worker_remote_rpc(AgentRemoteToolHost&,const AgentWorkerLeaseSpec&,const AgentWorkerScope&);
FixtureWorkerRpcHandler bind_agent_worker_callable_rpc(AgentWorkerCallableHost&,const AgentWorkerScope&);
FixtureWorkerRpcHandler bind_agent_worker_callable_rpc(AgentWorkerCallableService&,const AgentWorkerLeaseSpec&,const AgentWorkerScope&);
}
