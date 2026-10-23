#pragma once
#include "agentflow/connection_binding.hpp"
#include "nlohmann/json.hpp"
namespace agentflow::connection_values {
inline nlohmann::json identity(const ConnectionBindingIdentity& value){return {{"connection_id",value.connection_id},{"profile_revision",value.profile_revision},{"server_id",value.server_id},{"backend_generation",value.backend_generation},{"protocol_version",value.protocol_version},{"kind","local"}};}
inline nlohmann::json bound(const BoundConnectionSession& value){return {{"binding",identity(value.binding)},{"session",{{"id",value.session.id},{"title",value.session.title},{"parent_id",value.session.parent_id}}}};}
inline nlohmann::json bound(const BoundConnectionRun& value){return {{"binding",identity(value.binding)},{"run",{{"id",value.run.id},{"session_id",value.run.session_id},{"state",to_string(value.run.state)},{"parent_id",value.run.parent_id},{"node_id",value.run.node_id},{"graph_root",value.run.graph_root}}}};}
inline nlohmann::json cursor(const ConnectionEventCursor& value){return {{"server_id",value.server_id},{"connection_id",value.connection_id},{"session_id",value.session_id},{"run_id",value.run_id},{"sequence",value.sequence}};}
inline ConnectionEventCursor cursor(const nlohmann::json& value){
    if(!value.is_object()||value.size()!=5||!value.contains("sequence")||!value["sequence"].is_number_integer())throw std::invalid_argument("Invalid connection event cursor");
    ConnectionEventCursor result;result.sequence=value["sequence"].get<std::int64_t>();
    for(const auto* key:{"server_id","connection_id","session_id","run_id"})if(!value.contains(key)||!value[key].is_string()||value[key].get_ref<const std::string&>().size()>256)throw std::invalid_argument("Invalid connection event cursor identity");
    result.server_id=value["server_id"];result.connection_id=value["connection_id"];result.session_id=value["session_id"];result.run_id=value["run_id"];return result;
}
inline nlohmann::json page(const ConnectionEventPage& value){auto events=nlohmann::json::array();for(const auto& event:value.events)events.push_back({{"sequence",event.sequence},{"run_id",event.run_id},{"kind",event.kind},{"json",event.json}});return {{"events",std::move(events)},{"cursor",cursor(value.cursor)}};}
}
