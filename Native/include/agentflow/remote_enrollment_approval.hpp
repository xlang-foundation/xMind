#pragma once
#include "agentflow/run_executor.hpp"
#include "agentflow/persistence_service.hpp"

namespace agentflow {
struct EnrollmentApprovalBinding {
    std::string connection_id,server_id,scope_id;
    std::int64_t profile_revision=0;
    bool operator==(const EnrollmentApprovalBinding&)const=default;
};
enum class EnrollmentEffect:std::uint8_t {read=1,approved_edit=2,approved_process=4};
struct EnrollmentApproval {
    std::string id,workspace_id,authority_id;
    EnrollmentApprovalBinding binding;
    std::int64_t membership_revision=0,revision=1,expires_unix_ms=0;
    std::uint8_t effects=0;
    bool revoked=false;
};
// Backend-private local approval facts. Only an already authorized local owner
// controller may call approve_from_owner; connector grants and peer receipts
// are not accepted here. This journal neither sends requests nor grants effects.
// Public SDK/UI enrollment and execution fencing remain separate work.
class RemoteEnrollmentApprovals {
public:
    RemoteEnrollmentApprovals(PersistenceService&,RunExecutor&,EnrollmentApprovalBinding);
    EnrollmentApproval approve_from_owner(std::string id,std::int64_t expected_membership_revision,
        std::uint8_t effects,std::int64_t expires_unix_ms);
    EnrollmentApproval observe(const std::string& id);
    EnrollmentApproval require_current(const std::string& id,std::uint8_t requested_effects);
    // Commit local revocation before a caller attempts peer notification.
    EnrollmentApproval revoke(const std::string& id,std::int64_t expected_revision);
private:
    PersistenceService& store_;RunExecutor& executor_;EnrollmentApprovalBinding binding_;
};
}
