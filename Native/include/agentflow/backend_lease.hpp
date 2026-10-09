#pragma once
#include <memory>
#include <string>

namespace agentflow {
// OS-held lease, not a PID file. The sidecar remains after normal exit/crash.
// Hold this for the entire backend lifetime; acquire before startup recovery.
class BackendLease {
public:
    explicit BackendLease(const std::string& database_path);
    ~BackendLease();
    BackendLease(const BackendLease&) = delete;
    BackendLease& operator=(const BackendLease&) = delete;
    bool covers(const std::string& database_path) const;
    std::string canonical_database_path()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
