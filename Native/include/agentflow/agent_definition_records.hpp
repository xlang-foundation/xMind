#pragma once
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace agentflow {
struct AgentPermissionRule {
    // Ordered policy rules layer on backend authorization. A matching allow may
    // clear an earlier agent rule and ask pauses for controller review; neither
    // can bypass native workspace/effect authorization.
    // Actions name a native tool or '*'; resources are workspace-relative globs.
    std::string action;
    std::string resource;
    std::string effect = "deny";
    bool operator==(const AgentPermissionRule&) const = default;
};
struct AgentPermissionDenied : std::runtime_error { using std::runtime_error::runtime_error; };

struct AgentDefinition {
    std::string id;
    std::int64_t revision = 0;
    std::string model_id;
    std::string instructions;
    // Optional allowlist narrows the backend's already-authorized tool registry.
    // An absent list keeps backend defaults; an empty list offers no tools.
    std::optional<std::vector<std::string>> tools;
    std::vector<AgentPermissionRule> permissions;
    std::string name;
    std::string description;
    // `all` preserves the original xMind behavior for existing definitions.
    // `primary` agents own conversations; `subagent` agents are admitted in graphs.
    std::string mode = "all";
    bool hidden = false;
    bool is_default = false;
};

inline void validate_agent_permission_rules(const std::vector<AgentPermissionRule>& rules) {
    if (rules.size() > 128) throw std::invalid_argument("Agent permission rules exceed limits");
    for (const auto& rule : rules) {
        bool parent_component = false;
        std::size_t start = 0;
        while (start <= rule.resource.size()) {
            const auto end = rule.resource.find('/', start);
            if (rule.resource.substr(start, end == std::string::npos ? std::string::npos : end - start) == "..") parent_component = true;
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (rule.action.empty() || rule.action.size() > 128 ||
            (rule.action != "*" && rule.action.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-*") != std::string::npos) ||
            rule.resource.empty() || rule.resource.size() > 1024 || rule.resource.find('\0') != std::string::npos ||
            rule.resource.find(':') != std::string::npos || rule.resource.find('\\') != std::string::npos ||
            rule.resource.front() == '/' || parent_component ||
            (rule.effect != "deny" && rule.effect != "allow" && rule.effect != "ask"))
            throw std::invalid_argument("Invalid agent permission rule; effects must be allow, ask or deny");
    }
}

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
