#include "agentflow/graph.hpp"
#include "agentflow/agent_runner.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <future>
#include <iostream>
using namespace agentflow;using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected child-execution rejection did not occur");}
int main(int argc,char** argv){if(argc!=5)return 2;try{
 const auto database=(std::filesystem::u8path(argv[1])/"children.sqlite").string();const std::vector<std::string> imports{argv[2],argv[3]};
 const GraphPlan plan(R"({"nodes":[{"id":"left","type":"agent","prompt":"left"},{"id":"right","type":"agent","prompt":"right"}]})");
 {
  PersistenceService store(database,imports);store.create_session("root-session","actual child execution with synthetic inference").get();
  const auto root=store.start_graph_run("graph-root","root-session","fixture",1,plan,R"({"content":"root-private-prompt"})").get();require(root.graph_root,"Graph root identity must be explicit");
  AgentSettings settings;settings.provider.model="fixture-model";settings.provider.endpoint=argv[4];settings.provider.tools=Capability::supported;settings.workspace=argv[1];AgentRunner runner(store,settings);
  rejects<std::invalid_argument>([&]{runner.execute(root.id);});require(store.run(root.id).get().state==RunState::queued,"Single-agent executor must not claim a graph root");
  rejects<Conflict>([&]{store.start_graph_child("too-early",root.id,"left",R"({"content":"early"})").get();});
  store.transition(root.id,RunState::queued,RunState::running).get();
  const auto before=store.events(root.id).get().size();
  {XlangSqlite inject(database,imports);inject.execute("CREATE TRIGGER reject_child_event BEFORE INSERT ON events WHEN NEW.kind='graph.child.queued' BEGIN SELECT RAISE(ABORT,'actual child event fixture failure'); END");}
  rejects<DatabaseError>([&]{store.start_graph_child("faulted",root.id,"left",R"({"content":"faulted-private"})").get();});rejects<NotFound>([&]{store.run("faulted").get();});require(store.children(root.id).get().empty() && store.events(root.id).get().size()==before,"Child/message/events must roll back together");
  {XlangSqlite inject(database,imports);inject.execute("DROP TRIGGER reject_child_event");}
  const auto left=store.start_graph_child("left-run",root.id,"left",R"({"content":"child-left"})").get();const auto right=store.start_graph_child("right-run",root.id,"right",R"({"content":"child-right"})").get();
  require(left.parent_id==root.id && right.session_id==root.session_id,"Children must belong to the root session");require(store.runs(root.session_id).get().size()==1,"Root run listing must not present children as competing roots");
  rejects<Conflict>([&]{store.start_prompt_run("competing-root",root.session_id,R"({"content":"bad"})").get();});rejects<Conflict>([&]{store.start_graph_child("duplicate-node",root.id,"left",R"({"content":"bad"})").get();});
  rejects<Conflict>([&]{store.transition(root.id,RunState::running,RunState::paused).get();});rejects<Conflict>([&]{store.transition(root.id,RunState::running,RunState::cancelled).get();});rejects<Conflict>([&]{store.complete_run(root.id,R"({"content":"premature"})").get();});
  auto a=std::async(std::launch::async,[&]{return runner.execute(left.id);});auto b=std::async(std::launch::async,[&]{return runner.execute(right.id);});require(a.get().state==RunState::completed && b.get().state==RunState::completed,"Both native child agent loops must finish");
  const auto leftHistory=store.run_history(left.id).get(),rightHistory=store.run_history(right.id).get();require(leftHistory.size()==4 && rightHistory.size()==4,"Child tool conversations must remain isolated");require(store.history(root.session_id).get().size()==1,"Child conversations must not leak into root history");
  require(Json::parse(leftHistory.front().json).at("content")=="child-left" && Json::parse(rightHistory.front().json).at("content")=="child-right","Each child must retain its own prompt");
  const auto tail=store.graph_events(root.id).get();bool l=false,r=false;std::int64_t cursor=0;for(const auto& event:tail){require(event.sequence>cursor,"Graph cursor must preserve global committed order");cursor=event.sequence;if(event.run_id==left.id)l=true;if(event.run_id==right.id)r=true;}require(l&&r,"Root observation must include identified child events");require(store.graph_events(root.id,cursor).get().empty(),"Committed graph cursor must not replay old rows");
  store.complete_run(root.id,Json{{"content",Json::parse(leftHistory.back().json).at("content").get<std::string>()+" / "+Json::parse(rightHistory.back().json).at("content").get<std::string>()},{"source","fixture-native-join"}}.dump()).get();
  require(store.history(root.session_id).get().size()==2,"Root join must append only its own final record");
  store.create_session("interrupted-session","synthetic interrupted ownership").get();store.start_graph_run("interrupted-root","interrupted-session","fixture",1,plan,R"({"content":"interrupted-root"})").get();store.transition("interrupted-root",RunState::queued,RunState::running).get();store.start_graph_child("interrupted-child","interrupted-root","left",R"({"content":"retained-private-child"})").get();store.transition("interrupted-child",RunState::queued,RunState::running).get();store.close();
 }
 {PersistenceService store(database,imports);require(store.graph_run("graph-root").get().specification_json==plan.json(),"Immutable graph specification must survive actual reopen");require(store.run_history("left-run").get().size()==4 && store.history("root-session").get().size()==2,"Private/root histories must survive reopen independently");require(store.run("interrupted-root").get().state==RunState::failed && store.run("interrupted-child").get().state==RunState::failed,"Owner recovery must retire interrupted root and child admission");require(store.run_history("interrupted-child").get().size()==1,"Interrupted recovery must preserve private child inputs");store.close();}
 std::cout<<"Native graph children passed actual concurrent agent transport/tool reads, isolated conversations, atomic admission rollback, root boundaries, identified cursor/reopen and owner recovery. Inference and interrupted ownership fixture are synthetic; no public graph service claimed.\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
