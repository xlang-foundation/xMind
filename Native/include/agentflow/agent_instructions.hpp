#pragma once
#include "agentflow/persistence_service.hpp"
namespace agentflow {
struct AgentInstructionPolicy {std::string instructions;std::int64_t revision=0;};
// Trusted startup/offline configuration. Native effect permissions remain
// independent of prompt text. All persistence is owned by embedded xlang3.
class AgentInstructionStore {
public:
    explicit AgentInstructionStore(PersistenceService& store):store_(store){}
    AgentInstructionPolicy load();
    AgentInstructionPolicy apply(const std::string& trusted_json);
private:
    PersistenceService& store_;
};
}
