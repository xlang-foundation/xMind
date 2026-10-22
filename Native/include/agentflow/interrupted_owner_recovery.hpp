#pragma once
#include "agentflow/backend_owner.hpp"

namespace agentflow {
class BackendLease;
class XlangSqlite;
// Internal managed-profile recovery. The caller retains its protected profile
// lock and verified replacement runtime; this never opens model admission.
BackendOwnerState finish_interrupted_backend_retirement(
    XlangSqlite&,const BackendLease&,const BackendOwnerReceipt&,
    const BackendOwnerTarget&,std::uint32_t source_pid,const std::string& source_birth);
}
