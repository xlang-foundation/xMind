#include "agentflow/agent_worker_runner.hpp"
#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/agent_worker_model_gateway.hpp"
#include "agentflow/agent_worker_read_workspace.hpp"
#include "agentflow/agent_worker_closed_ports.hpp"
#include "agentflow/agent_runner.hpp"
#include "agentflow/http_stream_transport.hpp"

namespace agentflow {
AgentWorkerRunResult execute_agent_worker_read_root(AgentWorkerRpcPeer& peer,std::stop_token cancel){
    if(cancel.stop_requested())throw TransportCancelled("Worker execution cancelled before admission");
    const auto admission=AgentWorkerModelClient(peer).admission(cancel);
    if(!admission.owned_request_window)throw Conflict("Worker execution requires backend-owned request windows");
    std::shared_ptr<AgentWorkerOrdinaryJournal> journal=make_agent_worker_ordinary_journal(peer,cancel);
    const auto budget=journal->root_budget(peer.scope().root_id).get();
    if(budget.spec.max_model_calls<1||budget.spec.max_model_calls>32||budget.spec.wall_limit_ms<1||budget.spec.wall_limit_ms>3600000)
        throw Conflict("Worker execution limits are outside ordinary root admission");
    auto workspace=make_agent_worker_read_workspace(peer,journal);
    auto model=make_agent_worker_model_gateway(peer,journal);
    auto closed=make_agent_worker_closed_ports();
    AgentSettings settings;
    settings.provider=agent_worker_model_config(admission);
    settings.max_output_tokens=admission.max_output_tokens;
    settings.instructions.clear();
    settings.max_turns=static_cast<std::size_t>(budget.spec.max_model_calls);
    settings.run_timeout=std::chrono::milliseconds(budget.spec.wall_limit_ms);
    settings.a2a_enabled=false;
    AgentRunner runner(*journal,*closed.context,std::move(settings),
        AgentRunnerPorts{model,closed.catalogue,closed.tools,closed.runtime,workspace});
    auto run=runner.execute(peer.scope().run_id,cancel);
    return {std::move(run),journal->progress()};
}
}
