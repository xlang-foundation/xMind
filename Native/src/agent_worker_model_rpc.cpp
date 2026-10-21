#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agentflow/records.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
using Json=nlohmann::json;
namespace {
Json request_envelope(const std::string& bytes){if(bytes.empty()||bytes.size()>AgentWorkerFrameCodec::max_frame_bytes)throw AgentWorkerRpcRemoteError("rejected");std::vector<std::set<std::string>> keys;return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){if(depth>8)throw AgentWorkerRpcRemoteError("rejected");if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key&&(keys.empty()||!keys.back().insert(value.get<std::string>()).second))throw AgentWorkerRpcRemoteError("rejected");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});}
ProviderHttpError provider_failure(const std::string& bytes){
    try{
        const auto value=request_envelope(bytes);
        if(!value.is_object()||value.size()!=2||!value.at("status").is_number_integer()||!value.at("code").is_string()||value.at("status")<100||value.at("status")>599)throw ModelProtocolError("Invalid worker provider failure");
        ProviderHttpError result(value.at("status").get<int>());result.code=value.at("code").get<std::string>();
        if(!result.code.empty()&&(result.status!=400||result.code!="context_length_exceeded"))throw ModelProtocolError("Invalid worker provider failure");
        return result;
    }catch(const Json::exception&){throw ModelProtocolError("Invalid worker provider failure");}
     catch(const AgentWorkerRpcRemoteError&){throw ModelProtocolError("Invalid worker provider failure");}
}
}
AgentWorkerRpcHandler bind_agent_worker_model_rpc(AgentWorkerModelHost& host,const AgentWorkerScope& scope){
    const auto& lease=host.lease();if(scope!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Model RPC channel differs from captured lease");
    return [&host](const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel)->std::string{
        if(method!="model.complete"&&method!="model.prepare_request"&&method!="model.admission"&&method!="model.rebuild_request")throw AgentWorkerRpcRemoteError("unsupported");
        try{const auto value=request_envelope(payload);
            if(method=="model.admission"){if(!value.is_object()||!value.empty())throw AgentWorkerRpcRemoteError("rejected");return encode_agent_worker_model_admission(host.admission(cancel));}
            if(method=="model.prepare_request"){if(!value.is_object()||!value.empty())throw AgentWorkerRpcRemoteError("rejected");const auto bytes=encode_agent_model_request(host.prepare_request(cancel));if(bytes.size()>AgentWorkerFrameCodec::max_frame_bytes/2-1024)throw AgentWorkerRpcRemoteError("capacity");return bytes;}
            if(!value.is_object()||value.size()!=2||!value.at("attempt_id").is_string()||!value.at("request").is_string())throw AgentWorkerRpcRemoteError("rejected");const auto attempt=value.at("attempt_id").get<std::string>();if(attempt.empty()||attempt.size()>128||attempt.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos)throw AgentWorkerRpcRemoteError("rejected");
            const auto request=decode_agent_model_request(value.at("request").get<std::string>());if(method=="model.rebuild_request"){const auto bytes=encode_agent_model_request(host.rebuild_request(attempt,request,cancel));if(bytes.size()>AgentWorkerFrameCodec::max_frame_bytes/2-1024)throw AgentWorkerRpcRemoteError("capacity");return bytes;}return encode_agent_model_completion(host.complete(attempt,request,[&](const ModelEvent& event){sink(encode_agent_model_event(event));},cancel));
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
AgentWorkerModelAdmission AgentWorkerModelClient::admission(std::stop_token cancel){
    try{return decode_agent_worker_model_admission(peer_.call("model.admission","{}",{},cancel));}
    catch(const AgentWorkerRpcCancelled&){throw TransportCancelled("Worker model admission RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw TransportTimeout("Worker model admission RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="cancelled")throw TransportCancelled("Worker model admission cancelled");if(error.code()=="timeout")throw TransportTimeout("Worker model admission deadline elapsed");if(error.code()=="rejected")throw Conflict("Worker model admission rejected its captured owner");throw ModelProtocolError("Worker model admission capability unavailable");}
}
ModelRequest AgentWorkerModelClient::prepare_request(std::stop_token cancel){
    try{return decode_agent_model_request(peer_.call("model.prepare_request","{}",{},cancel));}
    catch(const AgentWorkerRpcCancelled&){throw TransportCancelled("Worker context RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw TransportTimeout("Worker context RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker context RPC rejected its captured owner");if(error.code()=="cancelled")throw TransportCancelled("Worker context owner cancelled");if(error.code()=="timeout")throw TransportTimeout("Worker context deadline elapsed");throw;}
}
ModelRequest AgentWorkerModelClient::rebuild_request(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){
    try{return decode_agent_model_request(peer_.call("model.rebuild_request",Json{{"attempt_id",attempt},{"request",encode_agent_model_request(request)}}.dump(),{},cancel));}
    catch(const AgentWorkerRpcCancelled&){throw TransportCancelled("Worker context rebuild RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw TransportTimeout("Worker context rebuild RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker context rebuild rejected its captured owner");if(error.code()=="cancelled")throw TransportCancelled("Worker context rebuild owner cancelled");if(error.code()=="timeout")throw TransportTimeout("Worker context rebuild deadline elapsed");throw;}
}
ModelCompletion AgentWorkerModelClient::complete(const std::string& attempt,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel){
    if(!sink)throw std::invalid_argument("Worker model RPC requires its event sink");
    try{return decode_agent_model_completion(peer_.call("model.complete",Json{{"attempt_id",attempt},{"request",encode_agent_model_request(request)}}.dump(),[&](const std::string& event){sink(decode_agent_model_event(event));},cancel));}
    catch(const AgentWorkerRpcCancelled&){throw TransportCancelled("Worker model RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw TransportTimeout("Worker model RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="cancelled")throw TransportCancelled("Worker model RPC cancelled");if(error.code()=="timeout")throw TransportTimeout("Worker model RPC deadline elapsed");if(error.code()=="rejected")throw Conflict("Worker model RPC rejected its captured scope");if(error.code()=="outcome_unrecorded")throw AgentWorkerModelOutcomeUnrecorded("Worker model acknowledgement is unrecorded");if(error.code()=="provider_http")throw provider_failure(error.detail());throw ModelProtocolError("Worker model RPC failed");}
}
}
