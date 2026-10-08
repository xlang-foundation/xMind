#include "agentflow/repository.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "graph_schema_fixture.hpp"
#include <filesystem>
#include <iostream>
#include <random>
using namespace agentflow;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action> void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected rejection");}
struct Directory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("xmind-incoming-"+std::to_string(std::random_device{}()));
    Directory(){std::filesystem::create_directory(path);}
    ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try{
        Directory directory;const auto db=(directory.path/"state.sqlite").string();const std::vector<std::string> roots{argv[1],argv[2]};
        const std::string identity=R"({"fixture":"synthetic repository admission metadata"})";
        {
            Repository store(db,roots);XlangSqlite inspect(db,roots);
            inspect.execute("CREATE TRIGGER reject_incoming BEFORE INSERT ON incoming_messages BEGIN SELECT RAISE(ABORT,'fixture fault'); END");
            rejects<DatabaseError>([&]{store.start_incoming_message("rollback","","fault",R"({"content":"first"})",identity);});
            for(const auto* table:{"sessions","runs","messages","task_history_owners","task_messages","incoming_messages","events"})
                require(inspect.execute(std::string("SELECT * FROM ")+table).rows.empty(),"Failed admission must roll back all rows");
            inspect.execute("DROP TRIGGER reject_incoming");
            const auto first=store.start_incoming_message("first","","message-1",R"({"content":"first"})",identity);
            require(first.session_id=="ctx_first","Admission must create a persistent context");
            require(store.start_incoming_message("retry","","message-1",R"({"content":"first"})",identity).id=="first","Retry must reuse the original task");
            require(store.runs("ctx_first").size()==1&&store.history("ctx_first").size()==1,"Retry must not insert run or prompt");
            rejects<Conflict>([&]{store.incoming_message("message-1","",identity,"changed");});
            rejects<Conflict>([&]{store.start_incoming_message("changed","","message-1",R"({"content":"changed"})",identity);});
            rejects<Conflict>([&]{store.incoming_message("message-1","different",identity,"first");});
            rejects<Conflict>([&]{store.incoming_message("message-1","",R"({"fixture":"changed"})","first");});
            rejects<Conflict>([&]{store.start_incoming_message("busy","ctx_first","message-2",R"({"content":"second"})",identity);});
            require(!store.incoming_message("message-2","",identity,"second"),"Rejected admission cannot reserve its message ID");
            // Explicit lifecycle writes belong only to this storage contract fixture.
            store.transition("first",RunState::queued,RunState::running);store.complete_run("first",R"({"content":"fixture first reply"})");
            store.start_incoming_message("second","ctx_first","message-2",R"({"content":"second"})",identity);
            store.transition("second",RunState::queued,RunState::running);store.complete_run("second",R"({"content":"fixture second reply"})");
            require(store.history("ctx_first").size()==4&&store.run_history("second").size()==4,"Agent must retain prior conversation context");
            const auto history=store.task_history("first");require(history&&history->size()==2&&history->at(1).json.find("first reply")!=std::string::npos,"Task history must exclude later replies");
        }
        {
            Repository restarted(db,roots);
            require(restarted.incoming_message("message-1","ctx_first",identity,"first")->state==RunState::completed,"Restart must retain completed replay mapping");
            require(restarted.start_incoming_message("restart_retry","","message-1",R"({"content":"first"})",identity).id=="first","Restart retry must not execute again");
            require(restarted.task_history("second")->size()==2,"Restart must retain task history ownership");
        }
        {
            XlangSqlite old(db,roots);remove_delegation_schema_fixture(old);old.execute("DROP TABLE run_status_clock");old.execute("DROP TABLE incoming_messages");old.execute("DROP TABLE task_messages");old.execute("DROP TABLE task_history_owners");old.execute("PRAGMA user_version=7");
        }
        {
            Repository migrated(db,roots);require(!migrated.task_history("first"),"Migration must not guess legacy message attribution");
            require(migrated.history("ctx_first").size()==4,"Migration must preserve legacy conversation context");
            migrated.start_prompt_run("new_after_migration","ctx_first",R"({"content":"new prompt"})");
            require(migrated.task_history("new_after_migration")->size()==1,"New tasks must have provable history after migration");
        }
        std::cout<<"Native incoming storage contract passed atomic rollback, durable retries, changed-input rejection, task isolation and legacy migration. Lifecycle replies are synthetic storage fixtures.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
