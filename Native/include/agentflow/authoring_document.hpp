#pragma once
#include <string>
#include <string_view>

namespace agentflow {

// Convert one bounded, alias-free YAML document to JSON before the native
// definition-specific validator runs. JSON inputs keep their existing parser.
std::string authoring_yaml_to_json(std::string_view source);

}
