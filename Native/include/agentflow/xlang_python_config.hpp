#pragma once
#include "agentflow/persistence_service.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace agentflow {

struct XlangPythonToolDefinition {
    std::string name,description,input_schema_json,module,function;
};
struct XlangPythonToolConfig {
    std::string code_root;
    std::vector<XlangPythonToolDefinition> tools;
};

// Parses an explicitly supplied trusted JSON/YAML startup snapshot. This does
// not load code or grant workspace effect permissions by itself.
XlangPythonToolConfig parse_xlang_python_tool_config(std::string_view source);
XlangPythonToolConfig load_xlang_python_tool_config(PersistenceService& store);
XlangPythonToolConfig import_xlang_python_tool_config(PersistenceService& store,std::string_view source);

}
