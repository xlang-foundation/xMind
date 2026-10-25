#include "fixture_worker_channel.hpp"
#include "agent_worker_rpc_fixture.hpp"
#include "agent_worker_model_rpc_values_fixture.hpp"
namespace agentflow {
FixtureWorkerRpcHandler bind_agent_worker_model_rpc(AgentWorkerModelHost& host,const AgentWorkerScope& scope){
    const auto& lease=host.lease();if(scope!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Model RPC channel differs from captured lease");
    return [&host](const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel)->std::string{
        if(method!="model.complete"&&method!="model.prepare_request"&&method!="model.admission"&&method!="model.rebuild_request")throw AgentWorkerRpcRemoteError("unsupported");
        try{const auto value=request_envelope(payload);
            if(method=="model.admission"){if(!value.is_object()||!value.empty())throw AgentWorkerRpcRemoteError("rejected");return encode_agent_worker_model_admission(host.admission(cancel));}
            if(method=="model.prepare_request"){if(!value.is_object()||!value.empty())throw AgentWorkerRpcRemoteError("rejected");const auto bytes=encode_agent_model_request(host.prepare_request(cancel));if(bytes.size()>FixtureWorkerChannel::max_message_bytes/2-1024)throw AgentWorkerRpcRemoteError("capacity");return bytes;}
            if(!value.is_object()||value.size()!=2||!value.at("attempt_id").is_string()||!value.at("request").is_string())throw AgentWorkerRpcRemoteError("rejected");const auto attempt=value.at("attempt_id").get<std::string>();if(attempt.empty()||attempt.size()>128||attempt.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos)throw AgentWorkerRpcRemoteError("rejected");
            const auto request=decode_agent_model_request(value.at("request").get<std::string>());if(method=="model.rebuild_request"){const auto bytes=encode_agent_model_request(host.rebuild_request(attempt,request,cancel));if(bytes.size()>FixtureWorkerChannel::max_message_bytes/2-1024)throw AgentWorkerRpcRemoteError("capacity");return bytes;}return encode_agent_model_completion(host.complete(attempt,request,[&](const ModelEvent& event){sink(encode_agent_model_event(event));},cancel));
        }catch(const AgentWorkerRpcRemoteError&){throw;}
         catch(const AgentWorkerModelOutcomeUnrecorded&){throw AgentWorkerRpcRemoteError("outcome_unrecorded");}
         catch(const AgentWorkerRpcCancelled&){throw AgentWorkerRpcRemoteError("cancelled");}
         catch(const TransportCancelled&){throw AgentWorkerRpcRemoteError("cancelled");}
         catch(const TransportTimeout&){throw AgentWorkerRpcRemoteError("timeout");}
         catch(const AgentWorkerAuthorityCancelled&){throw AgentWorkerRpcRemoteError("cancelled");}
         catch(const AgentWorkerAuthorityExpired&){throw AgentWorkerRpcRemoteError("timeout");}
         catch(const ProviderHttpError& error){throw AgentWorkerRpcRemoteError("provider_http",Json{{"status",error.status},{"code",error.status==400&&error.code=="context_length_exceeded"?error.code:std::string{}}}.dump());}
         catch(const Conflict&){throw AgentWorkerRpcRemoteError("rejected");}
         catch(const NotFound&){throw AgentWorkerRpcRemoteError("rejected");}
         catch(const ModelProtocolError&){throw AgentWorkerRpcRemoteError("model_protocol");}
         catch(const std::invalid_argument&){throw AgentWorkerRpcRemoteError("rejected");}
         catch(const Json::exception&){throw AgentWorkerRpcRemoteError("rejected");}
    };
}
}
