#include "agentflow/agent_worker_model_gateway.hpp"
#include "agentflow/agent_worker_run_journal.hpp"
#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "nlohmann/json.hpp"
#include <atomic>
#include <utility>

namespace agentflow {
namespace {
const char* wire_name(ProviderWire wire){switch(wire){case ProviderWire::chat_completions:return "chat-completions";case ProviderWire::responses:return "responses";case ProviderWire::anthropic_messages:return "anthropic-messages";case ProviderWire::gemini_generate_content:return "gemini-generate-content";}throw std::invalid_argument("Invalid worker provider wire");}
class WorkerModelGateway final:public AgentModelGateway {
public:
    WorkerModelGateway(AgentWorkerRpcPeer& peer,std::shared_ptr<AgentWorkerOrdinaryJournal> journal):client_(peer),journal_(std::move(journal)){
        if(!journal_||!journal_->bound_to(peer))throw std::invalid_argument("Worker model gateway requires its exact captured peer and journal");
        const auto budget=journal_->root_budget(peer.scope().root_id).get();auto identity=nlohmann::json::parse(budget.spec.provider_identity_json);
        if(peer.scope().root_id!=peer.scope().run_id)identity["model_id"]=AgentWorkerModelClient(peer).admission().model_id;
        model_=identity.at("model_id").get<std::string>();wire_=identity.at("wire").get<std::string>();
        if(model_.empty()||model_.size()>1024||model_.find_first_of("\r\n")!=std::string::npos||model_.find('\0')!=std::string::npos||
           (wire_!="chat-completions"&&wire_!="responses"&&wire_!="anthropic-messages"&&wire_!="gemini-generate-content"))throw std::invalid_argument("Worker public model identity is invalid");
    }
    ModelCompletion complete(const ChatProviderConfig& config,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel)override{
        if(config.endpoint!="worker://captured"||config.model!=model_||wire_name(config.wire)!=wire_)throw Conflict("Worker model config differs from captured public routing");
        if(cancel.stop_requested())throw TransportCancelled("Worker model cancelled before dispatch");
        if(active_.test_and_set(std::memory_order_acquire))throw Conflict("Worker model gateway already owns a dispatch");
        struct Release {std::atomic_flag& active;~Release(){active.clear(std::memory_order_release);}} release{active_};
        const auto attempt=journal_->progress().current_attempt;if(attempt.empty())throw Conflict("Worker model has no acknowledged journal attempt");
        // No endpoint, config, credential or worker deadline crosses this call.
        return client_.complete(attempt,request,std::move(sink),cancel);
    }
    ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::logic_error("Worker context counting capability is not admitted");}
    ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::logic_error("Worker context compaction capability is not admitted");}
private:
    AgentWorkerModelClient client_;std::shared_ptr<AgentWorkerOrdinaryJournal> journal_;std::string model_,wire_;std::atomic_flag active_=ATOMIC_FLAG_INIT;
};
}
std::shared_ptr<AgentModelGateway> make_agent_worker_model_gateway(AgentWorkerRpcPeer& peer,std::shared_ptr<AgentWorkerOrdinaryJournal> journal){return std::make_shared<WorkerModelGateway>(peer,std::move(journal));}
}
