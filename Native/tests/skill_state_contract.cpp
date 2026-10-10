// Actual embedded-xlang3 persistence. Activation acknowledgements and agent
// prompts are explicitly synthetic unit fixtures; no inference is claimed.
#include "agentflow/repository.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/graph.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <random>
using namespace agentflow;using Json=nlohmann::json;
namespace {
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected skill-state rejection absent");}
struct Directory{
    std::filesystem::path parent,path;
    Directory():parent(std::filesystem::canonical(std::filesystem::temp_directory_path())){std::random_device random;for(int i=0;i<32;i++){const auto candidate=parent/("xmind-skill-state-"+std::to_string(random())+"-"+std::to_string(random()));if(std::filesystem::create_directory(candidate)){path=std::filesystem::canonical(candidate);return;}}throw std::runtime_error("Cannot create owned skill-state fixture");}
    ~Directory(){std::error_code error;const auto actual=std::filesystem::weakly_canonical(path,error);if(!error&&actual==path&&actual.parent_path()==parent&&actual.filename().string().rfind("xmind-skill-state-",0)==0)std::filesystem::remove_all(actual,error);}
};
std::string assistant(const std::string& id,const std::string& skill,const std::string& name="load_skill"){return Json{{"tool_calls",Json::array({{{"id",id},{"name",name},{"arguments",Json{{"id",skill}}.dump()}}})}}.dump();}
std::vector<std::string> result(const std::string& call,const std::string& skill){return {Json{{"tool_call_id",call},{"content",Json{{"skill",{{"id",skill},{"model_invocable",true}}},{"activation","requested_for_next_model_request"},{"effect_permission",false}}.dump()}}.dump()};}
void running(Repository& store,const std::string& id,const std::string& session){store.start_prompt_run(id,session,R"({"content":"Synthetic native skill-state prompt"})");store.transition(id,RunState::queued,RunState::running);}
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
    Directory directory;const auto database=(directory.path/"state.sqlite").string();const std::vector<std::string> imports{argv[1],argv[2]};const std::string workspace="synthetic-unit-workspace-A";const SkillSelections one{workspace,{"inspect"}},two{workspace,{"inspect","review"}};
    {
        BackendLease lease(database);Repository store(database,imports);store.create_session("session","Synthetic typed activation state");running(store,"first","session");need(store.initialize_run_skills("first",workspace).ids.empty(),"New sessions cannot invent activation from metadata/history");
        rejects<Conflict>([&]{store.record_tool_turn("first",assistant("read","inspect","read_file"),result("read","inspect"),one);});need(store.run_skills("first").ids.empty()&&store.history("session").size()==1,"Foreign tool output cannot promote a skill selection");
        rejects<std::invalid_argument>([&]{store.record_tool_turn("first",assistant("load","inspect"),result("unmatched","inspect"),one);});
        rejects<std::invalid_argument>([&]{store.append_event("first","skill.selection.changed",R"({"ids":["inspect"]})");});
        store.record_tool_turn("first",assistant("load","inspect"),result("load","inspect"),one);need(store.run_skills("first").ids==one.ids,"Native acknowledgement must commit selection and transcript together");store.complete_run("first",R"({"content":"Synthetic completed state fixture"})");
        running(store,"second","session");need(store.initialize_run_skills("second",workspace).ids==one.ids,"Next prompt must restore the native session selection");const auto history=store.history("session").size(),events=store.events("second").size();
        {XlangSqlite faults(database,imports);faults.execute("CREATE TRIGGER reject_skill_tool_message BEFORE INSERT ON messages WHEN NEW.role='tool' BEGIN SELECT RAISE(ABORT,'synthetic atomic skill fixture'); END");}
        rejects<DatabaseError>([&]{store.record_tool_turn("second",assistant("review","review"),result("review","review"),two);});need(store.run_skills("second").ids==one.ids&&store.history("session").size()==history&&store.events("second").size()==events,"Failed tool commit must roll back run/session selections and events/messages");
        {XlangSqlite faults(database,imports);faults.execute("DROP TRIGGER reject_skill_tool_message");}
        rejects<Conflict>([&]{store.initialize_run_skills("second","synthetic-unit-workspace-B");});
    }
    {
        BackendLease lease(database);Repository store(database,imports);need(store.initialize_run_skills("second",workspace).ids==one.ids,"Owned run selection must survive repository close/reopen without inferring history");
        store.record_tool_turn("second",assistant("review","review"),result("review","review"),two);store.complete_run("second",R"({"content":"Synthetic second completed fixture"})");
        const GraphPlan plan(R"({"nodes":[{"id":"inspect","type":"agent","prompt":"Synthetic child state fixture"}]})");store.start_graph_run("graph","session","fixture-skills",1,plan,R"({"content":"Synthetic graph owner"})");store.transition("graph",RunState::queued,RunState::running);store.start_graph_child("child","graph","inspect",R"({"content":"Synthetic owned child"})");store.transition("child",RunState::queued,RunState::running);
        need(store.initialize_run_skills("child",workspace).ids==two.ids,"Owned graph agent must inherit this workspace's session selection");const SkillSelections child{workspace,{"inspect","review","child-only"}};store.record_tool_turn("child",assistant("child-load","child-only"),result("child-load","child-only"),child);store.complete_run("child",R"({"content":"Synthetic actual typed child conclusion"})");store.settle_graph_child("child");store.complete_run("graph",R"({"content":"Synthetic typed join"})");
        running(store,"third","session");need(store.initialize_run_skills("third",workspace).ids==two.ids,"Child activation must not leak into the parent session selection");
        const SkillSelections invalid{workspace,{"inspect","inspect"}};rejects<std::invalid_argument>([&]{store.record_tool_turn("third",assistant("same","inspect"),result("same","inspect"),invalid);});need(store.run_skills("third").ids==two.ids,"Rejected selection must preserve the actual restored state");store.complete_run("third",R"({"content":"Synthetic third completed fixture"})");
        running(store,"other-scope","session");need(store.initialize_run_skills("other-scope","synthetic-unit-workspace-B").ids.empty(),"Different physical workspace identity cannot inherit these guides");store.complete_run("other-scope",R"({"content":"Synthetic other scope"})");
        const auto observed=store.session_skills("session",workspace);need(observed.editable&&observed.selections.ids==two.ids&&observed.selections.manual_ids.empty(),"Model selection must retain its provenance in editable session metadata");
        const SkillSelections explicit_selection{workspace,{"user-disabled"},{"user-disabled"}};const auto attached=store.replace_session_skills("session",explicit_selection,observed.revision);need(attached.revision==observed.revision+1&&attached.selections.manual_ids==explicit_selection.ids,"Explicit selection must publish exactly one native revision");
        rejects<Conflict>([&]{store.replace_session_skills("session",{workspace,{},{}},observed.revision);});rejects<std::invalid_argument>([&]{store.replace_session_skills("session",{workspace,{"invented"},{}},attached.revision);});
        running(store,"manual-run","session");need(!store.session_skills("session",workspace).editable,"Running session selection cannot be edited");rejects<Conflict>([&]{store.replace_session_skills("session",{workspace,{},{}},attached.revision);});const auto restored_manual=store.initialize_run_skills("manual-run",workspace);need(restored_manual.manual_ids==explicit_selection.ids,"A fresh run must inherit explicit user provenance");
        auto promoted=restored_manual;promoted.ids.push_back("model-loaded");promoted.manual_ids.push_back("model-loaded");rejects<Conflict>([&]{store.record_tool_turn("manual-run",assistant("promote","model-loaded"),result("promote","model-loaded"),promoted);});need(store.run_skills("manual-run").manual_ids==explicit_selection.ids,"A matched model acknowledgement cannot promote manual privilege");
        store.transition("manual-run",RunState::running,RunState::paused);rejects<Conflict>([&]{store.replace_session_skills("session",{workspace,{},{}},attached.revision);});store.transition("manual-run",RunState::paused,RunState::running);store.complete_run("manual-run",R"({"content":"Synthetic explicit selection owner"})");
        const auto cleared=store.replace_session_skills("session",{workspace,{},{}},attached.revision);need(cleared.selections.ids.empty()&&cleared.revision==attached.revision+1,"Clear must be a retained revision rather than deleting authority");
        rejects<NotFound>([&]{store.session_skills("absent",workspace);});rejects<std::invalid_argument>([&]{store.replace_session_skills("session",{workspace,{"x"},{"not-selected"}},cleared.revision);});
        store.create_session("rollback","Explicit selection rollback");{XlangSqlite faults(database,imports);faults.execute("CREATE TRIGGER reject_manual_skill BEFORE INSERT ON session_skills WHEN NEW.session_id='rollback' BEGIN SELECT RAISE(ABORT,'synthetic user selection rollback'); END");}
        rejects<DatabaseError>([&]{store.replace_session_skills("rollback",explicit_selection,0);});need(store.session_skills("rollback",workspace).revision==0,"Failed user selection publication must preserve absence and revision");{XlangSqlite faults(database,imports);faults.execute("DROP TRIGGER reject_manual_skill");}
        store.replace_session_skills("rollback",explicit_selection,0);
        XlangSqlite inspect(database,imports);need(std::get<std::int64_t>(inspect.execute("PRAGMA user_version").rows[0][0])==14&&inspect.execute("PRAGMA foreign_key_check").rows.empty(),"Schema14 typed skill and agent selection ownership must retain valid foreign keys");
    }
    {BackendLease lease(database);Repository reopened(database,imports);need(reopened.session_skills("rollback",workspace).selections.manual_ids==std::vector<std::string>{"user-disabled"},"Manual provenance must survive actual SQLite repository close/reopen");}
    std::cout<<"Native skill persistence passed embedded-xlang3 atomic activation/rollback, actual reopen, continued-session restoration, typed graph-child isolation and workspace binding. Acknowledgements/prompts are synthetic unit fixtures; no inference claimed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
