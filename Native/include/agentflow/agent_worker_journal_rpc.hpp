#pragma once
#include "agentflow/agent_worker_journal_host.hpp"
#include "agentflow/agent_worker_rpc.hpp"
#include "agentflow/agent_worker_records.hpp"

namespace agentflow {
class PersistenceService;
class AgentWorkerFileHost;
// Synchronous owned operations used by a future AgentRunJournal adapter. This is
// deliberately not a partial AgentRunJournal implementation with no-op writes.
class AgentWorkerJournalClient {
public:
    explicit AgentWorkerJournalClient(AgentWorkerRpcPeer& peer):peer_(peer){}
    Run run(const std::string&);
    Session session(const std::string&);
    std::vector<Message> run_history(const std::string&);
    std::vector<Event> events(const std::string&,std::int64_t after=0);
    RootBudgetRecord root_budget(const std::string&);
    ChildAdmissionRecord child_admission(const std::string&);
    AgentWorkerChannel::Deadline execution_deadline();
    bool coding_workspace_admitted();
    Run begin_run();
    Event acknowledge_model_event(const std::string& attempt,std::int64_t ordinal,const std::string& kind,const std::string& json);
    Event acknowledge_tool_event(const std::string& attempt,std::int64_t ordinal,bool returned,const std::string& kind,const std::string& json);
    Event acknowledge_workspace_tool_event(const std::string& attempt,std::int64_t ordinal,bool returned,const std::string& kind,const std::string& json);
    // Root, owner and role come from captured backend admission, not arguments.
    ModelCallReservation reserve_model_call(const std::string& attempt);
    ModelCallReservation start_model_call(const std::string& attempt);
    ModelCallReservation finish_model_call(const std::string& attempt);
    AgentWorkerToolTurnRecord commit_read_turn(const std::string& attempt);
    AgentWorkerToolTurnRecord commit_workspace_turn(const std::string& attempt);
    Run complete_run(const std::string& final_attempt);
private:AgentWorkerRpcPeer& peer_;
};
// Bind only this captured backend lease to its exact authenticated channel.
// Completion uses backend-captured model facts, never a supplied assistant.
// Read-turn commit uses backend model/read facts; no supplied history or tool
// output is accepted. Permissions, generic writes and SQL remain closed.
AgentWorkerRpcHandler bind_agent_worker_journal_rpc(PersistenceService&,AgentWorkerLeaseSpec,
    const AgentWorkerScope&,AgentWorkerChannel::Deadline,std::stop_token owner_cancel={},AgentWorkerJournalModelHooks={});
// Coding admission requires an actual durable native file owner. No child
// payload or flag can promote the read-only journal to this capability.
AgentWorkerRpcHandler bind_agent_worker_workspace_journal_rpc(PersistenceService&,AgentWorkerFileHost&,
    const AgentWorkerScope&,AgentWorkerChannel::Deadline,std::stop_token owner_cancel={},AgentWorkerJournalModelHooks={});
}
