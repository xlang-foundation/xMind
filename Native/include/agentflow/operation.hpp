#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
#include <vector>
#include "agentflow/records.hpp"

namespace agentflow {
enum class OperationState {awaiting_approval,ready,denied,expired,cancelled,executing,succeeded,failed,uncertain};
inline std::string to_string(OperationState state) {
    switch(state) {
    case OperationState::awaiting_approval:return "awaiting_approval";
    case OperationState::ready:return "ready";case OperationState::denied:return "denied";
    case OperationState::expired:return "expired";case OperationState::cancelled:return "cancelled";
    case OperationState::executing:return "executing";case OperationState::succeeded:return "succeeded";
    case OperationState::failed:return "failed";case OperationState::uncertain:return "uncertain";
    }
    throw std::invalid_argument("Invalid operation state");
}
enum class OperationDecision {allow,deny};
struct WorkspaceEffectBusy : Conflict {using Conflict::Conflict;};
struct WorkspaceEffectUncertain : Conflict {using Conflict::Conflict;};
// Supplied by the owning native runtime, never trusted from a view or model.
// Workspace is the runtime's verified workspace identity. Arguments are an
// exact JSON object, retained byte for byte. Changing serialization requires a
// new operation; effect adapters must execute the approved payload unchanged.
struct OperationSpec {
    std::string run_id,workspace,tool,arguments_json;
    // Additional trusted effect domains (for example a configured MCP server).
    // Revision changes do not release an unresolved domain. Views/models never
    // choose these keys. The verified workspace domain is included implicitly.
    std::vector<std::string> resources;
};
struct Operation {
    std::string id;
    OperationSpec spec;
    OperationState state;
    std::int64_t expires_unix_ms;
    std::string result_json;
    std::string decision_actor;
};
}
