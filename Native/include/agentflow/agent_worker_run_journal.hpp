#pragma once
#include "agentflow/agent_run_journal.hpp"
#include "agentflow/agent_worker_rpc.hpp"
#include <memory>

namespace agentflow {
struct AgentWorkerJournalProgress {
    std::string current_attempt,canonical_assistant;
    std::int64_t model_event_ordinal=0,model_events=0,tool_events=0,tool_turns=0;
};
// Ordinary root model/read journal. Every supported write uses a typed backend
// capability; graph/context/effect/skill operations reject rather than invent
// empty state or acknowledge writes. Progress contains acknowledged facts only,
// not live execution authority. The peer must outlive this serialized adapter.
class AgentWorkerOrdinaryJournal:public AgentRunJournal {
public:
    virtual bool bound_to(const AgentWorkerRpcPeer&)const noexcept=0;
    virtual AgentWorkerJournalProgress progress()const=0;
};
std::unique_ptr<AgentWorkerOrdinaryJournal> make_agent_worker_ordinary_journal(
    AgentWorkerRpcPeer&,std::stop_token owner_cancel={});
}
