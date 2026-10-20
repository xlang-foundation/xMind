#include "agentflow/agent_worker_read_workspace.hpp"
#include "agentflow/agent_worker_run_journal.hpp"
#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/agent_worker_read_rpc.hpp"
#include <utility>

namespace agentflow {
namespace {
class WorkerReadWorkspace final:public AgentRunnerReadWorkspace {
public:
    WorkerReadWorkspace(AgentWorkerRpcPeer& peer,std::shared_ptr<AgentWorkerOrdinaryJournal> journal):peer_(peer),model_(peer),journal_(std::move(journal)),scope_(peer.scope()){
        if(!journal_||!journal_->bound_to(peer))throw std::invalid_argument("Worker read workspace requires its exact captured peer and journal");
        if(scope_.workspace_id.empty()||scope_.workspace_id=="workspace:none"||scope_.workspace_revision<=0||scope_.workspace_revision>9007199254740991LL)
            throw std::invalid_argument("Worker read workspace has no captured native workspace");
        const auto budget=journal_->root_budget(scope_.root_id).get();if(budget.spec.workspace_identity!=scope_.workspace_id)throw Conflict("Worker read workspace differs from its root admission");
    }
    std::string workspace_id()const override{return scope_.workspace_id;}
    std::int64_t workspace_revision()const override{return scope_.workspace_revision;}
    ModelRequest prepare_request(std::stop_token cancel)override{return model_.prepare_request(cancel);}
    std::unique_ptr<AgentReadToolHost> read_host()override{return std::make_unique<AgentWorkerReadClient>(peer_);}
private:
    AgentWorkerRpcPeer& peer_;AgentWorkerModelClient model_;std::shared_ptr<AgentWorkerOrdinaryJournal> journal_;AgentWorkerScope scope_;
};
}
std::shared_ptr<AgentRunnerReadWorkspace> make_agent_worker_read_workspace(AgentWorkerRpcPeer& peer,std::shared_ptr<AgentWorkerOrdinaryJournal> journal){return std::make_shared<WorkerReadWorkspace>(peer,std::move(journal));}
}
