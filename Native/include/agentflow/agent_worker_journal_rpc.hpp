#pragma once
#include "agentflow/agent_worker_rpc.hpp"
#include "agentflow/agent_worker_records.hpp"

namespace agentflow {
class PersistenceService;
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
    // Root, owner and role come from captured backend admission, not arguments.
    ModelCallReservation reserve_model_call(const std::string& attempt);
    ModelCallReservation start_model_call(const std::string& attempt);
    ModelCallReservation finish_model_call(const std::string& attempt);
    Run complete_run(const std::string& final_attempt);
private:AgentWorkerRpcPeer& peer_;
};
// Bind only this captured backend lease to its exact authenticated channel.
// Completion uses backend-captured model facts, never a supplied assistant.
// Permissions, tool/context writes, arbitrary information and SQL remain closed.
AgentWorkerRpcHandler bind_agent_worker_journal_rpc(PersistenceService&,AgentWorkerLeaseSpec,
    const AgentWorkerScope&,AgentWorkerChannel::Deadline,std::stop_token owner_cancel={});
}
