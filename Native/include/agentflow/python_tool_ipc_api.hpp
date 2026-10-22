#pragma once
#include "xlang3/xlang3.h"
#include <string>

namespace agentflow {
// A process-local trusted callable, exposed under the common xmind root.
// Open returns a capability-bound SDK object; values use SDK IPC serialization.
struct PythonToolsApi {
    X::Value Open(std::string capability);
    BEGIN_PACKAGE(PythonToolsApi)
        APISET().AddFunc<1>("Open",&PythonToolsApi::Open);
    END_PACKAGE
};
}
