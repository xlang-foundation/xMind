#pragma once
#include <cstdint>
#include <string>
namespace agentflow {
struct OwnerProcessObservation {bool exited=false;bool identity_matches=false;};
// Read-only OS observation of the PID/creation-time pair issued by Native.
// Never signals or terminates a process; PID reuse means the original exited.
OwnerProcessObservation observe_owner_exit(std::uint32_t pid,const std::string& birth,std::uint32_t timeout_ms);
}
