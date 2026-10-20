#include "agentflow/agent_worker_control.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
[[noreturn]] void invalid(){throw AgentWorkerProtocolError("Invalid worker control reply");}
Json reply(const std::string& bytes){
    if(bytes.empty()||bytes.size()>256)invalid();
    try{
        std::set<std::string> keys;
        auto value=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& item){
            if(depth>1)invalid();
            if(event==Json::parse_event_t::key&&!keys.insert(item.get<std::string>()).second)invalid();
            return true;
        });
        if(!value.is_object()||value.size()!=2||!value.contains("version")||!value.at("version").is_number_integer()||value.at("version")!=1)invalid();
        return value;
    }catch(const Json::exception&){invalid();}
}
}
RunState decode_agent_worker_execution_intent(const std::string& bytes){
    const auto value=reply(bytes);
    if(!value.contains("outcome")||!value.at("outcome").is_string())invalid();
    const auto outcome=value.at("outcome").get<std::string>();
    if(outcome=="completed")return RunState::completed;
    if(outcome=="failed")return RunState::failed;
    if(outcome=="cancelled")return RunState::cancelled;
    invalid();
}
RunState request_agent_worker_execution(AgentWorkerRpcPeer& peer){
    return decode_agent_worker_execution_intent(peer.call("worker.execute","{}"));
}
void request_agent_worker_cancellation(AgentWorkerRpcPeer& peer){
    const auto value=reply(peer.call("worker.cancel","{}"));
    if(!value.contains("stop_requested")||!value.at("stop_requested").is_boolean()||!value.at("stop_requested").get<bool>())invalid();
}
}
