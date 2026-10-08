#pragma once
#include "agentflow/model_provider.hpp"
#include <map>

namespace agentflow {
// Backend capability declarations from the dated official model references in
// doc/evidence/google-gemini-tool-policy-reference.json. This is not an account
// catalogue, a live acceptance claim or a wildcard family/alias inference.
std::map<std::string,Capability> gemini_documented_tool_policy();
}
