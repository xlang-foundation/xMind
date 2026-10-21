#include "agentflow/agent_worker_read_workspace.hpp"
#include "agentflow/agent_worker_run_journal.hpp"
#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/agent_worker_read_rpc.hpp"
#include "agentflow/agent_worker_file_rpc.hpp"
#include "agentflow/agent_worker_process_rpc.hpp"
#include "agentflow/agent_worker_remote_rpc.hpp"
#include "agentflow/agent_worker_callable_rpc.hpp"
#include "agentflow/process_executor.hpp"
#include "nlohmann/json.hpp"
#include <utility>
#include <algorithm>

namespace agentflow {
namespace {
class WorkerReadWorkspace final:public AgentRunnerReadWorkspace {
public:
    WorkerReadWorkspace(AgentWorkerRpcPeer& peer,std::shared_ptr<AgentWorkerOrdinaryJournal> journal):peer_(peer),model_(peer),journal_(std::move(journal)),scope_(peer.scope()){
        if(!journal_||!journal_->bound_to(peer))throw std::invalid_argument("Worker read workspace requires its exact captured peer and journal");
        if(scope_.workspace_id.empty()||scope_.workspace_id=="workspace:none"||scope_.workspace_revision<=0||scope_.workspace_revision>9007199254740991LL)
            throw std::invalid_argument("Worker read workspace has no captured native workspace");
        const auto budget=journal_->root_budget(scope_.root_id).get();if(budget.spec.workspace_identity!=scope_.workspace_id)throw Conflict("Worker read workspace differs from its root admission");
        provider_identity_=budget.spec.provider_identity_json;policy_id_=budget.spec.policy_id;
        if(scope_.root_id!=scope_.run_id){auto value=nlohmann::json::parse(provider_identity_);value["model_id"]=model_.admission().model_id;provider_identity_=value.dump();}
    }
    std::string workspace_id()const override{return scope_.workspace_id;}
    std::int64_t workspace_revision()const override{return scope_.workspace_revision;}
    std::string provider_identity_json()const override{return provider_identity_;}
    std::string execution_policy_id()const override{return policy_id_;}
    ModelRequest prepare_request(std::stop_token cancel)override{return model_.prepare_request(cancel);}
    ModelRequest rebuild_request(const std::string& attempt,const ModelRequest& request,std::stop_token cancel)override{return model_.rebuild_request(attempt,request,cancel);}
    std::unique_ptr<AgentReadToolHost> read_host()override{return std::make_unique<AgentWorkerReadClient>(peer_);}
private:
    AgentWorkerRpcPeer& peer_;AgentWorkerModelClient model_;std::shared_ptr<AgentWorkerOrdinaryJournal> journal_;AgentWorkerScope scope_;std::string provider_identity_,policy_id_;
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
    std::string execution_policy_id()const override{return read_->execution_policy_id();}
    ModelRequest prepare_request(std::stop_token cancel)override{return read_->prepare_request(cancel);}
    ModelRequest rebuild_request(const std::string& attempt,const ModelRequest& request,std::stop_token cancel)override{return read_->rebuild_request(attempt,request,cancel);}
    std::unique_ptr<AgentReadToolHost> read_host()override{return read_->read_host();}
    std::unique_ptr<AgentFileToolHost> file_host()override{return std::make_unique<AgentWorkerFileClient>(peer_);}
    std::unique_ptr<AgentProcessOwnerHost> process_host()override{return std::make_unique<AgentWorkerProcessClient>(peer_);}
    std::unique_ptr<AgentRemoteToolHost> remote_host()override{return std::make_unique<AgentWorkerRemoteClient>(peer_);}
    std::unique_ptr<AgentCallableToolHost> callable_host()override{return std::make_unique<AgentWorkerCallableClient>(peer_);}
    std::vector<std::string> callable_tool_names()override{const auto result=nlohmann::json::parse(peer_.call("callable.catalogue","{}"));if(!result.is_array()||result.size()>64)throw ModelProtocolError("Invalid worker callable catalogue");std::vector<std::string> names;for(const auto& entry:result){if(!entry.is_string())throw ModelProtocolError("Invalid worker callable alias");const auto name=entry.get<std::string>();if(name.empty()||name.size()>128||name.find('\0')!=std::string::npos||std::find(names.begin(),names.end(),name)!=names.end())throw ModelProtocolError("Invalid worker callable alias");names.push_back(name);}return names;}
    bool join_background(const std::string& attempt,std::stop_token cancel)override{
        try{const auto value=nlohmann::json::parse(peer_.call("process.join",nlohmann::json{{"attempt_id",attempt}}.dump(),{},cancel));
            if(!value.is_object()||value.size()!=1||!value.at("joined").is_boolean())throw ModelProtocolError("Invalid worker process join result");return value.at("joined").get<bool>();}
        catch(const AgentWorkerRpcCancelled&){throw ToolCancelled("Worker process join cancelled");}
        catch(const AgentWorkerRpcTimeout&){throw ToolCancelled("Worker process join deadline elapsed");}
        catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="cancelled"||error.code()=="timeout")throw ToolCancelled("Worker process join cancelled or expired");if(error.code()=="rejected")throw Conflict("Worker process join rejected its captured attempt");if(error.code()=="outcome_unrecorded")throw ProcessOutcomeUnrecorded("Worker process join observation is unrecorded; no retry is authorized");throw ModelProtocolError("Worker process join failed");}
        catch(const nlohmann::json::exception&){throw ModelProtocolError("Invalid worker process join result");}
    }
};
}
std::shared_ptr<AgentRunnerCodingWorkspace> make_agent_worker_coding_workspace(AgentWorkerRpcPeer& peer,std::shared_ptr<AgentWorkerOrdinaryJournal> journal){return std::make_shared<WorkerCodingWorkspace>(peer,std::move(journal));}

}
