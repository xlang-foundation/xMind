#pragma once
#include "agentflow/agent_worker_rpc.hpp"
#include "agentflow/agent_worker_model_host.hpp"

namespace agentflow {
class AgentWorkerModelClient {
public:
    explicit AgentWorkerModelClient(AgentWorkerRpcPeer& peer):peer_(peer){}
    ModelCompletion complete(const std::string& attempt,const ModelRequest&,ChatCompletionStream::Sink,std::stop_token={});
    ModelRequest prepare_request(std::stop_token={});
    AgentWorkerModelAdmission admission(std::stop_token={});
private:AgentWorkerRpcPeer& peer_;
};
// Bind this one authorized inference host to its exact authenticated channel.
// No dynamic service lookup, provider config, credentials or SQL is exposed.
AgentWorkerRpcHandler bind_agent_worker_model_rpc(AgentWorkerModelHost&,const AgentWorkerScope&);
}
