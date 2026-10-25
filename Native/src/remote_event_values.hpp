#pragma once
#include "agentflow/remote_connection_client.hpp"
#include "nlohmann/json.hpp"

namespace agentflow::remote_event_values {
// Public domain fields only; SDK value transfer remains owned by xlang3 IPC.
inline nlohmann::json cursor(const RemoteRunEventCursor& value,const std::string& generation){
    return {{"connection_id",value.connection_id},{"server_id",value.server_id},
        {"scope_id",value.scope_id},{"session_id",value.session_id},{"run_id",value.run_id},
        {"epoch",value.epoch},{"sequence",value.sequence},{"backend_generation",generation}};
}
inline RemoteRunEventCursor cursor(const nlohmann::json& value,const std::string& generation){
    if(!value.is_object()||value.size()!=8)throw std::invalid_argument("Invalid remote event cursor");
    for(const auto* field:{"connection_id","server_id","scope_id","session_id","run_id","epoch","backend_generation"})
        if(!value.contains(field)||!value.at(field).is_string()||value.at(field).get_ref<const std::string&>().size()>256)
            throw std::invalid_argument("Invalid remote event cursor");
    if(!value.contains("sequence")||!value.at("sequence").is_number_integer()||value.at("sequence")<0||value.at("sequence")>9007199254740991LL)
        throw std::invalid_argument("Invalid remote event sequence");
    if(value.at("backend_generation")!=generation)throw Conflict("Remote event backend generation changed");
    return {value.at("connection_id"),value.at("server_id"),value.at("scope_id"),value.at("session_id"),value.at("run_id"),value.at("epoch"),value.at("sequence")};
}
inline nlohmann::json page(const RemoteObservedRunEvents& value,const std::string& generation){
    auto events=nlohmann::json::array();
    for(const auto& event:value.events)events.push_back({{"sequence",event.sequence},{"run_id",event.run_id},{"kind",event.kind},{"payload",nlohmann::json::parse(event.json)}});
    return {{"cursor",cursor(value.cursor,generation)},{"events",std::move(events)}};
}
}
