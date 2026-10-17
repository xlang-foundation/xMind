#include "agentflow/agent_model_gateway.hpp"
#include "agentflow/persistence_service.hpp"
namespace agentflow {
namespace {
class PersistenceAgentModelGateway final:public AgentModelGateway {
public:
    PersistenceAgentModelGateway(PersistenceService& persistence,std::optional<CredentialReference> credential)
        :persistence_(persistence),credential_(std::move(credential)){}
    ModelCompletion complete(const ChatProviderConfig& config,const ModelRequest& request,
        ChatCompletionStream::Sink sink,std::stop_token cancel)override{
        auto secret=resolve();return complete_model(config,request,secret?&*secret:nullptr,std::move(sink),cancel);
    }
    ResponsesTokenCount count_context(const ChatProviderConfig& config,const ModelRequest& request,
        std::stop_token cancel)override{
        auto secret=resolve();return count_responses_context(config,request,secret?&*secret:nullptr,cancel);
    }
    ResponsesCompactionResult compact_context(const ChatProviderConfig& config,const ModelRequest& request,
        std::stop_token cancel)override{
        auto secret=resolve();return compact_responses_context(config,request,secret?&*secret:nullptr,cancel);
    }
private:
    std::optional<SecretBytes> resolve(){
        if(!credential_)return {};
        return persistence_.resolve_credential(credential_->scope,credential_->id,credential_->purpose).get();
    }
    PersistenceService& persistence_;
    std::optional<CredentialReference> credential_;
};
}
std::shared_ptr<AgentModelGateway> make_persistence_agent_model_gateway(
    PersistenceService& persistence,std::optional<CredentialReference> credential){
    return std::make_shared<PersistenceAgentModelGateway>(persistence,std::move(credential));
}
}
