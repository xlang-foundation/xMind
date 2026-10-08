#pragma once
#include "agentflow/context_manager.hpp"

namespace agentflow {
// Backend-owned, dated capacity facts from official model documentation.
// Profile/destination identities are bound only by ProviderProfileRuntime.
ContextRuntimePolicy documented_openai_context_policy();
}
