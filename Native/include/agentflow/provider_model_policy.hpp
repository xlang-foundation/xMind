#pragma once
#include "agentflow/model_provider.hpp"
#include <map>
#include <set>

namespace agentflow {
// A backend-owned declaration for native streamed text execution on one wire.
// Account catalogue entries and views never supply these capabilities.
struct NativeModelCapabilities {
    Capability tools=Capability::unknown;
    Capability stream_usage=Capability::unknown;
    Capability output_limit=Capability::unknown;
    std::set<ReasoningEffort> reasoning_efforts;
    std::optional<ReasoningEffort> tool_reasoning_effort;
};
struct ProviderModelPolicy {
    ProviderWire wire=ProviderWire::chat_completions;
    std::map<std::string,NativeModelCapabilities> models;
};
// Exact documented aliases/snapshots only. Unknown identities require a later
// backend policy declaration; a naming prefix does not grant capabilities.
ProviderModelPolicy documented_openai_model_policy(ProviderWire wire);
void validate_provider_model_policy(const ProviderModelPolicy& policy);
// Rejects before credentials, publication or run admission. Preserves the
// backend's endpoint and explicit reasoning choice; never switches wires.
ChatProviderConfig bind_provider_model_policy(const ProviderModelPolicy& policy,
    ChatProviderConfig provider,const std::string& model,bool require_tools);
}
