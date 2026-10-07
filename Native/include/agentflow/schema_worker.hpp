#pragma once
#include "agentflow/json_schema.hpp"
#include "agentflow/mcp_stdio.hpp"

namespace agentflow {
struct SchemaEvaluationFailure : std::runtime_error {using std::runtime_error::runtime_error;};
struct SchemaEvaluationCancelled : SchemaEvaluationFailure {using SchemaEvaluationFailure::SchemaEvaluationFailure;};
struct SchemaWorkerExecutable {
    std::string executable,working_directory;
    std::vector<std::string> arguments;
};
// Private native IPC adapter, not an MCP server or model/view command route.
// The default executable is the product's adjacent xmind_schema_worker.exe.
// Each evaluation gets a fresh owned child: 128 MiB committed memory, two
// seconds user CPU, one process, and an independent two-second wall deadline.
// Overrides belong to trusted backend/test configuration only.
class SchemaWorker {
public:
    explicit SchemaWorker(std::optional<SchemaWorkerExecutable> executable={});
    void evaluate(const std::string& schema,std::optional<std::string> instance,
        McpStdioProcess::Deadline deadline,std::stop_token cancel={}) const;
private:
    SchemaWorkerExecutable executable_;
};
}
