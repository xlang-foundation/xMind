#pragma once

#include <stop_token>
#include <string>

namespace agentflow {

// Calls trusted, in-process xlang3 Python code. This is an authoring/runtime
// bridge, not a sandbox: callers must only register code from a trusted root.
class XlangPythonCallable {
public:
    XlangPythonCallable(std::string modules_root,std::string stdlib_root,
        std::string code_root,std::string module,std::string function);
    std::string invoke_json(const std::string& arguments_json,std::stop_token cancel={}) const;
private:
    std::string modules_root_,stdlib_root_,code_root_,module_,function_;
};

}
