#pragma once
#include "agentflow/persistence_service.hpp"
#include <stop_token>

namespace agentflow {
struct PermissionCancelled : std::runtime_error {using std::runtime_error::runtime_error;};
struct PermissionDenied : std::runtime_error {using std::runtime_error::runtime_error;};
struct PermissionExpired : std::runtime_error {using std::runtime_error::runtime_error;};
// Transport-neutral effect authorization. This service never invokes a tool or
// fabricates its result. The caller owns the run and must journal the actual
// effect outcome after the returned executing claim, including uncertainty.
// Once claimed, cancellation also belongs to that caller: check immediately
// before physical dispatch and durably record a known pre-effect stop.
class PermissionWaiter {
public:
    explicit PermissionWaiter(PersistenceService& persistence):persistence_(persistence) {}
    Operation acquire(const std::string& id,const OperationSpec& actual,
        std::int64_t expires_unix_ms,std::stop_token cancel={});
private:
    PersistenceService& persistence_;
};
}
