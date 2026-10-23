#include "agent_worker_rpc_fixture.hpp"
#include "agent_worker_model_rpc_values_fixture.hpp"
namespace agentflow {
AgentWorkerModelAdmission FixtureWorkerRpcPeer::admission(std::stop_token cancel){
    try{return decode_agent_worker_model_admission(this->call("model.admission","{}",{},cancel));}
    catch(const AgentWorkerRpcCancelled&){throw TransportCancelled("Worker model admission RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw TransportTimeout("Worker model admission RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="cancelled")throw TransportCancelled("Worker model admission cancelled");if(error.code()=="timeout")throw TransportTimeout("Worker model admission deadline elapsed");if(error.code()=="rejected")throw Conflict("Worker model admission rejected its captured owner");throw ModelProtocolError("Worker model admission capability unavailable");}
}
ModelRequest FixtureWorkerRpcPeer::prepare_request(std::stop_token cancel){
    try{return decode_agent_model_request(this->call("model.prepare_request","{}",{},cancel));}
    catch(const AgentWorkerRpcCancelled&){throw TransportCancelled("Worker context RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw TransportTimeout("Worker context RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker context RPC rejected its captured owner");if(error.code()=="cancelled")throw TransportCancelled("Worker context owner cancelled");if(error.code()=="timeout")throw TransportTimeout("Worker context deadline elapsed");throw;}
}
ModelRequest FixtureWorkerRpcPeer::rebuild_request(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){
    try{return decode_agent_model_request(this->call("model.rebuild_request",Json{{"attempt_id",attempt},{"request",encode_agent_model_request(request)}}.dump(),{},cancel));}
    catch(const AgentWorkerRpcCancelled&){throw TransportCancelled("Worker context rebuild RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw TransportTimeout("Worker context rebuild RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker context rebuild rejected its captured owner");if(error.code()=="cancelled")throw TransportCancelled("Worker context rebuild owner cancelled");if(error.code()=="timeout")throw TransportTimeout("Worker context rebuild deadline elapsed");throw;}
}
ModelCompletion FixtureWorkerRpcPeer::complete(const std::string& attempt,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel){
    if(!sink)throw std::invalid_argument("Worker model RPC requires its event sink");
    try{return decode_agent_model_completion(this->call("model.complete",Json{{"attempt_id",attempt},{"request",encode_agent_model_request(request)}}.dump(),[&](const std::string& event){sink(decode_agent_model_event(event));},cancel));}
    catch(const AgentWorkerRpcCancelled&){throw TransportCancelled("Worker model RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw TransportTimeout("Worker model RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="cancelled")throw TransportCancelled("Worker model RPC cancelled");if(error.code()=="timeout")throw TransportTimeout("Worker model RPC deadline elapsed");if(error.code()=="rejected")throw Conflict("Worker model RPC rejected its captured scope");if(error.code()=="outcome_unrecorded")throw AgentWorkerModelOutcomeUnrecorded("Worker model acknowledgement is unrecorded");if(error.code()=="provider_http")throw provider_failure(error.detail());throw ModelProtocolError("Worker model RPC failed");}
}
}
