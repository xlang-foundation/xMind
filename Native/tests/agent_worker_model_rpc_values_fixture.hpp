#pragma once
#include "agentflow/agent_worker_model_rpc.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agentflow/records.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
using Json=nlohmann::json;
namespace {
Json request_envelope(const std::string& bytes){if(bytes.empty()||bytes.size()>AgentWorkerChannel::max_message_bytes)throw AgentWorkerRpcRemoteError("rejected");std::vector<std::set<std::string>> keys;return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){if(depth>8)throw AgentWorkerRpcRemoteError("rejected");if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key&&(keys.empty()||!keys.back().insert(value.get<std::string>()).second))throw AgentWorkerRpcRemoteError("rejected");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});}
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
}
