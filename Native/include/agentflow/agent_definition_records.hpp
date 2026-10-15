#pragma once
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace agentflow {
struct AgentDefinition {
    std::string id;
    std::int64_t revision = 0;
    std::string model_id;
    std::string instructions;
    // Optional allowlist narrows the backend's already-authorized tool registry.
    // An absent list keeps backend defaults; an empty list offers no tools.
    std::optional<std::vector<std::string>> tools;
    std::string name;
    std::string description;
    // `all` preserves the original xMind behavior for existing definitions.
    // `primary` agents own conversations; `subagent` agents are admitted in graphs.
    std::string mode = "all";
    bool hidden = false;
    bool is_default = false;
};

inline void validate_agent_tool_allowlist(const std::optional<std::vector<std::string>>& tools) {
    if (!tools) return;
    if (tools->size() > 64) throw std::invalid_argument("Agent tool allowlist exceeds limits");
    std::set<std::string> unique;
    for (const auto& tool : *tools) {
        if (tool.empty() || tool.size() > 128 ||
            tool.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos ||
            !unique.insert(tool).second)
            throw std::invalid_argument("Invalid or duplicate agent tool binding");
    }
}

struct AgentDefinitionMetadata {
    std::string id;
    std::int64_t revision = 0;
    std::string model_id;
    std::string name;
    std::string description;
    std::string mode = "all";
    bool hidden = false;
    bool is_default = false;
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
