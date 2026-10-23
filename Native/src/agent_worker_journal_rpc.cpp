#include "agent_worker_journal_rpc_values.hpp"
namespace agentflow {
Run AgentWorkerJournalClient::run(const std::string& id){return std::get<Run>(call(peer_,"journal.run",id));}
Session AgentWorkerJournalClient::session(const std::string& id){return std::get<Session>(call(peer_,"journal.session",id));}
std::vector<Message> AgentWorkerJournalClient::run_history(const std::string& id){return std::get<std::vector<Message>>(call(peer_,"journal.history",id));}
std::vector<Event> AgentWorkerJournalClient::events(const std::string& id,std::int64_t after){return std::get<std::vector<Event>>(call(peer_,"journal.events",id,after));}
RootBudgetRecord AgentWorkerJournalClient::root_budget(const std::string& id){return std::get<RootBudgetRecord>(call(peer_,"journal.root_budget",id));}
ChildAdmissionRecord AgentWorkerJournalClient::child_admission(const std::string& id){
    if(id!=peer_.scope().run_id||id==peer_.scope().root_id)throw Conflict("Worker child admission differs from its captured owner");
    auto result=std::get<ChildAdmissionRecord>(call(peer_,"journal.child_admission",id));if(result.root_run_id!=peer_.scope().root_id)invalid();return result;
}
bool AgentWorkerJournalClient::coding_workspace_admitted(){
    try{const auto value=parse((peer_.native_model()?peer_.native_model()->journal_workspace_mode():peer_.call("journal.workspace_mode","{}")));object(value,1);if(!value.at("coding").is_boolean())invalid();return value.at("coding").get<bool>();}
    catch(const Json::exception&){invalid();}catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker coding admission rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker coding admission cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker coding admission elapsed");throw;}
}
AgentWorkerChannel::Deadline AgentWorkerJournalClient::execution_deadline(){
    const auto before=std::chrono::steady_clock::now();
    try{const auto value=parse((peer_.native_model()?peer_.native_model()->journal_execution_window():peer_.call("journal.execution_window","{}")));object(value,1);const auto remaining=integer(value.at("remaining_ms"));if(remaining<0||remaining>3600000)invalid();return before+std::chrono::milliseconds(remaining);}
    catch(const Json::exception&){invalid();}catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker execution window rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker execution window cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker execution window elapsed");throw;}
}
Run AgentWorkerJournalClient::begin_run(){
    try{const auto result=std::get<Run>(decode(AgentWorkerJournalReadKind::run,(peer_.native_model()?peer_.native_model()->journal_begin():peer_.call("journal.begin_run","{}"))));if(result.id!=peer_.scope().run_id||result.state!=RunState::running)invalid();return result;}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker startup rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker startup cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker startup deadline elapsed");throw;}
}
Event AgentWorkerJournalClient::acknowledge_model_event(const std::string& attempt,std::int64_t ordinal,const std::string& kind,const std::string& json){
    try{const auto values=std::get<std::vector<Event>>(decode(AgentWorkerJournalReadKind::events,(peer_.native_model()?peer_.native_model()->journal_model_event(attempt,ordinal,kind,json):peer_.call("journal.model_event",Json{{"attempt_id",attempt},{"ordinal",ordinal},{"kind",kind},{"json",json}}.dump()))));if(values.size()!=1||values[0].run_id!=peer_.scope().run_id||values[0].kind!=kind||values[0].json!=json)invalid();return values[0];}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker model event acknowledgement rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker model event acknowledgement cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker model event acknowledgement deadline elapsed");throw;}
}
Event AgentWorkerJournalClient::acknowledge_tool_event(const std::string& attempt,std::int64_t ordinal,bool returned,const std::string& kind,const std::string& json){
    try{const auto values=std::get<std::vector<Event>>(decode(AgentWorkerJournalReadKind::events,(peer_.native_model()?peer_.native_model()->journal_tool_event(attempt,ordinal,returned):peer_.call("journal.tool_event",Json{{"attempt_id",attempt},{"ordinal",ordinal},{"phase",returned?"returned":"started"}}.dump()))));if(values.size()!=1||values[0].run_id!=peer_.scope().run_id||values[0].kind!=kind||values[0].json!=json)throw Conflict("Worker tool activity differs from its backend fact");return values[0];}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker tool activity rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker tool activity cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker tool activity deadline elapsed");throw;}
}
Event AgentWorkerJournalClient::acknowledge_workspace_tool_event(const std::string& attempt,std::int64_t ordinal,bool returned,const std::string& kind,const std::string& json){
    try{const auto values=std::get<std::vector<Event>>(decode(AgentWorkerJournalReadKind::events,(peer_.native_model()?peer_.native_model()->journal_workspace_tool_event(attempt,ordinal,returned):peer_.call("journal.workspace_tool_event",Json{{"attempt_id",attempt},{"ordinal",ordinal},{"phase",returned?"returned":"started"}}.dump()))));if(values.size()!=1||values[0].run_id!=peer_.scope().run_id||values[0].kind!=kind||values[0].json!=json)throw Conflict("Worker tool activity differs from its backend fact");return values[0];}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker tool activity rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker tool activity cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker tool activity deadline elapsed");throw;}
}
ModelCallReservation AgentWorkerJournalClient::reserve_model_call(const std::string& attempt){return attempt_call(peer_,"journal.reserve_model",attempt,"reserved");}
ModelCallReservation AgentWorkerJournalClient::reserve_planning_continuation(const std::string& call,const std::string& attempt){if(!peer_.native_model())throw Conflict("Planning continuation requires the native SDK service");return attempt_call(peer_,"journal.reserve_planning_continuation",attempt,"reserved",call);}
ModelCallReservation AgentWorkerJournalClient::start_model_call(const std::string& attempt){return attempt_call(peer_,"journal.start_model",attempt,"started");}
ModelCallReservation AgentWorkerJournalClient::finish_model_call(const std::string& attempt){return attempt_call(peer_,"journal.finish_model",attempt,"finished");}
AgentWorkerToolTurnRecord AgentWorkerJournalClient::commit_read_turn(const std::string& attempt){
    try{return decode_turn(peer_.scope(),attempt,(peer_.native_model()?peer_.native_model()->journal_commit_read_turn(attempt):peer_.call("journal.commit_read_turn",Json{{"attempt_id",attempt}}.dump())));}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker read turn rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker read turn cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker read turn deadline elapsed");throw;}
}
AgentWorkerToolTurnRecord AgentWorkerJournalClient::commit_workspace_turn(const std::string& attempt){
    try{return decode_turn(peer_.scope(),attempt,(peer_.native_model()?peer_.native_model()->journal_commit_workspace_turn(attempt):peer_.call("journal.commit_workspace_turn",Json{{"attempt_id",attempt}}.dump())));}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker workspace turn rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker workspace turn cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker workspace turn deadline elapsed");throw;}
}
Run AgentWorkerJournalClient::complete_run(const std::string& attempt){
    try{const auto value=std::get<Run>(decode(AgentWorkerJournalReadKind::run,(peer_.native_model()?peer_.native_model()->journal_complete(attempt):peer_.call("journal.complete_run",Json{{"attempt_id",attempt}}.dump()))));if(value.id!=peer_.scope().run_id||value.state!=RunState::completed)invalid();return value;}
    catch(const AgentWorkerRpcRemoteError& e){if(e.code()=="rejected")throw Conflict("Worker completion rejected");if(e.code()=="cancelled")throw AgentWorkerRpcCancelled("Worker completion cancelled");if(e.code()=="timeout")throw AgentWorkerRpcTimeout("Worker completion deadline elapsed");throw;}
}
}
