#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/agent_worker_native_model.hpp"
namespace agentflow {
AgentWorkerServices& NativeWorkerContext::services(){return *client_;}
AgentWorkerModelAdmission AgentWorkerModelClient::admission(std::stop_token cancel){return peer_.services().admission(cancel);}
ModelRequest AgentWorkerModelClient::prepare_request(std::stop_token cancel){return peer_.services().prepare_request(cancel);}
ModelRequest AgentWorkerModelClient::rebuild_request(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){return peer_.services().rebuild_request(attempt,request,cancel);}
ModelCompletion AgentWorkerModelClient::complete(const std::string& attempt,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel){return peer_.services().complete(attempt,request,std::move(sink),cancel);}
}
