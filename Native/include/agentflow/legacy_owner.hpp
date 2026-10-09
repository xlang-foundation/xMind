#pragma once
#include "agentflow/backend_owner.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/backend_lease.hpp"
namespace agentflow {
inline constexpr const char* legacy_owner_category="native-legacy-owner";
struct LegacyOwnerSource {std::uint32_t process_id=0;std::string process_birth,server_sha256;};
// Backend-only SQL helpers. No schema writes and no interpreter fallback.
void require_legacy_database_idle(XlangSqlite&);
std::string snapshot_legacy_database(XlangSqlite&);
std::optional<std::string> legacy_owner_record(XlangSqlite&);
void publish_legacy_owner_ticket(XlangSqlite&,const BackendLease&,const LegacyOwnerBootstrap&,
    const LegacyOwnerSource&,const std::string& snapshot);
void require_legacy_owner_bootstrap(XlangSqlite&,const BackendLease&,const LegacyOwnerBootstrap&);
void verify_legacy_owner_saved_records(XlangSqlite&);
void consume_legacy_owner_ticket(XlangSqlite&,const std::string& ticket);
// Shared fixed codec for native target qualification; not a client mutation API.
std::string encode_backend_owner_target(const BackendOwnerTarget&);
BackendOwnerTarget decode_backend_owner_target(const std::string&);
}
