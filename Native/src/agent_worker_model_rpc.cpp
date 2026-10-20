#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agentflow/records.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
using Json=nlohmann::json;
namespace {
Json request_envelope(const std::string& bytes){if(bytes.empty()||bytes.size()>AgentWorkerFrameCodec::max_frame_bytes)throw AgentWorkerRpcRemoteError("rejected");std::vector<std::set<std::string>> keys;return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){if(depth>8)throw AgentWorkerRpcRemoteError("rejected");if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key&&(keys.empty()||!keys.back().insert(value.get<std::string>()).second))throw AgentWorkerRpcRemoteError("rejected");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});}
}
AgentWorkerRpcHandler bind_agent_worker_model_rpc(AgentWorkerModelHost& host,const AgentWorkerScope& scope){
    const auto& lease=host.lease();if(scope!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Model RPC channel differs from captured lease");
    return [&host](const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel)->std::string{
        if(method!="model.complete")throw AgentWorkerRpcRemoteError("unsupported");
        try{const auto value=request_envelope(payload);if(!value.is_object()||value.size()!=2||!value.at("attempt_id").is_string()||!value.at("request").is_string())throw AgentWorkerRpcRemoteError("rejected");const auto attempt=value.at("attempt_id").get<std::string>();if(attempt.empty()||attempt.size()>128||attempt.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos)throw AgentWorkerRpcRemoteError("rejected");
            const auto request=decode_agent_model_request(value.at("request").get<std::string>());return encode_agent_model_completion(host.complete(attempt,request,[&](const ModelEvent& event){sink(encode_agent_model_event(event));},cancel));
        }catch(const AgentWorkerRpcRemoteError&){throw;}
         catch(const AgentWorkerModelOutcomeUnrecorded&){throw AgentWorkerRpcRemoteError("outcome_unrecorded");}
         catch(const AgentWorkerRpcCancelled&){throw AgentWorkerRpcRemoteError("cancelled");}
         catch(const TransportCancelled&){throw AgentWorkerRpcRemoteError("cancelled");}
         catch(const TransportTimeout&){throw AgentWorkerRpcRemoteError("timeout");}
         catch(const ProviderHttpError& error){throw AgentWorkerRpcRemoteError("provider_http",Json{{"status",error.status}}.dump());}
         catch(const Conflict&){throw AgentWorkerRpcRemoteError("rejected");}
         catch(const NotFound&){throw AgentWorkerRpcRemoteError("rejected");}
         catch(const ModelProtocolError&){throw AgentWorkerRpcRemoteError("model_protocol");}
         catch(const std::invalid_argument&){throw AgentWorkerRpcRemoteError("rejected");}
         catch(const Json::exception&){throw AgentWorkerRpcRemoteError("rejected");}
    };
}
ModelCompletion AgentWorkerModelClient::complete(const std::string& attempt,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel){
    if(!sink)throw std::invalid_argument("Worker model RPC requires its event sink");
    try{return decode_agent_model_completion(peer_.call("model.complete",Json{{"attempt_id",attempt},{"request",encode_agent_model_request(request)}}.dump(),[&](const std::string& event){sink(decode_agent_model_event(event));},cancel));}
    catch(const AgentWorkerRpcCancelled&){throw TransportCancelled("Worker model RPC cancelled");}
    catch(const AgentWorkerRpcTimeout&){throw TransportTimeout("Worker model RPC deadline elapsed");}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="cancelled")throw TransportCancelled("Worker model RPC cancelled");if(error.code()=="timeout")throw TransportTimeout("Worker model RPC deadline elapsed");if(error.code()=="rejected")throw Conflict("Worker model RPC rejected its captured scope");if(error.code()=="outcome_unrecorded")throw AgentWorkerModelOutcomeUnrecorded("Worker model acknowledgement is unrecorded");if(error.code()=="provider_http"){try{const auto value=Json::parse(error.detail());const auto status=value.at("status").get<int>();if(value.size()!=1||status<100||status>599)throw ModelProtocolError("Invalid worker provider failure");throw ProviderHttpError(status);}catch(const Json::exception&){throw ModelProtocolError("Invalid worker provider failure");}}throw ModelProtocolError("Worker model RPC failed");}
}
}
