#include "agent_worker_rpc_fixture.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
using Json=nlohmann::json;
std::string FixtureWorkerRpcPeer::journal_run(std::string id){return this->call("journal.run",Json{{"target",id},{"after",0}}.dump());}
std::string FixtureWorkerRpcPeer::journal_session(std::string id){return this->call("journal.session",Json{{"target",id},{"after",0}}.dump());}
std::string FixtureWorkerRpcPeer::journal_history(std::string id){return this->call("journal.history",Json{{"target",id},{"after",0}}.dump());}
std::string FixtureWorkerRpcPeer::journal_events(std::string id,std::int64_t after){return this->call("journal.events",Json{{"target",id},{"after",after}}.dump());}
std::string FixtureWorkerRpcPeer::journal_root_budget(std::string id){return this->call("journal.root_budget",Json{{"target",id},{"after",0}}.dump());}
std::string FixtureWorkerRpcPeer::journal_child_admission(std::string id){return this->call("journal.child_admission",Json{{"target",id},{"after",0}}.dump());}
std::string FixtureWorkerRpcPeer::journal_workspace_mode(){return this->call("journal.workspace_mode",Json::object().dump());}
std::string FixtureWorkerRpcPeer::journal_execution_window(){return this->call("journal.execution_window",Json::object().dump());}
std::string FixtureWorkerRpcPeer::journal_begin(){return this->call("journal.begin_run",Json::object().dump());}
std::string FixtureWorkerRpcPeer::journal_reserve_model(std::string attempt){return this->call("journal.reserve_model",Json{{"attempt_id",attempt}}.dump());}
std::string FixtureWorkerRpcPeer::journal_start_model(std::string attempt){return this->call("journal.start_model",Json{{"attempt_id",attempt}}.dump());}
std::string FixtureWorkerRpcPeer::journal_finish_model(std::string attempt){return this->call("journal.finish_model",Json{{"attempt_id",attempt}}.dump());}
std::string FixtureWorkerRpcPeer::journal_commit_read_turn(std::string attempt){return this->call("journal.commit_read_turn",Json{{"attempt_id",attempt}}.dump());}
std::string FixtureWorkerRpcPeer::journal_commit_workspace_turn(std::string attempt){return this->call("journal.commit_workspace_turn",Json{{"attempt_id",attempt}}.dump());}
std::string FixtureWorkerRpcPeer::journal_complete(std::string attempt){return this->call("journal.complete_run",Json{{"attempt_id",attempt}}.dump());}
std::string FixtureWorkerRpcPeer::journal_reserve_planning_continuation(std::string call,std::string attempt){return this->call("journal.reserve_planning_continuation",Json{{"attempt_id",attempt},{"call_id",call}}.dump());}
std::string FixtureWorkerRpcPeer::journal_model_event(std::string attempt,std::int64_t ordinal,std::string kind,std::string json){return this->call("journal.model_event",Json{{"attempt_id",attempt},{"ordinal",ordinal},{"kind",kind},{"json",json}}.dump());}
std::string FixtureWorkerRpcPeer::journal_tool_event(std::string attempt,std::int64_t ordinal,bool returned){return this->call("journal.tool_event",Json{{"attempt_id",attempt},{"ordinal",ordinal},{"phase",returned?"returned":"started"}}.dump());}
std::string FixtureWorkerRpcPeer::journal_workspace_tool_event(std::string attempt,std::int64_t ordinal,bool returned){return this->call("journal.workspace_tool_event",Json{{"attempt_id",attempt},{"ordinal",ordinal},{"phase",returned?"returned":"started"}}.dump());}
}
