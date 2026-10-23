#pragma once
#include "agentflow/connection_binding.hpp"
#include "nlohmann/json.hpp"
namespace agentflow::connection_values {
inline nlohmann::json identity(const ConnectionBindingIdentity& value){return {{"connection_id",value.connection_id},{"profile_revision",value.profile_revision},{"server_id",value.server_id},{"backend_generation",value.backend_generation},{"protocol_version",value.protocol_version},{"kind","local"}};}
inline nlohmann::json bound(const BoundConnectionSession& value){return {{"binding",identity(value.binding)},{"session",{{"id",value.session.id},{"title",value.session.title},{"parent_id",value.session.parent_id}}}};}
inline nlohmann::json bound(const BoundConnectionRun& value){nlohmann::json run={{"id",value.run.id},{"session_id",value.run.session_id},{"state",to_string(value.run.state)},{"parent_id",value.run.parent_id},{"node_id",value.run.node_id},{"graph_root",value.run.graph_root}};if(!value.run.provider_context_json.empty())run["provider_context"]=nlohmann::json::parse(value.run.provider_context_json);return {{"binding",identity(value.binding)},{"run",std::move(run)}};}
inline nlohmann::json operation(const Operation& value){return {{"id",value.id},{"run_id",value.spec.run_id},{"workspace_id",value.spec.workspace},{"tool",value.spec.tool},{"arguments_json",value.spec.arguments_json},{"resources",value.spec.resources},{"state",to_string(value.state)},{"expires_unix_ms",value.expires_unix_ms},{"decision_actor",value.decision_actor},{"result_json",value.result_json}};}
inline nlohmann::json bound(const BoundConnectionOperation& value){return {{"binding",identity(value.binding)},{"operation",operation(value.operation)}};}
inline nlohmann::json bound(const BoundConnectionOperations& value){auto operations=nlohmann::json::array();for(const auto& item:value.operations)operations.push_back(operation(item));return {{"binding",identity(value.binding)},{"session_id",value.session_id},{"run_id",value.run_id},{"operations",std::move(operations)}};}
inline nlohmann::json cursor(const ConnectionEventCursor& value){return {{"server_id",value.server_id},{"connection_id",value.connection_id},{"session_id",value.session_id},{"run_id",value.run_id},{"sequence",value.sequence}};}
inline ConnectionEventCursor cursor(const nlohmann::json& value){
    if(!value.is_object()||value.size()!=5||!value.contains("sequence")||!value["sequence"].is_number_integer())throw std::invalid_argument("Invalid connection event cursor");
    ConnectionEventCursor result;result.sequence=value["sequence"].get<std::int64_t>();
    for(const auto* key:{"server_id","connection_id","session_id","run_id"})if(!value.contains(key)||!value[key].is_string()||value[key].get_ref<const std::string&>().size()>256)throw std::invalid_argument("Invalid connection event cursor identity");
    result.server_id=value["server_id"];result.connection_id=value["connection_id"];result.session_id=value["session_id"];result.run_id=value["run_id"];return result;
}
inline nlohmann::json page(const ConnectionEventPage& value){auto events=nlohmann::json::array();for(const auto& event:value.events)events.push_back({{"sequence",event.sequence},{"run_id",event.run_id},{"kind",event.kind},{"json",event.json}});return {{"events",std::move(events)},{"cursor",cursor(value.cursor)}};}
}
