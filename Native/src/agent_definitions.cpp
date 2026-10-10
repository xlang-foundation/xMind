#include "agentflow/agent_definitions.hpp"
#include "agentflow/authoring_document.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>

namespace agentflow {
namespace {
using Json = nlohmann::json;
constexpr std::int64_t maximum_revision = 9007199254740991;

Json parse(const std::string& source, bool stored) {
    if (source.size() > (stored ? 1048576 : 262144))
        throw std::invalid_argument("Agent definition catalog exceeds limits");
    try {
        auto encoded = source;
        if (!stored) {
            const auto first = source.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) throw std::invalid_argument("Empty agent definition catalog");
            if (source[first] != '{' && source[first] != '[')
                encoded = authoring_yaml_to_json(source);
        }
        return Json::parse(mcp_compact_object(encoded));
    } catch (const McpProtocolError&) {
        throw std::invalid_argument("Invalid agent definition catalog JSON or YAML");
    } catch (const Json::exception&) {
        throw std::invalid_argument("Invalid agent definition catalog JSON or YAML");
    }
}

void fields(const Json& value, std::initializer_list<const char*> allowed) {
    if (!value.is_object()) throw std::invalid_argument("Agent definition catalog value must be an object");
    for (auto it = value.begin(); it != value.end(); ++it)
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* key) { return it.key() == key; }))
            throw std::invalid_argument("Unknown or backend-owned agent definition field");
}

bool identifier(const std::string& value) {
    return !value.empty() && value.size() <= 64 &&
        value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") == std::string::npos;
}

std::string text(const Json& value, const char* key, std::size_t limit, bool empty = false) {
    if (!value.contains(key) || !value.at(key).is_string()) throw std::invalid_argument("Missing agent definition text");
    auto result = value.at(key).get<std::string>();
    if ((!empty && result.empty()) || result.size() > limit || result.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid agent definition text");
    return result;
}

std::int64_t revision(const Json& value, const char* key) {
    if (!value.contains(key) || !value.at(key).is_number_integer() || value.at(key) < 1 || value.at(key) > maximum_revision)
        throw std::invalid_argument("Invalid stored agent definition revision");
    return value.at(key).get<std::int64_t>();
}

AgentDefinitionCatalog decode(const Json& value, bool stored) {
    if (stored) fields(value, {"version", "revision", "agents", "retired_ids"});
    else fields(value, {"agents"});
    if (!value.contains("agents") || !value.at("agents").is_array() || value.at("agents").size() > 64)
        throw std::invalid_argument("Agent definition catalog accepts at most 64 agents");

    AgentDefinitionCatalog result;
    if (stored) {
        if (!value.contains("version") || !value.at("version").is_number_integer() || value.at("version") != 1)
            throw std::invalid_argument("Invalid agent definition catalog version");
        result.revision = revision(value, "revision");
    }

    std::set<std::string> ids;
    for (const auto& record : value.at("agents")) {
        if (stored) fields(record, {"id", "revision", "model_id", "instructions"});
        else fields(record, {"id", "model_id", "instructions"});
        const auto key = text(record, "id", 64);
        if (!identifier(key) || !ids.insert(key).second) throw std::invalid_argument("Invalid or duplicate agent definition ID");
        const auto model = record.contains("model_id") ? text(record, "model_id", 256, true) : std::string{};
        const auto guidance = text(record, "instructions", 32768);
        const auto rev = stored ? revision(record, "revision") : 0;
        if (rev > result.revision) throw std::invalid_argument("Agent definition revision exceeds catalog revision");
        result.entries.push_back({key, rev, model, guidance});
    }

    if (stored) {
        if (!value.contains("retired_ids") || !value.at("retired_ids").is_array() || value.at("retired_ids").size() > 4096)
            throw std::invalid_argument("Invalid retired agent definition identities");
        for (const auto& retired : value.at("retired_ids")) {
            if (!retired.is_string()) throw std::invalid_argument("Invalid retired agent definition identity");
            auto key = retired.get<std::string>();
            if (!identifier(key) || !ids.insert(key).second) throw std::invalid_argument("Duplicate or active retired agent definition identity");
            result.retired_ids.push_back(std::move(key));
        }
    }
    return result;
}

std::string encode(const AgentDefinitionCatalog& catalog) {
    auto entries = Json::array();
    for (const auto& agent : catalog.entries)
        entries.push_back({{"id", agent.id}, {"revision", agent.revision}, {"model_id", agent.model_id}, {"instructions", agent.instructions}});
    return Json{{"version", 1}, {"revision", catalog.revision}, {"agents", std::move(entries)}, {"retired_ids", catalog.retired_ids}}.dump();
}
}

AgentDefinitionCatalog AgentDefinitionStore::load() {
    std::string source;
    try { source = store_.information("native-agents", "catalog").get(); }
    catch (const NotFound&) { return {}; }
    return decode(parse(source, true), true);
}

AgentDefinitionCatalog AgentDefinitionStore::apply(const std::string& source) {
    auto desired = decode(parse(source, false), false);
    const auto previous = load();
    desired.retired_ids = previous.retired_ids;
    bool changed = previous.revision == 0 || desired.entries.size() != previous.entries.size();
    for (std::size_t i = 0; i < desired.entries.size(); ++i) {
        auto& entry = desired.entries[i];
        if (std::find(previous.retired_ids.begin(), previous.retired_ids.end(), entry.id) != previous.retired_ids.end())
            throw std::invalid_argument("Retired agent definition IDs cannot be reused");
        const auto found = std::find_if(previous.entries.begin(), previous.entries.end(), [&](const auto& old) { return old.id == entry.id; });
        if (found == previous.entries.end()) { entry.revision = 1; changed = true; }
        else if (found->model_id == entry.model_id && found->instructions == entry.instructions) entry.revision = found->revision;
        else {
            if (found->revision == maximum_revision) throw std::overflow_error("Agent definition revision exhausted");
            entry.revision = found->revision + 1;
            changed = true;
        }
        if (i >= previous.entries.size() || previous.entries[i].id != entry.id) changed = true;
    }
    for (const auto& old : previous.entries) {
        if (std::none_of(desired.entries.begin(), desired.entries.end(), [&](const auto& agent) { return agent.id == old.id; })) {
            if (desired.retired_ids.size() >= 4096) throw std::overflow_error("Retired agent definition identity budget exhausted");
            desired.retired_ids.push_back(old.id);
            changed = true;
        }
    }
    if (!changed) return previous;
    if (previous.revision == maximum_revision) throw std::overflow_error("Agent definition catalog revision exhausted");
    desired.revision = previous.revision + 1;
    store_.put_information("native-agents", "catalog", encode(desired)).get();
    return desired;
}
}
