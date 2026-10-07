// Actual repository transactions; completion/controller inputs are synthetic
// domain fixtures, not model or authenticated public graph-service evidence.
#include "agentflow/graph.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
using namespace agentflow;using Json=nlohmann::json;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected checkpoint rejection did not occur");}
int main(int argc,char** argv){if(argc!=4)return 2;try{
 const auto database=(std::filesystem::u8path(argv[1])/"checkpoint.sqlite").string();const std::vector<std::string> imports{argv[2],argv[3]};
 const GraphPlan plan(R"({"nodes":[{"id":"first","type":"agent","prompt":"fixture"},{"id":"gate","type":"human","prompt":"fixture decision","depends_on":["first"]},{"id":"accepted","type":"agent","prompt":"fixture","depends_on":["gate"],"when":{"node":"gate","path":["approved"],"equals":true}},{"id":"rejected","type":"agent","prompt":"fixture","depends_on":["gate"],"when":{"node":"gate","path":["approved"],"equals":false}},{"id":"tail","type":"agent","prompt":"fixture","depends_on":["rejected"]}]})");
 {
  PersistenceService store(database,imports);store.create_session("session","checkpoint fixture").get();store.start_graph_run("root","session","fixture",1,plan,R"({"content":"fixture root"})").get();store.transition("root",RunState::queued,RunState::running).get();
  const auto initial=store.graph_run("root").get();require(initial.checkpoint_revision==1,"Initial graph checkpoint must be durable");
  rejects<Conflict>([&]{store.start_graph_child("unready","root","accepted",R"({"content":"bad"})",1).get();});require(store.graph_run("root").get().checkpoint_revision==1,"Unready admission must not alter checkpoint");
  const auto first=store.start_graph_child("first-child","root","first",R"({"content":"fixture child"})",1).get();require(store.graph_run("root").get().checkpoint_revision==2,"Child ownership must commit with its checkpoint");
  rejects<Conflict>([&]{store.settle_graph_child(first.id).get();});store.transition(first.id,RunState::queued,RunState::running).get();store.complete_run(first.id,R"({"content":"Synthetic observed ledger completion"})").get();
  {XlangSqlite inject(database,imports);inject.execute("CREATE TRIGGER reject_graph_settlement BEFORE INSERT ON events WHEN NEW.kind='graph.child.settled' BEGIN SELECT RAISE(ABORT,'actual settlement event fixture failure'); END");}
  rejects<DatabaseError>([&]{store.settle_graph_child(first.id,2).get();});require(store.graph_run("root").get().checkpoint_revision==2,"Settlement checkpoint must roll back with its failed event");
  {XlangSqlite inject(database,imports);inject.execute("DROP TRIGGER reject_graph_settlement");}
  const auto settled=store.settle_graph_child(first.id,2).get();require(settled.checkpoint_revision==3,"Actual stored child completion must settle once");require(store.settle_graph_child(first.id,3).get().checkpoint_revision==3,"Duplicate settlement must be idempotent");
  rejects<Conflict>([&]{store.start_graph_human("root","gate",2).get();});const auto paused=store.start_graph_human("root","gate",3).get();require(paused.run.state==RunState::paused && paused.checkpoint_revision==4,"Human wait and root pause must commit together");
  rejects<Conflict>([&]{store.start_prompt_run("competitor","session",R"({"content":"bad"})").get();});store.close();
 }
 {
  PersistenceService store(database,imports);auto root=store.graph_run("root").get();require(root.run.state==RunState::paused && root.checkpoint_revision==4,"Actual restart must preserve human wait and completed work");
  rejects<Conflict>([&]{store.input_graph_human("root","gate",R"({"approved":false})","fixture-controller",3).get();});require(store.graph_run("root").get().run.state==RunState::paused,"Stale input must not unpause the root");
  {XlangSqlite inject(database,imports);inject.execute("CREATE TRIGGER reject_human_input BEFORE INSERT ON events WHEN NEW.kind='graph.human.input' BEGIN SELECT RAISE(ABORT,'actual human event fixture failure'); END");}
  rejects<DatabaseError>([&]{store.input_graph_human("root","gate",R"({"approved":true})","fixture-controller",4).get();});require(store.graph_run("root").get().checkpoint_revision==4 && store.graph_run("root").get().run.state==RunState::paused,"Failed input must preserve checkpoint and pause");
  {XlangSqlite inject(database,imports);inject.execute("DROP TRIGGER reject_human_input");}
  root=store.input_graph_human("root","gate",R"({"approved":true})","fixture-controller",4).get();require(root.run.state==RunState::running && root.checkpoint_revision==5,"Typed input and unpause must commit together");
  rejects<Conflict>([&]{store.start_graph_child("stale","root","accepted",R"({"content":"bad"})",4).get();});const auto accepted=store.start_graph_child("accepted-child","root","accepted",R"({"content":"fixture successor"})",5).get();store.transition(accepted.id,RunState::queued,RunState::running).get();store.complete_run(accepted.id,R"({"content":"Synthetic successor completion"})").get();root=store.settle_graph_child(accepted.id,6).get();
  rejects<Conflict>([&]{store.complete_run("root",R"({"content":"premature"})").get();});root=store.skip_graph_node("root","rejected",7).get();root=store.skip_graph_node("root","tail",8).get();require(GraphCoordinator(plan,root.checkpoint_json,GraphRestoreMode::live).inspect().finished,"Conditions and propagated skips must complete the durable coordinator");
  store.complete_run("root",R"({"content":"Synthetic ledger join","source":"fixture"})").get();require(store.run_history("first-child").get().size()==2,"Completed first child must never be repeated during resume");store.close();
 }
 {PersistenceService store(database,imports);const auto root=store.graph_run("root").get();require(root.run.state==RunState::completed && root.checkpoint_revision==9,"Finished checkpoint must reopen unchanged");store.close();}
 std::cout<<"Native durable graph checkpoint passed actual SQLite CAS, dependency admission, settlement/input event rollback, human pause/reopen/resume and conditional skips. Completion/input values are synthetic; public graph execution remains pending.\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
