#pragma once

#include <string>

namespace agentflow {

// Output collected from one explicitly trusted xlang3 authoring program.
// A null catalog means that the program did not declare that catalog.
struct XlangAuthoringProgramOutput {
    std::string agents_json;
    std::string graphs_json;
};

// Executes configure(xmind) from a trusted Python module under xlang3. The
// xmind argument is a native API exposing agent(definition) and
// graph(definition); it cannot access persistence or effect authorities.
XlangAuthoringProgramOutput run_xlang_authoring_program(
    const std::string& modules_root,const std::string& stdlib_root,
    const std::string& code_root,const std::string& module,
    const std::string& function="configure");

}
