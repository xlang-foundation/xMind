#pragma once
#include "agentflow/persistence_service.hpp"
#include "agentflow/run_executor.hpp"
#include <optional>
namespace agentflow {
// Authenticated access adapters call this boundary. It uses actual shared
// execution records and services; no protocol call can set lifecycle states.
// Native message admission, discovery and streaming are subsequent components.
class A2aTaskControl {
public:
    A2aTaskControl(PersistenceService& persistence,RunExecutor* execution):store_(persistence),executor_(execution){}
    // No response for a valid JSON-RPC notification. Errors never expose input,
    // provider bodies, SQL diagnostics or secrets.
    std::optional<std::string> dispatch(const std::string& source);
private:
    PersistenceService& store_;
    RunExecutor* executor_;
};
}
