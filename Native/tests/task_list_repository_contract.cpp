// Storage fixtures exercise the actual C++/xlang3 repository. Explicit lifecycle
// writes below are test-only; they are not public agent execution endpoints.
#include "agentflow/repository.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/graph.hpp"
#include "graph_schema_fixture.hpp"
#include <filesystem>
#include <chrono>
#include <random>
#include <iostream>
using namespace agentflow;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected rejection");}
std::int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
struct Directory{std::filesystem::path path=std::filesystem::temp_directory_path()/("xmind-task-list-"+std::to_string(std::random_device{}()));Directory(){std::filesystem::create_directory(path);}~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}};
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
 Directory directory;const auto database=(directory.path/"state.sqlite").string();const std::vector<std::string> roots{argv[1],argv[2]};
 {
  Repository store(database,roots);XlangSqlite inspect(database,roots);
  inspect.execute("CREATE TRIGGER reject_clock BEFORE INSERT ON run_status_clock BEGIN SELECT RAISE(ABORT,'fixture status clock fault'); END");
  rejects<DatabaseError>([&]{store.start_incoming_message("rollback","","fault",R"({"content":"fixture"})","{}");});
  for(const auto* table:{"sessions","runs","events","messages","incoming_messages","run_status_clock"})require(inspect.execute(std::string("SELECT * FROM ")+table).rows.empty(),"Clock failure must roll back admission and its journal");
  inspect.execute("DROP TRIGGER reject_clock");store.create_session("context","Storage fixture context");
  const auto before=now();
  for(const auto* id:{"first","second"}){store.start_prompt_run(id,"context",R"({"content":"fixture prompt"})");store.transition(id,RunState::queued,RunState::running);store.complete_run(id,R"({"content":"synthetic storage fixture reply"})");}
  auto page=store.list_root_runs("","",{},1);require(page.total==2&&page.more&&page.entries[0].run.id=="second","Newest status must be first with cursor pagination");
  const auto position=page.entries[0];require(position.status_ms&&*position.status_ms>=before&&*position.status_ms<=now(),"Status time must be actually measured rather than invented");
  store.start_prompt_run("third","context",R"({"content":"new after watermark"})");
  auto next=store.list_root_runs("","",{},1,page.watermark,position.status_ms,position.status_sequence);require(next.total==2&&!next.more&&next.entries[0].run.id=="first","Later admission cannot enter an existing page sequence");
  store.transition("third",RunState::queued,RunState::cancelled);
  require(store.list_root_runs("context","completed",{},100).total==2,"Context/state filtering must precede pagination");
  require(store.list_root_runs("","",now()+60000,100).entries.empty(),"Future timestamp filter cannot include earlier status updates");
  rejects<std::invalid_argument>([&]{store.list_root_runs("","",{},0);});rejects<std::invalid_argument>([&]{store.list_root_runs("","",{},101);});
  rejects<std::invalid_argument>([&]{store.list_root_runs("","",{},1,page.watermark+100000,position.status_ms,position.status_sequence);});
  store.create_session("graph-context","Synthetic storage graph");const GraphPlan graph(R"({"nodes":[{"id":"node","type":"agent","prompt":"Synthetic storage child"}]})");
  store.start_graph_run("graph","graph-context","fixture-graph",1,graph,R"({"content":"storage graph prompt"})");store.transition("graph",RunState::queued,RunState::running);store.start_graph_child("internal-child","graph","node",R"({"content":"storage child prompt"})");
  require(store.list_root_runs("","",{},100).total==4,"Internal graph children must never appear as root tasks");
  store.transition("internal-child",RunState::queued,RunState::cancelled);store.settle_graph_child("internal-child");store.retire_graph_run("graph",RunState::cancelled,"{}");
 }
 {
  XlangSqlite old(database,roots);remove_delegation_schema_fixture(old);old.execute("DROP TABLE run_status_clock");old.execute("PRAGMA user_version=8");
 }
 {
  Repository migrated(database,roots);auto page=migrated.list_root_runs("","",{},100);require(page.total==4,"Migration must retain historical task visibility");
  for(const auto& entry:page.entries)require(!entry.status_ms,"Migration must never fabricate historical wall-clock times");
  require(page.entries[0].run.id=="graph","Historical rows retain actual journal ordering");
  rejects<StatusTimeUnavailable>([&]{migrated.list_root_runs("","",0,100);});
  migrated.create_session("new-context","After status-clock migration");migrated.start_prompt_run("new-task","new-context",R"({"content":"new fixture prompt"})");
  require(migrated.list_root_runs("new-context","",0,100).entries[0].status_ms.has_value(),"New statuses must record real times after migration");
  migrated.transition("new-task",RunState::queued,RunState::running);XlangSqlite inject(database,roots);
  inject.execute("CREATE TRIGGER reject_status_update BEFORE UPDATE ON run_status_clock BEGIN SELECT RAISE(ABORT,'fixture clock update fault'); END");
  const auto prior=migrated.list_root_runs("new-context","",{},100).entries[0];
  rejects<DatabaseError>([&]{migrated.complete_run("new-task",R"({"content":"must roll back"})");});
  require(migrated.run("new-task").state==RunState::running&&migrated.history("new-context").size()==1,"Clock fault must not report or persist completion");
  require(migrated.list_root_runs("new-context","",{},100).entries[0].status_sequence==prior.status_sequence,"Clock fault must preserve the prior status cursor");
  inject.execute("DROP TRIGGER reject_status_update");migrated.complete_run("new-task",R"({"content":"synthetic committed fixture reply"})");
 }
 {Repository restarted(database,roots);require(restarted.list_root_runs("new-context","completed",0,100).entries.size()==1,"Status clock/filtering must survive restart");}
 std::cout<<"Native task listing storage passed actual status times, atomic clock faults, context/state/time filtering, cursor watermark, root-only visibility, restart and schema8 migration without fictional historical timestamps. Lifecycle replies are synthetic storage fixtures.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
