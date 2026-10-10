#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agentflow {
struct AgentDefinition {
    std::string id;
    std::int64_t revision = 0;
    std::string model_id;
    std::string instructions;
};

struct AgentDefinitionMetadata {
    std::string id;
    std::int64_t revision = 0;
    std::string model_id;
};

struct AgentDefinitionCatalog {
    std::int64_t revision = 0;
    std::vector<AgentDefinition> entries;
    std::vector<std::string> retired_ids;
};

struct SessionAgentState {
    std::optional<AgentDefinition> selected;
    std::int64_t revision = 0;
    bool editable = false;
};
}
