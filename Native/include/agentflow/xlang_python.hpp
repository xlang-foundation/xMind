#pragma once

#include "agentflow/model_provider.hpp"
#include <stop_token>
#include <stdexcept>
#include <string>

namespace agentflow {

class XlangPythonCancelled final:public std::runtime_error {
public: XlangPythonCancelled():std::runtime_error("xlang3 tool call cancelled"){}
};
class XlangPythonExecutionError final:public std::runtime_error {
public: XlangPythonExecutionError():std::runtime_error("Trusted xlang3 tool execution failed"){}
};

// Calls trusted, in-process xlang3 Python code. This is an authoring/runtime
// bridge, not a sandbox: callers must only register code from a trusted root.
class XlangPythonCallable {
public:
    XlangPythonCallable(std::string modules_root,std::string stdlib_root,
        std::string code_root,std::string module,std::string function,
        std::string worker_executable={});
    std::string invoke_json(const std::string& arguments_json,std::stop_token cancel={}) const;
    std::string invoke_json_worker(const std::string& arguments_json,std::stop_token cancel={}) const;
private:
    std::string modules_root_,stdlib_root_,code_root_,module_,function_,worker_executable_;
};

enum class XlangPythonExecution { in_process, worker };

struct XlangPythonToolRegistration {
    ModelToolDefinition definition;
    XlangPythonCallable callable;
    XlangPythonExecution execution=XlangPythonExecution::in_process;
};

}
