#include "agentflow/agent_worker_read_workspace.hpp"
#include "agentflow/agent_worker_run_journal.hpp"
#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/agent_worker_read_rpc.hpp"
#include "agentflow/agent_worker_file_rpc.hpp"
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
        provider_identity_=budget.spec.provider_identity_json;
    }
    std::string workspace_id()const override{return scope_.workspace_id;}
    std::int64_t workspace_revision()const override{return scope_.workspace_revision;}
    std::string provider_identity_json()const override{return provider_identity_;}
    ModelRequest prepare_request(std::stop_token cancel)override{return model_.prepare_request(cancel);}
    std::unique_ptr<AgentReadToolHost> read_host()override{return std::make_unique<AgentWorkerReadClient>(peer_);}
private:
    AgentWorkerRpcPeer& peer_;AgentWorkerModelClient model_;std::shared_ptr<AgentWorkerOrdinaryJournal> journal_;AgentWorkerScope scope_;std::string provider_identity_;
};
}
std::shared_ptr<AgentRunnerReadWorkspace> make_agent_worker_read_workspace(AgentWorkerRpcPeer& peer,std::shared_ptr<AgentWorkerOrdinaryJournal> journal){return std::make_shared<WorkerReadWorkspace>(peer,std::move(journal));}
namespace {
class WorkerCodingWorkspace final:public AgentRunnerCodingWorkspace {
    AgentWorkerRpcPeer& peer_;std::shared_ptr<AgentRunnerReadWorkspace> read_;
public:
    WorkerCodingWorkspace(AgentWorkerRpcPeer& peer,std::shared_ptr<AgentWorkerOrdinaryJournal> journal):peer_(peer),read_(make_agent_worker_read_workspace(peer,std::move(journal))){}
    std::string workspace_id()const override{return read_->workspace_id();}
    std::int64_t workspace_revision()const override{return read_->workspace_revision();}
    std::string provider_identity_json()const override{return read_->provider_identity_json();}
    ModelRequest prepare_request(std::stop_token cancel)override{return read_->prepare_request(cancel);}
    std::unique_ptr<AgentReadToolHost> read_host()override{return read_->read_host();}
    std::unique_ptr<AgentFileToolHost> file_host()override{return std::make_unique<AgentWorkerFileClient>(peer_);}
};
}
std::shared_ptr<AgentRunnerCodingWorkspace> make_agent_worker_coding_workspace(AgentWorkerRpcPeer& peer,std::shared_ptr<AgentWorkerOrdinaryJournal> journal){return std::make_shared<WorkerCodingWorkspace>(peer,std::move(journal));}

}
