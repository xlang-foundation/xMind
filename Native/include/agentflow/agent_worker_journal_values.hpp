#pragma once
#include "agentflow/agent_worker_records.hpp"
namespace agentflow {
struct AgentWorkerJournalCapacity:std::runtime_error {using std::runtime_error::runtime_error;};
std::string encode_worker_journal_value(const AgentWorkerJournalValue&);
std::string encode_worker_model_reservation(const ModelCallReservation&);
std::string encode_worker_tool_turn(const AgentWorkerToolTurnRecord&);
}
