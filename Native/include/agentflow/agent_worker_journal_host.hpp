#pragma once
#include "agentflow/agent_worker_records.hpp"
#include <chrono>
#include <functional>
#include <stop_token>
namespace agentflow {
class PersistenceService;
class AgentWorkerFileHost;
struct AgentWorkerJournalModelHooks {
    std::function<ModelCallReservation(const std::string&,std::stop_token)> reserve,start,finish;
    std::function<void(AgentWorkerJournalReadKind,std::stop_token)> before_read;
};
// Backend authority for one captured worker lease. Native services and temporary
// wire adapters share these typed operations; neither supplies SQL or ownership.
class AgentWorkerJournalHost final {
public:
    using Deadline=std::chrono::steady_clock::time_point;
    AgentWorkerJournalHost(PersistenceService&,AgentWorkerLeaseSpec,Deadline,std::stop_token={},AgentWorkerJournalModelHooks={});
    AgentWorkerJournalHost(PersistenceService&,AgentWorkerFileHost&,Deadline,std::stop_token={},AgentWorkerJournalModelHooks={});
    const AgentWorkerLeaseSpec& lease()const noexcept{return lease_;}
    Run run(std::string,std::stop_token={});
    Session session(std::string,std::stop_token={});
    std::vector<Message> history(std::string,std::stop_token={});
    std::vector<Event> events(std::string,std::int64_t after,std::stop_token={});
    RootBudgetRecord root_budget(std::string,std::stop_token={});
    ChildAdmissionRecord child_admission(std::string,std::stop_token={});
    bool coding_workspace_admitted(std::stop_token={});
    std::int64_t remaining_ms(std::stop_token={});
    Run begin_run(std::stop_token={});
    ModelCallReservation reserve_model(std::string,std::stop_token={});
    ModelCallReservation reserve_planning_continuation(std::string call,std::string attempt,std::stop_token={});
    ModelCallReservation start_model(std::string,std::stop_token={});
    ModelCallReservation finish_model(std::string,std::stop_token={});
    Event model_event(std::string,std::int64_t,std::string kind,std::string json,std::stop_token={});
    Event tool_event(std::string,std::int64_t,bool returned,std::stop_token={});
    Event workspace_tool_event(std::string,std::int64_t,bool returned,std::stop_token={});
    AgentWorkerToolTurnRecord commit_read_turn(std::string,std::stop_token={});
    AgentWorkerToolTurnRecord commit_workspace_turn(std::string,std::stop_token={});
    Run complete_run(std::string,std::stop_token={});
private:
    PersistenceService& store_;
    AgentWorkerLeaseSpec lease_;
    Deadline deadline_;
    std::stop_token owner_cancel_;
    AgentWorkerJournalModelHooks hooks_;
    bool workspace_=false;
    void check(std::stop_token)const;
    AgentWorkerJournalValue read(AgentWorkerJournalRead,std::stop_token);
};
}
