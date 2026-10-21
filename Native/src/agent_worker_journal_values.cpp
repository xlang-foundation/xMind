#include "agentflow/agent_worker_journal_values.hpp"
#include "nlohmann/json.hpp"
#include <type_traits>
namespace agentflow {
namespace {using Json=nlohmann::json;constexpr std::size_t max_value_bytes=2*1024*1024-1024,max_rows=4096;}
std::string encode_worker_journal_value(const AgentWorkerJournalValue& input){
    Json v;std::visit([&](const auto& value){using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,Run>)v={{"id",value.id},{"session_id",value.session_id},{"state",to_string(value.state)},{"parent_id",value.parent_id},{"node_id",value.node_id},{"graph_root",value.graph_root},{"provider_context_json",value.provider_context_json}};
        else if constexpr(std::is_same_v<T,Session>)v={{"id",value.id},{"title",value.title},{"parent_id",value.parent_id},{"fork_boundary_sequence",value.fork_boundary_sequence?Json(*value.fork_boundary_sequence):Json(nullptr)},{"fork_boundary_type",value.fork_boundary_type}};
        else if constexpr(std::is_same_v<T,std::vector<Message>>||std::is_same_v<T,std::vector<Event>>){
            if(value.size()>max_rows)throw AgentWorkerJournalCapacity("Worker journal value exceeds capacity");v=Json::array();for(const auto& item:value){
                if constexpr(std::is_same_v<T,std::vector<Message>>)v.push_back({{"sequence",item.sequence},{"role",item.role},{"json",item.json}});
                else v.push_back({{"sequence",item.sequence},{"run_id",item.run_id},{"kind",item.kind},{"json",item.json}});
            }
        }else if constexpr(std::is_same_v<T,ChildAdmissionRecord>){if(value.kind==ChildAdmissionKind::graph_agent){v={{"kind","graph_agent"},{"root_run_id",value.root_run_id},{"graph_node_path",value.graph_node_path}};}else{if(value.kind!=ChildAdmissionKind::delegated_leaf||value.preset_id!="workspace.inspect"||value.preset_revision!=1)throw Conflict("Worker journal value rejected");v={{"kind","delegated_leaf"},{"root_run_id",value.root_run_id},{"batch_id",value.batch_id},{"preset_id",value.preset_id},{"preset_revision",value.preset_revision}};}
        }else if constexpr(std::is_same_v<T,RootBudgetRecord>){const auto& s=value.spec;
            v={{"root_run_id",value.root_run_id},{"spec",{{"policy_id",s.policy_id},{"policy_revision",s.policy_revision},{"workspace_identity",s.workspace_identity},{"provider_identity_json",s.provider_identity_json},{"max_children",s.max_children},{"max_parallel",s.max_parallel},{"max_model_calls",s.max_model_calls},{"wall_limit_ms",s.wall_limit_ms}}},
                {"children_admitted",value.children_admitted},{"model_calls_reserved",value.model_calls_reserved},{"parent_calls_held",value.parent_calls_held},{"revision",value.revision},{"planned_children_reserved",value.planned_children_reserved},{"parent_model_calls_reserved",value.parent_model_calls_reserved}};
        }
    },input);auto bytes=v.dump();if(bytes.size()>max_value_bytes)throw AgentWorkerJournalCapacity("Worker journal value exceeds capacity");return bytes;
}
std::string encode_worker_model_reservation(const ModelCallReservation& r){auto value=Json{{"root_run_id",r.root_run_id},{"owner_run_id",r.owner_run_id},{"attempt_id",r.attempt_id},{"role",r.role==ModelCallRole::parent?"parent":"leaf"},{"state",r.state},{"purpose",r.purpose==ModelCallPurpose::inference?"inference":"context_compaction"},{"inference_step_id",r.inference_step_id},{"actual_assistant_json",r.actual_assistant_json}};auto bytes=value.dump();if(bytes.size()>max_value_bytes)throw AgentWorkerJournalCapacity("Worker journal value exceeds capacity");return bytes;}
std::string encode_worker_tool_turn(const AgentWorkerToolTurnRecord& turn){const auto& m=turn.model;return Json{{"model",{{"generation",m.generation},{"root_run_id",m.root_run_id},{"owner_run_id",m.owner_run_id},{"attempt_id",m.attempt_id},{"request_binding",m.request_binding}}},{"message_sequences",turn.message_sequences},{"messages_binding",turn.messages_binding}}.dump();}
}
