#include "agentflow/repository.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "graph_schema_fixture.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>

using namespace agentflow;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Function> void rejects(Function action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected rejection did not occur");
}
struct Directory {
    std::filesystem::path path;
    Directory() {
        std::random_device random;
        for(int i=0;i<32;++i) {
            auto candidate=std::filesystem::temp_directory_path()/("xmind-repository-"+std::to_string(random())+"-"+std::to_string(random()));
            if(std::filesystem::create_directory(candidate)) {path=std::move(candidate);return;}
        }
        throw std::runtime_error("Cannot create test directory");
    }
    ~Directory() {std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
#if defined(_WIN32)
void long_backend_lease(const Directory& directory,const std::vector<std::string>& roots) {
    // Real native lease and embedded xlang3 SQLite at the product failure's
    // boundary: database254 UTF-16 units, unchanged sidecar267 (>MAX_PATH).
    auto parent=directory.path/"long-backend";const std::filesystem::path name="state.sqlite";
    require((parent/name).native().size()<254,"Owned temporary prefix must permit the exact long-path case");
    while((parent/name).native().size()+60<254)parent/=std::wstring(40,L'x');
    const auto needed=254-(parent/name).native().size();
    std::filesystem::path filename=name;
    if(needed>=2)parent/=std::wstring(needed-1,L'y');else if(needed==1)filename="xstate.sqlite";
    const auto database=parent/filename;std::filesystem::create_directories(parent);
    require(database.native().size()==254,"Long database spelling must remain254 units");
    auto sidecar=database;sidecar+=".backend-lock";
    require(sidecar.native().size()==267,"Lease sidecar must retain original267-unit spelling");
    const auto encoded=database.u8string();const std::string path(encoded.begin(),encoded.end());
    const auto extended=std::filesystem::path(L"\\\\?\\"+database.native()).u8string();const std::string extended_path(extended.begin(),extended.end());
    {
        BackendLease owner(path);require(owner.covers(path),"Long lease must cover its public canonical database");
        rejects<Conflict>([&]{BackendLease competing(path);});
        rejects<Conflict>([&]{BackendLease competing(extended_path);});
        Repository persisted(path,roots);persisted.create_session("long-session","Native long lease fixture");
        persisted.append_message("long-session","user",R"({"content":"Synthetic persistence marker; no model or keys"})");
        persisted.put_information("long-lease","marker",R"({"retained":true})");
        require(owner.covers(extended_path),"Existing extended spelling must cover the same physical database");
        require(!owner.covers((parent/"other.sqlite").string()),"Long lease must not cover another database");
    }
    {
        BackendLease reopened(extended_path);require(reopened.covers(path),"Released long lease must reopen through extended spelling");
        rejects<Conflict>([&]{BackendLease competing(path);});
        Repository persisted(path,roots);
        require(persisted.history("long-session").size()==1,"Long database conversation must survive lease release/reopen");
        require(persisted.information("long-lease","marker")==R"({"retained":true})","Long database information must survive reopen");
    }
    BackendLease final_owner(path);require(final_owner.covers(path),"Extended-owner release must allow ordinary spelling again");
}
#endif
}
int main(int argc,char** argv) {
    if(argc!=3) {std::cerr<<"Expected package and stdlib-source roots\n";return 2;}
    try {
        Directory directory; const auto path=(directory.path/"state.sqlite").string();
        const std::vector<std::string> roots{argv[1],argv[2]};
        const auto migration_path=(directory.path/"v19-inbox.sqlite").string();
        {
            BackendLease owner(migration_path);Repository legacy(migration_path,roots);
            legacy.create_session("migrated","v19 inbox");
            legacy.enqueue_session_input("migrated-queued","migrated","Pending before migration");
            legacy.enqueue_session_input("migrated-claimed","migrated","Claimed before migration");
            legacy.start_prompt_run("migrated-run","migrated",R"({"content":"Claimed before migration"})",{},{},"migrated-claimed");
        }
        {
            XlangSqlite legacy(migration_path,roots);
            remove_dynamic_plan_owner_scope_fixture(legacy);
            legacy.execute("DROP INDEX session_inbox_order");legacy.execute("ALTER TABLE session_inbox RENAME TO session_inbox_v20");
            legacy.execute("CREATE TABLE session_inbox(id TEXT PRIMARY KEY NOT NULL,session_id TEXT NOT NULL REFERENCES sessions(id),content TEXT NOT NULL CHECK(length(content)>0),state TEXT NOT NULL CHECK(state IN ('queued','claimed')),created_ms INTEGER NOT NULL CHECK(created_ms>0),run_id TEXT UNIQUE REFERENCES runs(id),CHECK((state='queued' AND run_id IS NULL) OR (state='claimed' AND run_id IS NOT NULL)))");
            legacy.execute("INSERT INTO session_inbox(id,session_id,content,state,created_ms,run_id) SELECT id,session_id,content,state,created_ms,run_id FROM session_inbox_v20");
            legacy.execute("DROP TABLE session_inbox_v20");legacy.execute("CREATE INDEX session_inbox_order ON session_inbox(session_id,state,created_ms,id)");legacy.execute("DROP TABLE session_models");legacy.execute("PRAGMA user_version=19");
        }
        {
            BackendLease owner(migration_path);Repository migrated(migration_path,roots);
            const auto before=migrated.session_inbox("migrated");const auto claimed=std::find_if(before.begin(),before.end(),[](const auto& item){return item.id=="migrated-claimed";});require(before.size()==2&&claimed!=before.end()&&claimed->state=="claimed"&&claimed->run_id=="migrated-run","v19 migration must preserve existing claimed input and its run link");
            migrated.cancel_session_input("migrated","migrated-queued");
            const auto after=migrated.session_inbox("migrated");require(after.size()==1&&after[0].id=="migrated-claimed","v19 migration must retain queued data and allow cancellation without exposing tombstones");
        }
#if defined(_WIN32)
        long_backend_lease(directory,roots);
#endif
        std::int64_t cursor=0;
        {
            BackendLease owner(path); Repository first(path,roots),second(path,roots);
            first.create_session("session","shared");
            first.create_session("inbox","durable inbox fixture");
            const auto queued=first.enqueue_session_input("queued-input-1","inbox","Persisted prompt survives refresh and backend reopen");
            require(queued.state=="queued"&&queued.run_id.empty(),"New session input must enter the durable queue");
            const auto removable=first.enqueue_session_input("queued-input-remove","inbox","This prompt can still be cancelled");
            second.cancel_session_input("inbox",removable.id);
            require(second.session_inbox("inbox").size()==1,"Cancellation must remove a still-queued item across repository connections");
            second.cancel_session_input("inbox","queued-input-remove");
            rejects<Conflict>([&]{second.enqueue_session_input("queued-input-remove","inbox","This prompt can still be cancelled");});
            const auto replay=second.enqueue_session_input("queued-input-1","inbox","Persisted prompt survives refresh and backend reopen");
            require(replay.id==queued.id&&second.session_inbox("inbox").size()==1,"Queue admission must be idempotent and visible across repository connections");
            const auto claimed=first.start_prompt_run("inbox-run","inbox",R"({"content":"Persisted prompt survives refresh and backend reopen"})",{}, {},queued.id);
            const auto inbox_after_claim=second.session_inbox("inbox");
            require(claimed.state==RunState::queued&&inbox_after_claim.size()==1&&inbox_after_claim[0].state=="claimed"&&inbox_after_claim[0].run_id==claimed.id,"Run admission must atomically claim its exact queued input");
            rejects<Conflict>([&]{second.start_prompt_run("inbox-replay-run","inbox",R"({"content":"Persisted prompt survives refresh and backend reopen"})",{}, {},queued.id);});
            const auto attached=first.enqueue_session_input("queued-input-files","inbox","Queue a file",R"([{"path":"README.md","folder_id":"workspace","name":"README"}])","workspace",4);
            const auto attached_replay=second.enqueue_session_input("queued-input-files","inbox","Queue a file",R"([{"path":"README.md","folder_id":"workspace","name":"README"}])","workspace",4);
            require(attached.files_json==attached_replay.files_json&&attached.workspace_id=="workspace"&&attached.workspace_revision==4,"Queued attachment references and their workspace revision must survive inbox replay");
            rejects<Conflict>([&]{second.enqueue_session_input("queued-input-files","inbox","Queue a file",R"([{"path":"src/changed.cpp","folder_id":"workspace","name":"changed.cpp"}])","workspace",4);});
            rejects<Conflict>([&]{first.start_prompt_run("queued-input-files-mismatch","inbox",R"({"content":"Queue a file"})",{}, {},attached.id);});
            second.cancel_session_input("inbox",queued.id);
            first.transition("inbox-run",RunState::queued,RunState::cancelled,R"({"reason":"contract_cleanup"})");
            first.put_information("agents","coding",R"({"model":"test"})");
            require(second.information("agents","coding")==R"({"model":"test"})","Information must be shared");
            rejects<DatabaseError>([&]{first.put_information("agents","coding","invalid JSON");});
            require(second.information("agents","coding")==R"({"model":"test"})","Invalid update must roll back");
            rejects<std::invalid_argument>([&]{first.put_information("secrets","key","{}");});
            first.create_run("run","session");
            rejects<Conflict>([&]{second.create_run("competitor","session");});
            first.transition("run",RunState::queued,RunState::running);
            cursor=second.events("run").back().sequence;
            rejects<DatabaseError>([&]{first.transition("run",RunState::running,RunState::completed,"invalid JSON");});
            require(second.run("run").state==RunState::running,"State must roll back with rejected event");
            require(second.events("run",cursor).empty(),"Failed transition must publish no event");
            rejects<Conflict>([&]{second.transition("run",RunState::queued,RunState::failed);});
            first.append_message("session","user",R"({"content":"hello"})");
            first.transition("run",RunState::running,RunState::completed);
            rejects<Conflict>([&]{second.append_event("run","model.text","{}");});
            first.create_run("interrupted","session");
        }
        {
            BackendLease owner(path); Repository reopened(path,roots);
            require(reopened.history("session").size()==1,"Conversation must survive reopen");
            require(reopened.events("run",cursor).size()==1,"Cursor replay must survive reopen");
            require(reopened.run("interrupted").state==RunState::queued,"Opening must not recover automatically");
            require(reopened.recover_interrupted(owner)==1,"Recovery must fail the interrupted root");
            require(reopened.recover_interrupted(owner)==0,"Recovery must be idempotent");
            XlangSqlite faults(path,roots);
            faults.execute("CREATE TRIGGER reject_event BEFORE INSERT ON events BEGIN SELECT RAISE(ABORT,'fault'); END");
            rejects<DatabaseError>([&]{reopened.create_run("rollback","session");});
            rejects<NotFound>([&]{reopened.run("rollback");});
            faults.execute("DROP TRIGGER reject_event");
            reopened.create_run("rollback","session");
            require(reopened.events("rollback").size()==1,"Fault must leave no orphan run or event");
            reopened.create_session("delete-parent","session deletion fixture");
            reopened.append_message("delete-parent","user",R"({"content":"Synthetic parent history"})");
            reopened.fork_session("delete-child","delete-parent");
            reopened.delete_session("delete-parent");
            rejects<NotFound>([&]{reopened.session("delete-parent");});
            rejects<NotFound>([&]{reopened.session("delete-child");});
            require(faults.execute("SELECT seq FROM messages WHERE session_id='delete-parent' AND role='user'").rows.size()==1&&faults.execute("SELECT id FROM deleted_sessions WHERE id IN ('delete-parent','delete-child') ORDER BY id").rows.size()==2,"Deleting a source session must hide its direct child while retaining immutable source history and tombstones");
            reopened.create_session("delete-tree","session deletion tree fixture");
            reopened.append_message("delete-tree","user",R"({"content":"Fork target"})");
            reopened.fork_session("delete-tree-child","delete-tree");
            reopened.create_run("delete-tree-child-run","delete-tree-child");
            rejects<Conflict>([&]{reopened.delete_session("delete-tree");});
            require(reopened.session("delete-tree").id=="delete-tree"&&reopened.session("delete-tree-child").id=="delete-tree-child","A live direct child must prevent a partial session-tree deletion");
            reopened.transition("delete-tree-child-run",RunState::queued,RunState::cancelled,R"({"reason":"contract_cleanup"})");
            reopened.delete_session("delete-tree");
            rejects<NotFound>([&]{reopened.session("delete-tree");});
            rejects<NotFound>([&]{reopened.session("delete-tree-child");});
            reopened.create_session("delete-active","active deletion fixture");
            reopened.create_run("delete-active-run","delete-active");
            rejects<Conflict>([&]{reopened.delete_session("delete-active");});
            reopened.transition("delete-active-run",RunState::queued,RunState::cancelled,R"({"reason":"contract_cleanup"})");
            reopened.delete_session("delete-active");
            reopened.create_session("delete-context","execution context cleanup fixture");
            reopened.create_run("delete-context-run","delete-context");
            reopened.transition("delete-context-run",RunState::queued,RunState::cancelled,R"({"reason":"contract_cleanup"})");
            faults.execute("INSERT INTO context_scopes(kind,id) VALUES('execution','delete-context-run')");
            faults.execute("INSERT INTO context_events(scope_kind,scope_id,owner_id,kind,payload) VALUES('execution','delete-context-run','delete-context-run','contract.fixture','{}')");
            reopened.delete_session("delete-context");
            require(faults.execute("SELECT id FROM context_scopes WHERE kind='execution' AND id='delete-context-run'").rows.size()==1&&faults.execute("SELECT seq FROM context_events WHERE scope_kind='execution' AND scope_id='delete-context-run'").rows.size()==1,"Session removal retains immutable execution context evidence while hiding the session");
            const auto remaining_sessions=reopened.sessions();
            require(std::none_of(remaining_sessions.begin(),remaining_sessions.end(),[](const auto& value){return value.id=="delete-context";}),"Deleted sessions disappear from the shared catalog");
            require(faults.execute("PRAGMA foreign_key_check").rows.empty(),"Session removal must leave no dangling repository foreign keys");
        }
        std::cout<<"xlang3-backed repository contracts passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
