#pragma once
#include "agentflow/model_provider.hpp"
#include <map>
namespace agentflow {
// Exact documented identities only; account discovery never implies support.
std::map<std::string,Capability> deepseek_documented_tool_policy();
}
