#pragma once

#include <string>
#include <stop_token>

namespace agentflow {

// Internal entry point for the out-of-process xlang3 tool worker hosted by
// the same xmind executable.
int run_agent_worker(int argc, char** argv);

// Executes one trusted xlang3 callable in a short-lived xmind worker process.
std::string invoke_xlang_python_worker(const std::string& modules_root,
    const std::string& stdlib_root, const std::string& code_root,
    const std::string& module, const std::string& function,
    const std::string& arguments_json, std::stop_token cancel = {},
    const std::string& worker_executable = {});

}
