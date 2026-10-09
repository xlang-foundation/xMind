#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <optional>
#include "agentflow/runtime_generation.hpp"

namespace agentflow {
struct BackendOwnerPrecondition {std::string generation;std::int64_t revision=0;};
struct BackendOwnerReceipt {std::string generation,receipt_id;std::int64_t revision=0;bool operator==(const BackendOwnerReceipt&)const=default;};
// Produced by native runtime/workspace qualification, not by a view's labels.
struct BackendOwnerTarget {
    RuntimeGenerationBinding runtime;
    std::string workspace_root,workspace_id,auth_binding;
    bool approved_edits=false;
    bool operator==(const BackendOwnerTarget&)const=default;
};
struct BackendOwnerBootstrap {BackendOwnerReceipt receipt;BackendOwnerTarget target;};
// A native operator ticket published only after observing legacy process exit.
// This is distinct from a receipt issued by live native quiescence.
struct LegacyOwnerBootstrap {std::string ticket_id;BackendOwnerTarget target;};
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
    std::optional<BackendOwnerTarget> replacement_target;
    bool replacement_prepared=false;
    std::optional<BackendOwnerReceipt> replacement_source;
    std::string database_path;
    std::string legacy_ticket_id;
};
struct BackendQuiesced : std::runtime_error {using std::runtime_error::runtime_error;};
}
