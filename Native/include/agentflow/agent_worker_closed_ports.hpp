#pragma once
#include "agentflow/agent_context_journal.hpp"
#include "agentflow/agent_backend_catalog.hpp"
#include "agentflow/agent_tool_host_factory.hpp"
#include "agentflow/agent_backend_runtime.hpp"

namespace agentflow {
// Explicit absence of child-local context maintenance, catalogue access and
// native effects. All operations throw; none return empty facts or acknowledge
// writes. Admitted model/read proxies are supplied separately by their factories.
struct AgentWorkerClosedPorts {
    std::unique_ptr<AgentContextJournal> context;
    std::shared_ptr<AgentBackendCatalog> catalogue;
    std::shared_ptr<AgentToolHostFactory> tools;
    std::shared_ptr<AgentBackendRuntime> runtime;
};
AgentWorkerClosedPorts make_agent_worker_closed_ports();
}
