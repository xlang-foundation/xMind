#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace agentflow {
// Database-owner lifecycle, not a model tool or a workspace permission.
// The authenticated transport must also validate the actual workspace authority
// and runtime health/idle state before admitting quiescence.
struct BackendOwnerState {
    std::string generation;
    std::int64_t revision=0;
    bool quiesced=false;
    std::string receipt_id;
    // A durable request, not evidence that the process has exited. Consuming
    // quiescence for retirement prevents the old generation from resuming.
    bool retirement_requested=false;
};
struct BackendOwnerPrecondition {std::string generation;std::int64_t revision=0;};
struct BackendOwnerReceipt {std::string generation,receipt_id;std::int64_t revision=0;};
struct BackendQuiesced : std::runtime_error {using std::runtime_error::runtime_error;};
}
