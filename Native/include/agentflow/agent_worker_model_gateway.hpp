#pragma once
#include "agentflow/agent_model_gateway.hpp"
#include "agentflow/agent_worker_client_context.hpp"

namespace agentflow {
class AgentWorkerOrdinaryJournal;
// Ordinary worker model calls through the exact peer/journal pair. Credentials,
// endpoint, provider policy and deadlines remain with the backend. The local
// config must use worker://captured and the admitted public model/wire. Count
// and compaction require separate capabilities and currently reject explicitly.
// The peer must outlive the gateway and its retained journal.
std::shared_ptr<AgentModelGateway> make_agent_worker_model_gateway(
    AgentWorkerClientContext&,std::shared_ptr<AgentWorkerOrdinaryJournal>);
}
