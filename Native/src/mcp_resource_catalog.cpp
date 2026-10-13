#include "agentflow/mcp_resource_catalog.hpp"
#include "agentflow/mcp_client_factory.hpp"
#include "agentflow/mcp_oauth_credentials.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <set>
#include <string_view>

namespace agentflow {
namespace {
using Json = nlohmann::json;

bool reflects_credential(std::string_view source, const std::vector<SecretBytes>& credentials) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(source.data());
    const auto* end = begin + source.size();
    for (const auto& credential : credentials) {
        const auto secret = credential.view();
        if (!secret.empty() && secret.size() <= source.size() &&
            std::search(begin, end, secret.begin(), secret.end()) != end) return true;
    }
    return false;
}
}

NativeMcpResourceCatalog::NativeMcpResourceCatalog(PersistenceService& persistence,
    std::vector<McpServerSetting> servers)
    : persistence_(persistence), servers_(std::move(servers)) {}

std::string NativeMcpResourceCatalog::list_json() {
    const auto deadline = McpToolClient::Deadline::clock::now() + std::chrono::seconds(8);
    Json servers = Json::array();
    Json resources = Json::array();
    std::size_t total_resources = 0;
    std::size_t total_bytes = 0;
    for (const auto& setting : servers_) {
        Json entry = {{"id", setting.id}, {"revision", setting.revision},
            {"enabled", setting.enabled}, {"state", "disabled"}};
        if (!setting.enabled) {
            servers.push_back(std::move(entry));
            continue;
        }
        try {
            std::vector<SecretBytes> private_credentials;
            auto client = connect_mcp_client(setting, persistence_, deadline, {}, &private_credentials);
            Json capabilities = Json::parse(client->server().capabilities_json);
            if (!capabilities.is_object() || !capabilities.contains("resources")) {
                entry["state"] = "unsupported";
                client->shutdown();
                servers.push_back(std::move(entry));
                continue;
            }
            Json discovered = Json::array();
            std::set<std::string> identities, cursors;
            std::optional<std::string> cursor;
            bool reflected = false;
            for (std::size_t page_number = 0; page_number < 32; ++page_number) {
                if (McpToolClient::Deadline::clock::now() >= deadline)
                    throw McpTransportTimeout("MCP resource catalog deadline exceeded");
                const auto page = client->list_resources(cursor, deadline);
                for (const auto& raw : page.entries_json) {
                    if (++total_resources > 1024) throw McpProtocolError("MCP resource catalog exceeds limits");
                    total_bytes += raw.size();
                    if (total_bytes > 4 * 1024 * 1024) throw McpProtocolError("MCP resource catalog metadata exceeds limits");
                    if (reflects_credential(raw, private_credentials)) { reflected = true; break; }
                    const auto resource = Json::parse(raw);
                    if (reflects_credential(resource.dump(), private_credentials)) { reflected = true; break; }
                    if (!resource.is_object() || !resource.contains("uri") || !resource.at("uri").is_string() ||
                        resource.at("uri").get_ref<const std::string&>().empty() ||
                        !identities.insert(resource.at("uri").get<std::string>()).second)
                        throw McpProtocolError("MCP resource catalog contains an invalid or duplicate URI");
                    discovered.push_back({{"server_id", setting.id}, {"config_revision", setting.revision},
                        {"resource", resource}});
                }
                if (reflected) break;
                if (!page.next_cursor) break;
                if (page_number == 31 || !cursors.insert(*page.next_cursor).second)
                    throw McpProtocolError("MCP resource catalog pagination is invalid");
                cursor = page.next_cursor;
            }
            client->shutdown();
            if (reflected) throw McpProtocolError("MCP resource metadata reflected a configured credential");
            entry["state"] = "connected";
            entry["resource_count"] = discovered.size();
            for (auto& item : discovered) resources.push_back(std::move(item));
        } catch (const McpOAuthAuthorizationRequired&) {
            entry["state"] = "authorization_required";
        } catch (...) {
            entry["state"] = "unavailable";
        }
        servers.push_back(std::move(entry));
    }
    return Json{{"servers", std::move(servers)}, {"resources", std::move(resources)}}.dump();
}
}
