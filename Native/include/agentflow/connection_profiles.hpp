#pragma once
#include "agentflow/persistence_service.hpp"
#include <cstdint>
#include <mutex>
#include <optional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

namespace agentflow {
void validate_remote_connection_origin(const std::string& origin);
enum class ConnectionKind {local,nexus};
struct ConnectionProfile {
    std::string id,name,origin;
    ConnectionKind kind=ConnectionKind::local;
    std::int64_t revision=0;
};
struct ConnectionProfileCatalog {
    std::int64_t revision=0;
    std::string active_id;
    std::vector<ConnectionProfile> profiles;
    std::vector<std::string> retired_ids;
};

// Persistent public connection metadata only. Authentication remains a
// separate backend-owned encrypted credential and is added by the connector
// enrollment flow, never by this YAML/JSON configuration surface.
class ConnectionProfiles {
public:
    explicit ConnectionProfiles(PersistenceService& store);
    std::shared_lock<std::shared_mutex> admit_local(std::int64_t profile_revision);
    std::string local_server_identity();
    ConnectionProfileCatalog load();
    ConnectionProfileCatalog load_or_initialize();
    ConnectionProfileCatalog apply(const std::string& source,std::int64_t expected_revision);
    ConnectionProfileCatalog select(const std::string& id,std::int64_t expected_revision);
private:
    ConnectionProfileCatalog load_unlocked(std::optional<std::string>* encoded=nullptr);
    PersistenceService& store_;
    std::mutex mutex_;
    std::shared_ptr<std::shared_mutex> admission_;
};
}
