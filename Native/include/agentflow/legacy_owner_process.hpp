#pragma once
#include "agentflow/legacy_owner.hpp"
#include "agentflow/secret_protection.hpp"
#include <memory>
namespace agentflow {
struct LegacyOwnerProcessObservation {
    LegacyOwnerSource source;
    std::string image_path,database_path;
};
// Read-only discovery input. Not an authenticated migration authorization.
LegacyOwnerProcessObservation discover_legacy_listener(std::uint16_t port);
// Read-only preflight for an operator-owned legacy listener. Retains actual
// process/image/database handles; neither signals a process nor creates a ticket.
class VerifiedLegacyOwnerProcess {
public:
    VerifiedLegacyOwnerProcess(std::uint16_t port,const std::string& image,
        const std::string& image_sha256,const std::string& database,
        const std::string& workspace,const std::string& workspace_id,
        const std::string& authority_id,const SecretBytes& token);
    ~VerifiedLegacyOwnerProcess();
    VerifiedLegacyOwnerProcess(const VerifiedLegacyOwnerProcess&)=delete;
    VerifiedLegacyOwnerProcess& operator=(const VerifiedLegacyOwnerProcess&)=delete;
    const LegacyOwnerProcessObservation& observation()const;
    void revalidate()const;
    // Explicit operator operation. Acquires a native SQLite writer fence,
    // checks all durable idle owners, revalidates, stops the exact process,
    // observes exit and publishes the ticket under the canonical backend lease.
    void stop_and_prepare(const LegacyOwnerBootstrap&,const std::vector<std::string>& import_roots);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
