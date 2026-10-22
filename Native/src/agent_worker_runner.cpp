#include "agentflow/agent_worker_runner.hpp"
#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/agent_worker_model_gateway.hpp"
#include "agentflow/agent_worker_read_workspace.hpp"
#include "agentflow/agent_worker_closed_ports.hpp"
#include "agentflow/agent_runner.hpp"
#include "agentflow/root_execution_budget.hpp"
#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/http_stream_transport.hpp"

namespace agentflow {
static AgentWorkerRunResult execute_workspace_root(AgentWorkerClientContext& peer,std::stop_token cancel,bool coding){
    if(cancel.stop_requested())throw TransportCancelled("Worker execution cancelled before admission");
    const auto admission=AgentWorkerModelClient(peer).admission(cancel);
    if(!admission.owned_request_window)throw Conflict("Worker execution requires backend-owned request windows");
    std::shared_ptr<AgentWorkerOrdinaryJournal> journal=coding?make_agent_worker_coding_journal(peer,cancel):make_agent_worker_ordinary_journal(peer,cancel);
    const auto budget=journal->root_budget(peer.scope().root_id).get();
    if(budget.spec.max_model_calls<1||budget.spec.max_model_calls>32||budget.spec.wall_limit_ms<1||budget.spec.wall_limit_ms>3600000)
        throw Conflict("Worker execution limits are outside ordinary root admission");
    auto workspace=coding?std::shared_ptr<AgentRunnerReadWorkspace>{}:make_agent_worker_read_workspace(peer,journal);
    auto coding_workspace=coding?make_agent_worker_coding_workspace(peer,journal):std::shared_ptr<AgentRunnerCodingWorkspace>{};
    auto model=make_agent_worker_model_gateway(peer,journal);
    auto closed=make_agent_worker_closed_ports();
    AgentSettings settings;
    settings.provider=agent_worker_model_config(admission);
    settings.max_output_tokens=admission.max_output_tokens;
    settings.instructions.clear();
    const bool leaf=peer.scope().root_id!=peer.scope().run_id;
    if(leaf&&coding&&budget.spec.policy_id!="native.graph-context")throw Conflict("Delegated worker cannot acquire coding assembly");
    settings.max_turns=leaf&&budget.spec.policy_id!="native.graph-context"?4:static_cast<std::size_t>(budget.spec.max_model_calls);
    settings.run_timeout=std::chrono::milliseconds(budget.spec.wall_limit_ms);
    settings.a2a_enabled=false;
    AgentRunner runner(*journal,*closed.context,std::move(settings),
        AgentRunnerPorts{model,closed.catalogue,closed.tools,closed.runtime,workspace,coding_workspace});
    std::shared_ptr<RootExecutionBudget> shared;
    if(leaf)shared=std::make_shared<RootExecutionBudget>(*journal,budget,AgentWorkerJournalClient(peer).execution_deadline(),cancel);
    auto run=runner.execute(peer.scope().run_id,cancel,{},std::move(shared));
    return {std::move(run),journal->progress()};
}
AgentWorkerRunResult execute_agent_worker_read_root(AgentWorkerClientContext& peer,std::stop_token cancel){return execute_workspace_root(peer,cancel,false);}
AgentWorkerRunResult execute_agent_worker_coding_root(AgentWorkerClientContext& peer,std::stop_token cancel){return execute_workspace_root(peer,cancel,true);}

}
