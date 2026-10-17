#pragma once
#include "agentflow/model_provider.hpp"
#include "agentflow/responses_context.hpp"
#include <memory>

namespace agentflow {
class PersistenceService;
struct CredentialReference {std::string scope,id,purpose;};
// Backend-owned model calls over bounded native values. Implementations keep
// credential resolution outside AgentRunner and any future worker process.
class AgentModelGateway {
public:
    virtual ~AgentModelGateway()=default;
    virtual ModelCompletion complete(const ChatProviderConfig&,const ModelRequest&,
        ChatCompletionStream::Sink,std::stop_token)=0;
    virtual ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,
        std::stop_token)=0;
    virtual ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,
        std::stop_token)=0;
};
std::shared_ptr<AgentModelGateway> make_persistence_agent_model_gateway(
    PersistenceService&,std::optional<CredentialReference>);
}
