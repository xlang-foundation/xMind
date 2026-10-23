#pragma once
#include "agentflow/agent_worker_client_context.hpp"
#include "agentflow/agent_worker_model_host.hpp"

namespace agentflow {
class AgentWorkerModelClient {
public:
    explicit AgentWorkerModelClient(AgentWorkerClientContext& peer):peer_(peer){}
    ModelCompletion complete(const std::string& attempt,const ModelRequest&,ChatCompletionStream::Sink,std::stop_token={});
    ModelRequest prepare_request(std::stop_token={});
    ModelRequest rebuild_request(const std::string&,const ModelRequest&,std::stop_token={});
    AgentWorkerModelAdmission admission(std::stop_token={});
private:AgentWorkerClientContext& peer_;
};

}
