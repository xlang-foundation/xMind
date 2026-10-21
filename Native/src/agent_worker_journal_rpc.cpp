#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/agent_worker_file_host.hpp"
#include "nlohmann/json.hpp"
#include <limits>
#include <set>
#include <type_traits>

namespace agentflow {
namespace {
using Json=nlohmann::json;
// Leave room for the enclosing RPC string and its escaping. Never truncate an
// admitted history or event list to make it fit; larger windows need paging.
constexpr std::size_t max_value_bytes=AgentWorkerFrameCodec::max_frame_bytes/2-1024,max_rows=4096;
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
    if(method=="journal.root_budget")return AgentWorkerJournalReadKind::root_budget;throw AgentWorkerRpcRemoteError("unsupported");
}
std::string encode(const AgentWorkerJournalValue& input){
    Json v;std::visit([&](const auto& value){using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,Run>)v={{"id",value.id},{"session_id",value.session_id},{"state",to_string(value.state)},{"parent_id",value.parent_id},{"node_id",value.node_id},{"graph_root",value.graph_root},{"provider_context_json",value.provider_context_json}};
        else if constexpr(std::is_same_v<T,Session>)v={{"id",value.id},{"title",value.title},{"parent_id",value.parent_id},{"fork_boundary_sequence",value.fork_boundary_sequence?Json(*value.fork_boundary_sequence):Json(nullptr)},{"fork_boundary_type",value.fork_boundary_type}};
        else if constexpr(std::is_same_v<T,std::vector<Message>>||std::is_same_v<T,std::vector<Event>>){
            if(value.size()>max_rows)throw AgentWorkerRpcRemoteError("capacity");v=Json::array();for(const auto& item:value){
                if constexpr(std::is_same_v<T,std::vector<Message>>)v.push_back({{"sequence",item.sequence},{"role",item.role},{"json",item.json}});
                else v.push_back({{"sequence",item.sequence},{"run_id",item.run_id},{"kind",item.kind},{"json",item.json}});
            }
        }else if constexpr(std::is_same_v<T,RootBudgetRecord>){const auto& s=value.spec;
            v={{"root_run_id",value.root_run_id},{"spec",{{"policy_id",s.policy_id},{"policy_revision",s.policy_revision},{"workspace_identity",s.workspace_identity},{"provider_identity_json",s.provider_identity_json},{"max_children",s.max_children},{"max_parallel",s.max_parallel},{"max_model_calls",s.max_model_calls},{"wall_limit_ms",s.wall_limit_ms}}},
                {"children_admitted",value.children_admitted},{"model_calls_reserved",value.model_calls_reserved},{"parent_calls_held",value.parent_calls_held},{"revision",value.revision},{"planned_children_reserved",value.planned_children_reserved},{"parent_model_calls_reserved",value.parent_model_calls_reserved}};
        }
    },input);auto bytes=v.dump();if(bytes.size()>max_value_bytes)throw AgentWorkerRpcRemoteError("capacity");return bytes;
}
AgentWorkerJournalValue decode(AgentWorkerJournalReadKind k,const std::string& bytes){
    try{const auto v=parse(bytes);
        switch(k){
        case AgentWorkerJournalReadKind::run:object(v,7);return Run{text(v.at("id")),text(v.at("session_id")),state(text(v.at("state"))),text(v.at("parent_id")),text(v.at("node_id")),v.at("graph_root").get<bool>(),text(v.at("provider_context_json"))};
        case AgentWorkerJournalReadKind::session:{object(v,5);Session s{text(v.at("id")),text(v.at("title")),text(v.at("parent_id")),{},text(v.at("fork_boundary_type"))};if(!v.at("fork_boundary_sequence").is_null())s.fork_boundary_sequence=integer(v.at("fork_boundary_sequence"));return s;}
        case AgentWorkerJournalReadKind::history:{rows(v);std::vector<Message> result;for(const auto& m:v){object(m,3);const auto seq=integer(m.at("sequence"));if(seq<=0||(!result.empty()&&seq<=result.back().sequence))invalid();result.push_back({seq,text(m.at("role")),text(m.at("json"))});}return result;}
        case AgentWorkerJournalReadKind::events:{rows(v);std::vector<Event> result;for(const auto& e:v){object(e,4);const auto seq=integer(e.at("sequence"));if(seq<=0||(!result.empty()&&seq<=result.back().sequence))invalid();result.push_back({seq,text(e.at("run_id")),text(e.at("kind")),text(e.at("json"))});}return result;}
        case AgentWorkerJournalReadKind::root_budget:{object(v,8);const auto& s=v.at("spec");object(s,8);return RootBudgetRecord{text(v.at("root_run_id")),{text(s.at("policy_id")),integer(s.at("policy_revision")),text(s.at("workspace_identity")),text(s.at("provider_identity_json")),integer(s.at("max_children")),integer(s.at("max_parallel")),integer(s.at("max_model_calls")),integer(s.at("wall_limit_ms"))},integer(v.at("children_admitted")),integer(v.at("model_calls_reserved")),integer(v.at("parent_calls_held")),integer(v.at("revision")),integer(v.at("planned_children_reserved")),integer(v.at("parent_model_calls_reserved"))};}
        }invalid();
    }catch(const Json::exception&){invalid();}
}
AgentWorkerJournalValue call(AgentWorkerRpcPeer& peer,const std::string& method,const std::string& target,std::int64_t after=0){
    try{return decode(kind(method),peer.call(method,Json{{"target",target},{"after",after}}.dump()));}
    catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="rejected")throw Conflict("Worker journal read rejected");if(error.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker journal owner cancelled");if(error.code()=="timeout")throw AgentWorkerRpcTimeout("Worker journal deadline elapsed");throw;}
}
std::string attempt_id(const Json& v){auto id=text(v);if(id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos)invalid();return id;}
std::string encode_attempt(const ModelCallReservation& r){auto value=Json{{"root_run_id",r.root_run_id},{"owner_run_id",r.owner_run_id},{"attempt_id",r.attempt_id},{"role",r.role==ModelCallRole::parent?"parent":"leaf"},{"state",r.state},{"purpose",r.purpose==ModelCallPurpose::inference?"inference":"context_compaction"},{"inference_step_id",r.inference_step_id},{"actual_assistant_json",r.actual_assistant_json}};auto bytes=value.dump();if(bytes.size()>max_value_bytes)throw AgentWorkerRpcRemoteError("capacity");return bytes;}
ModelCallReservation attempt_call(AgentWorkerRpcPeer& peer,const std::string& method,const std::string& attempt,const std::string& expected_state){
    try{const auto v=parse(peer.call(method,Json{{"attempt_id",attempt}}.dump()));object(v,8);const auto& scope=peer.scope();
        if(text(v.at("root_run_id"))!=scope.root_id||text(v.at("owner_run_id"))!=scope.run_id||attempt_id(v.at("attempt_id"))!=attempt||text(v.at("role"))!="parent"||text(v.at("state"))!=expected_state||text(v.at("purpose"))!="inference"||!text(v.at("inference_step_id")).empty())invalid();
        const auto assistant=text(v.at("actual_assistant_json"));if((expected_state=="finished")==assistant.empty())invalid();return {scope.root_id,attempt,scope.run_id,ModelCallRole::parent,expected_state,ModelCallPurpose::inference,"",assistant};
    }catch(const Json::exception&){invalid();}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker model funding rejected");if(e.code()=="budget_exhausted")throw RootBudgetExhausted("Worker root model allowance exhausted");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker model funding cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker funding deadline elapsed");throw;}
}
void digest(const std::string& value){if(value.size()!=64||value.find_first_not_of("0123456789abcdef")!=std::string::npos)invalid();}
std::string encode_turn(const AgentWorkerToolTurnRecord& turn){const auto& m=turn.model;return Json{{"model",{{"generation",m.generation},{"root_run_id",m.root_run_id},{"owner_run_id",m.owner_run_id},{"attempt_id",m.attempt_id},{"request_binding",m.request_binding}}},{"message_sequences",turn.message_sequences},{"messages_binding",turn.messages_binding}}.dump();}
AgentWorkerToolTurnRecord decode_turn(const AgentWorkerScope& scope,const std::string& attempt,const std::string& bytes){
    try{const auto v=parse(bytes);object(v,3);const auto& m=v.at("model");object(m,5);AgentWorkerToolTurnRecord turn{{text(m.at("generation")),text(m.at("root_run_id")),text(m.at("owner_run_id")),attempt_id(m.at("attempt_id")),text(m.at("request_binding"))},{},text(v.at("messages_binding"))};
        if(turn.model.generation!=scope.generation||turn.model.root_run_id!=scope.root_id||turn.model.owner_run_id!=scope.run_id||turn.model.attempt_id!=attempt)invalid();digest(turn.model.request_binding);digest(turn.messages_binding);
        const auto& sequences=v.at("message_sequences");if(!sequences.is_array()||sequences.size()<2||sequences.size()>65)invalid();std::int64_t previous=0;for(const auto& s:sequences){const auto n=integer(s);if(n<=previous||n>9007199254740991LL)invalid();turn.message_sequences.push_back(n);previous=n;}return turn;
    }catch(const Json::exception&){invalid();}
}
}
Run AgentWorkerJournalClient::run(const std::string& id){return std::get<Run>(call(peer_,"journal.run",id));}
Session AgentWorkerJournalClient::session(const std::string& id){return std::get<Session>(call(peer_,"journal.session",id));}
std::vector<Message> AgentWorkerJournalClient::run_history(const std::string& id){return std::get<std::vector<Message>>(call(peer_,"journal.history",id));}
std::vector<Event> AgentWorkerJournalClient::events(const std::string& id,std::int64_t after){return std::get<std::vector<Event>>(call(peer_,"journal.events",id,after));}
RootBudgetRecord AgentWorkerJournalClient::root_budget(const std::string& id){return std::get<RootBudgetRecord>(call(peer_,"journal.root_budget",id));}
Run AgentWorkerJournalClient::begin_run(){
    try{const auto result=std::get<Run>(decode(AgentWorkerJournalReadKind::run,peer_.call("journal.begin_run","{}")));if(result.id!=peer_.scope().run_id||result.state!=RunState::running)invalid();return result;}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker startup rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker startup cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker startup deadline elapsed");throw;}
}
Event AgentWorkerJournalClient::acknowledge_model_event(const std::string& attempt,std::int64_t ordinal,const std::string& kind,const std::string& json){
    try{const auto values=std::get<std::vector<Event>>(decode(AgentWorkerJournalReadKind::events,peer_.call("journal.model_event",Json{{"attempt_id",attempt},{"ordinal",ordinal},{"kind",kind},{"json",json}}.dump())));if(values.size()!=1||values[0].run_id!=peer_.scope().run_id||values[0].kind!=kind||values[0].json!=json)invalid();return values[0];}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker model event acknowledgement rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker model event acknowledgement cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker model event acknowledgement deadline elapsed");throw;}
}
Event AgentWorkerJournalClient::acknowledge_tool_event(const std::string& attempt,std::int64_t ordinal,bool returned,const std::string& kind,const std::string& json){
    try{const auto values=std::get<std::vector<Event>>(decode(AgentWorkerJournalReadKind::events,peer_.call("journal.tool_event",Json{{"attempt_id",attempt},{"ordinal",ordinal},{"phase",returned?"returned":"started"}}.dump())));if(values.size()!=1||values[0].run_id!=peer_.scope().run_id||values[0].kind!=kind||values[0].json!=json)throw Conflict("Worker tool activity differs from its backend fact");return values[0];}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker tool activity rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker tool activity cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker tool activity deadline elapsed");throw;}
}
Event AgentWorkerJournalClient::acknowledge_workspace_tool_event(const std::string& attempt,std::int64_t ordinal,bool returned,const std::string& kind,const std::string& json){
    try{const auto values=std::get<std::vector<Event>>(decode(AgentWorkerJournalReadKind::events,peer_.call("journal.workspace_tool_event",Json{{"attempt_id",attempt},{"ordinal",ordinal},{"phase",returned?"returned":"started"}}.dump())));if(values.size()!=1||values[0].run_id!=peer_.scope().run_id||values[0].kind!=kind||values[0].json!=json)throw Conflict("Worker tool activity differs from its backend fact");return values[0];}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker tool activity rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker tool activity cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker tool activity deadline elapsed");throw;}
}
ModelCallReservation AgentWorkerJournalClient::reserve_model_call(const std::string& attempt){return attempt_call(peer_,"journal.reserve_model",attempt,"reserved");}
ModelCallReservation AgentWorkerJournalClient::start_model_call(const std::string& attempt){return attempt_call(peer_,"journal.start_model",attempt,"started");}
ModelCallReservation AgentWorkerJournalClient::finish_model_call(const std::string& attempt){return attempt_call(peer_,"journal.finish_model",attempt,"finished");}
AgentWorkerToolTurnRecord AgentWorkerJournalClient::commit_read_turn(const std::string& attempt){
    try{return decode_turn(peer_.scope(),attempt,peer_.call("journal.commit_read_turn",Json{{"attempt_id",attempt}}.dump()));}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker read turn rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker read turn cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker read turn deadline elapsed");throw;}
}
AgentWorkerToolTurnRecord AgentWorkerJournalClient::commit_workspace_turn(const std::string& attempt){
    try{return decode_turn(peer_.scope(),attempt,peer_.call("journal.commit_workspace_turn",Json{{"attempt_id",attempt}}.dump()));}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker workspace turn rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker workspace turn cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker workspace turn deadline elapsed");throw;}
}
Run AgentWorkerJournalClient::complete_run(const std::string& attempt){
    try{const auto value=std::get<Run>(decode(AgentWorkerJournalReadKind::run,peer_.call("journal.complete_run",Json{{"attempt_id",attempt}}.dump())));if(value.id!=peer_.scope().run_id||value.state!=RunState::completed)invalid();return value;}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker completion rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker completion cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker completion deadline elapsed");throw;}
}
static AgentWorkerRpcHandler bind_journal_rpc(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,AgentWorkerChannel::Deadline deadline,std::stop_token owner_cancel,bool workspace){
    if(scope!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Journal channel differs from captured lease");
    (void)store.read_agent_worker_journal(lease,{AgentWorkerJournalReadKind::run,lease.owner_run_id}).get();
    return [&store,lease=std::move(lease),deadline,owner_cancel,workspace](const std::string& method,const std::string& payload,AgentWorkerRpcSink,std::stop_token cancel)->std::string{
        const bool reserve=method=="journal.reserve_model",start=method=="journal.start_model",finish=method=="journal.finish_model",complete=method=="journal.complete_run";auto check=[&]{if(cancel.stop_requested()||owner_cancel.stop_requested())throw AgentWorkerRpcRemoteError("cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerRpcRemoteError("timeout");};
        try{check();const auto v=parse(payload);
            if(method=="journal.begin_run"){object(v,0);return encode(store.start_agent_worker_run(lease,deadline,owner_cancel,cancel).get());}
            if(method=="journal.model_event"){object(v,4);const auto result=store.agent_worker_model_event(lease,attempt_id(v.at("attempt_id")),integer(v.at("ordinal")),text(v.at("kind")),text(v.at("json"))).get();check();return encode(std::vector<Event>{result});}
            if(method=="journal.tool_event"){object(v,3);const auto phase=text(v.at("phase"));if(phase!="started"&&phase!="returned")invalid();const auto result=store.agent_worker_tool_event(lease,attempt_id(v.at("attempt_id")),integer(v.at("ordinal")),phase=="returned",deadline,owner_cancel,cancel).get();check();return encode(std::vector<Event>{result});}
            if(workspace&&method=="journal.workspace_tool_event"){object(v,3);const auto phase=text(v.at("phase"));if(phase!="started"&&phase!="returned")invalid();const auto result=store.agent_worker_workspace_tool_event(lease,attempt_id(v.at("attempt_id")),integer(v.at("ordinal")),phase=="returned",deadline,owner_cancel,cancel).get();check();return encode(std::vector<Event>{result});}
            if(method=="journal.commit_read_turn"){object(v,1);const auto result=store.commit_agent_worker_read_turn(lease,attempt_id(v.at("attempt_id")),deadline,owner_cancel,cancel).get();check();return encode_turn(result);}
            if(workspace&&method=="journal.commit_workspace_turn"){object(v,1);const auto result=store.commit_agent_worker_workspace_turn(lease,attempt_id(v.at("attempt_id")),deadline,owner_cancel,cancel).get();check();return encode_turn(result);}
            if(reserve||start||finish||complete){object(v,1);const auto attempt=attempt_id(v.at("attempt_id"));if(complete)return encode(store.complete_agent_worker_run(lease,attempt,deadline,owner_cancel,cancel).get());if(finish)return encode_attempt(store.finish_agent_worker_model(lease,attempt,deadline,owner_cancel,cancel).get());return encode_attempt(reserve?store.reserve_agent_worker_model(lease,attempt,deadline,owner_cancel,cancel).get():store.start_agent_worker_model(lease,attempt,deadline,owner_cancel,cancel).get());}
            const auto k=kind(method);object(v,2);AgentWorkerJournalRead query{k,text(v.at("target")),integer(v.at("after"))};const auto result=store.read_agent_worker_journal(lease,std::move(query)).get();check();return encode(result);}
        catch(const AgentWorkerRpcRemoteError&){throw;}
        catch(const AgentWorkerAuthorityCancelled&){throw AgentWorkerRpcRemoteError("cancelled");}catch(const AgentWorkerAuthorityExpired&){throw AgentWorkerRpcRemoteError("timeout");}
        catch(const RootBudgetExhausted&){throw AgentWorkerRpcRemoteError("budget_exhausted");}
        catch(const Conflict&){throw AgentWorkerRpcRemoteError("rejected");}catch(const NotFound&){throw AgentWorkerRpcRemoteError("rejected");}
        catch(const AgentWorkerProtocolError&){throw AgentWorkerRpcRemoteError("rejected");}catch(const std::invalid_argument&){throw AgentWorkerRpcRemoteError("rejected");}catch(const Json::exception&){throw AgentWorkerRpcRemoteError("rejected");}
    };
}
AgentWorkerRpcHandler bind_agent_worker_journal_rpc(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,AgentWorkerChannel::Deadline deadline,std::stop_token cancel){return bind_journal_rpc(store,std::move(lease),scope,deadline,cancel,false);}
AgentWorkerRpcHandler bind_agent_worker_workspace_journal_rpc(PersistenceService& store,AgentWorkerFileHost& files,const AgentWorkerScope& scope,AgentWorkerChannel::Deadline deadline,std::stop_token cancel){return bind_journal_rpc(store,files.lease(),scope,deadline,cancel,true);}

}
