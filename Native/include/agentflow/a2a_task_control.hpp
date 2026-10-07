#pragma once
#include "agentflow/persistence_service.hpp"
#include "agentflow/run_executor.hpp"
#include <optional>
#include <functional>
namespace agentflow {
struct A2aStreamStart {bool enabled=false;std::string task_id,context_id,id_json,initial_response;};
struct A2aStreamBatch {std::vector<std::string> responses;bool final=false;};
// Stream state is per viewer. Domain execution and cancellation remain shared.
class A2aTaskStream {
public:
    A2aTaskStream(PersistenceService& store,const A2aStreamStart& start);
    ~A2aTaskStream();
    A2aStreamBatch poll();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Authenticated access adapters call this boundary. It uses actual shared
// execution records and services; no protocol call can set lifecycle states.
// Native text admission and streaming use shared execution and persistence.
class A2aTaskControl {
public:
    A2aTaskControl(PersistenceService& persistence,RunExecutor* execution):store_(persistence),executor_(execution){}
    // No response for a valid JSON-RPC notification. Errors never expose input,
    // provider bodies, SQL diagnostics or secrets.
    std::optional<std::string> dispatch(const std::string& source,A2aStreamStart* stream=nullptr,const std::function<bool()>& reserve_stream={});
private:
    PersistenceService& store_;
    RunExecutor* executor_;
};
}
