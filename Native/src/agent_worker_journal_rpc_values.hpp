#pragma once
#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/agent_worker_journal_values.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/agent_worker_file_host.hpp"
#include "nlohmann/json.hpp"
#include <limits>
#include <algorithm>
#include <set>
#include <type_traits>

namespace agentflow {
namespace {
using Json=nlohmann::json;
// Leave room for the enclosing RPC string and its escaping. Never truncate an
// admitted history or event list to make it fit; larger windows need paging.
constexpr std::size_t max_value_bytes=AgentWorkerChannel::max_message_bytes/2-1024,max_rows=4096;
[[noreturn]] void invalid(){throw AgentWorkerProtocolError("Invalid owned worker journal values");}
Json parse(const std::string& bytes){
    if(bytes.empty()||bytes.size()>max_value_bytes)invalid();std::vector<std::set<std::string>> keys;
    return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){if(depth>8)invalid();if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key&&(keys.empty()||!keys.back().insert(value.get<std::string>()).second))invalid();if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});
}
void object(const Json& v,std::size_t n){if(!v.is_object()||v.size()!=n)invalid();}
std::int64_t integer(const Json& v){if(!v.is_number_integer()||(v.is_number_unsigned()&&v.get<std::uint64_t>()>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))invalid();return v.get<std::int64_t>();}
std::string text(const Json& v){return v.get<std::string>();}
void rows(const Json& v){if(!v.is_array()||v.size()>max_rows)invalid();}
RunState state(const std::string& v){for(const auto s:{RunState::queued,RunState::running,RunState::paused,RunState::completed,RunState::failed,RunState::cancelled})if(v==to_string(s))return s;invalid();}
AgentWorkerJournalReadKind kind(const std::string& method){
    if(method=="journal.run")return AgentWorkerJournalReadKind::run;if(method=="journal.session")return AgentWorkerJournalReadKind::session;
    if(method=="journal.history")return AgentWorkerJournalReadKind::history;if(method=="journal.events")return AgentWorkerJournalReadKind::events;
    if(method=="journal.child_admission")return AgentWorkerJournalReadKind::child_admission;
    if(method=="journal.root_budget")return AgentWorkerJournalReadKind::root_budget;throw AgentWorkerRpcRemoteError("unsupported");
}
AgentWorkerJournalValue decode(AgentWorkerJournalReadKind k,const std::string& bytes){
    try{const auto v=parse(bytes);
        switch(k){
        case AgentWorkerJournalReadKind::run:object(v,7);return Run{text(v.at("id")),text(v.at("session_id")),state(text(v.at("state"))),text(v.at("parent_id")),text(v.at("node_id")),v.at("graph_root").get<bool>(),text(v.at("provider_context_json"))};
        case AgentWorkerJournalReadKind::session:{object(v,5);Session s{text(v.at("id")),text(v.at("title")),text(v.at("parent_id")),{},text(v.at("fork_boundary_type"))};if(!v.at("fork_boundary_sequence").is_null())s.fork_boundary_sequence=integer(v.at("fork_boundary_sequence"));return s;}
        case AgentWorkerJournalReadKind::history:{rows(v);std::vector<Message> result;for(const auto& m:v){object(m,3);const auto seq=integer(m.at("sequence"));if(seq<=0||(!result.empty()&&seq<=result.back().sequence))invalid();result.push_back({seq,text(m.at("role")),text(m.at("json"))});}return result;}
        case AgentWorkerJournalReadKind::events:{rows(v);std::vector<Event> result;for(const auto& e:v){object(e,4);const auto seq=integer(e.at("sequence"));if(seq<=0||(!result.empty()&&seq<=result.back().sequence))invalid();result.push_back({seq,text(e.at("run_id")),text(e.at("kind")),text(e.at("json"))});}return result;}
        case AgentWorkerJournalReadKind::child_admission:{if(text(v.at("kind"))=="dynamic_agent"){object(v,9);ChildAdmissionRecord result;result.kind=ChildAdmissionKind::dynamic_agent;result.root_run_id=text(v.at("root_run_id"));result.plan_id=text(v.at("plan_id"));result.node_label=text(v.at("node_label"));result.claim_id=text(v.at("claim_id"));result.definition_revision=integer(v.at("definition_revision"));result.claim_revision=integer(v.at("claim_revision"));result.preset_id=text(v.at("preset_id"));result.preset_revision=integer(v.at("preset_revision"));if(result.root_run_id.empty()||result.plan_id.empty()||result.node_label.empty()||result.claim_id.empty()||result.definition_revision<1||result.claim_revision<1||result.preset_revision!=1||(result.preset_id!="workspace.inspect"&&result.preset_id!="workspace.coding"))invalid();return result;}if(text(v.at("kind"))=="graph_agent"){object(v,3);const auto path=text(v.at("graph_node_path"));if(text(v.at("root_run_id")).empty()||path.empty()||path.size()>259||path.front()=='/'||path.back()=='/'||path.find("//")!=std::string::npos||path.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-/")!=std::string::npos||std::count(path.begin(),path.end(),'/')>3)invalid();ChildAdmissionRecord result;result.kind=ChildAdmissionKind::graph_agent;result.root_run_id=text(v.at("root_run_id"));result.graph_node_path=path;return result;}object(v,5);if(text(v.at("kind"))!="delegated_leaf"||text(v.at("preset_id"))!="workspace.inspect"||integer(v.at("preset_revision"))!=1||text(v.at("root_run_id")).empty()||text(v.at("batch_id")).empty())invalid();return ChildAdmissionRecord{ChildAdmissionKind::delegated_leaf,text(v.at("root_run_id")),text(v.at("batch_id")),"workspace.inspect",1};}
        case AgentWorkerJournalReadKind::root_budget:{object(v,8);const auto& s=v.at("spec");object(s,8);return RootBudgetRecord{text(v.at("root_run_id")),{text(s.at("policy_id")),integer(s.at("policy_revision")),text(s.at("workspace_identity")),text(s.at("provider_identity_json")),integer(s.at("max_children")),integer(s.at("max_parallel")),integer(s.at("max_model_calls")),integer(s.at("wall_limit_ms"))},integer(v.at("children_admitted")),integer(v.at("model_calls_reserved")),integer(v.at("parent_calls_held")),integer(v.at("revision")),integer(v.at("planned_children_reserved")),integer(v.at("parent_model_calls_reserved"))};}
        }invalid();
    }catch(const Json::exception&){invalid();}
}
AgentWorkerJournalValue call(AgentWorkerClientContext& peer,const std::string& method,const std::string& target,std::int64_t after=0){
    try{std::string bytes;{auto* native=&peer.services();switch(kind(method)){case AgentWorkerJournalReadKind::run:bytes=native->journal_run(target);break;case AgentWorkerJournalReadKind::session:bytes=native->journal_session(target);break;case AgentWorkerJournalReadKind::history:bytes=native->journal_history(target);break;case AgentWorkerJournalReadKind::events:bytes=native->journal_events(target,after);break;case AgentWorkerJournalReadKind::root_budget:bytes=native->journal_root_budget(target);break;case AgentWorkerJournalReadKind::child_admission:bytes=native->journal_child_admission(target);break;}}return decode(kind(method),bytes);}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker journal read rejected");if(error.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker journal owner cancelled");if(error.code()=="timeout")throw AgentWorkerRpcTimeout("Worker journal deadline elapsed");throw;}
}
std::string attempt_id(const Json& v){auto id=text(v);if(id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos)invalid();return id;}
ModelCallReservation attempt_call(AgentWorkerClientContext& peer,const std::string& method,const std::string& attempt,const std::string& expected_state,const std::string& planning_call={}){
    try{const auto v=parse(((method=="journal.reserve_planning_continuation"?peer.services().journal_reserve_planning_continuation(planning_call,attempt):method=="journal.reserve_model"?peer.services().journal_reserve_model(attempt):method=="journal.start_model"?peer.services().journal_start_model(attempt):peer.services().journal_finish_model(attempt))));object(v,8);const auto& scope=peer.scope();const auto role=scope.root_id==scope.run_id?ModelCallRole::parent:ModelCallRole::leaf;
        if(text(v.at("root_run_id"))!=scope.root_id||text(v.at("owner_run_id"))!=scope.run_id||(attempt_id(v.at("attempt_id"))!=attempt&&method!="journal.reserve_model"&&method!="journal.reserve_planning_continuation")||text(v.at("role"))!=(role==ModelCallRole::parent?"parent":"leaf")||text(v.at("state"))!=expected_state||text(v.at("purpose"))!="inference")invalid();
        const auto step=text(v.at("inference_step_id"));if(!step.empty()){(void)attempt_id(v.at("inference_step_id"));const auto budget=std::get<RootBudgetRecord>(call(peer,"journal.root_budget",scope.root_id));if(scope.root_id==scope.run_id){if(budget.spec.policy_id=="native.dynamic-plan"){if(!peer.native_model()||budget.spec.max_children<1||budget.spec.max_children>32||budget.spec.max_parallel<1||budget.spec.max_parallel>2)invalid();}else if(budget.spec.policy_id=="native.delegation"){if(budget.spec.max_children<1||budget.spec.max_children>8||budget.spec.max_parallel<1||budget.spec.max_parallel>2)invalid();}else if(budget.spec.policy_id!="native.context"||budget.spec.max_children!=0||budget.spec.max_parallel!=0)invalid();}else{const auto child=AgentWorkerJournalClient(peer).child_admission(scope.run_id);if(budget.spec.policy_id=="native.graph-context"){if(child.kind!=ChildAdmissionKind::graph_agent||budget.spec.max_children!=8||budget.spec.max_parallel<1||budget.spec.max_parallel>2||budget.spec.max_model_calls!=32)invalid();}else if(budget.spec.policy_id=="native.dynamic-plan"){if(!peer.native_model()||(child.kind!=ChildAdmissionKind::dynamic_agent&&child.kind!=ChildAdmissionKind::delegated_leaf))invalid();}else if(budget.spec.policy_id!="native.delegation"||child.kind!=ChildAdmissionKind::delegated_leaf)invalid();}}else if(attempt_id(v.at("attempt_id"))!=attempt&&!(method=="journal.reserve_planning_continuation"&&attempt.empty()))invalid();
        const auto assistant=text(v.at("actual_assistant_json"));if((expected_state=="finished")==assistant.empty())invalid();return {scope.root_id,attempt_id(v.at("attempt_id")),scope.run_id,role,expected_state,ModelCallPurpose::inference,step,assistant};
    }catch(const Json::exception&){invalid();}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker model funding rejected");if(e.code()=="budget_exhausted")throw RootBudgetExhausted("Worker root model allowance exhausted");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker model funding cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker funding deadline elapsed");throw;}
}
void digest(const std::string& value){if(value.size()!=64||value.find_first_not_of("0123456789abcdef")!=std::string::npos)invalid();}
AgentWorkerToolTurnRecord decode_turn(const AgentWorkerScope& scope,const std::string& attempt,const std::string& bytes){
    try{const auto v=parse(bytes);object(v,3);const auto& m=v.at("model");object(m,5);AgentWorkerToolTurnRecord turn{{text(m.at("generation")),text(m.at("root_run_id")),text(m.at("owner_run_id")),attempt_id(m.at("attempt_id")),text(m.at("request_binding"))},{},text(v.at("messages_binding"))};
        if(turn.model.generation!=scope.generation||turn.model.root_run_id!=scope.root_id||turn.model.owner_run_id!=scope.run_id||turn.model.attempt_id!=attempt)invalid();digest(turn.model.request_binding);digest(turn.messages_binding);
        const auto& sequences=v.at("message_sequences");if(!sequences.is_array()||sequences.size()<2||sequences.size()>65)invalid();std::int64_t previous=0;for(const auto& s:sequences){const auto n=integer(s);if(n<=previous||n>9007199254740991LL)invalid();turn.message_sequences.push_back(n);previous=n;}return turn;
    }catch(const Json::exception&){invalid();}
}
}
}
