#pragma once
#include "agentflow/persistence_service.hpp"
#include "agentflow/run_executor.hpp"
#include <optional>
#include <functional>
namespace agentflow {
enum class A2aVersion {legacy,v1,unsupported};
struct A2aStreamStart {bool enabled=false;std::string task_id,context_id,id_json,initial_response;A2aVersion version=A2aVersion::legacy;bool blocking=false;std::size_t history_count=0;};
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
    A2aTaskControl(PersistenceService& persistence,RunExecutor* execution,A2aVersion version=A2aVersion::legacy):store_(persistence),executor_(execution),version_(version){}
    // No response for a valid JSON-RPC notification. Errors never expose input,
    // provider bodies, SQL diagnostics or secrets.
    std::optional<std::string> dispatch(const std::string& source,A2aStreamStart* stream=nullptr,const std::function<bool()>& reserve_stream={});
    std::string snapshot(const A2aStreamStart& start);
private:
    PersistenceService& store_;
    RunExecutor* executor_;
    A2aVersion version_;
};
}
