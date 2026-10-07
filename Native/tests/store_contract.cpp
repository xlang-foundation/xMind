#include "agentflow/store.hpp"
#include "agentflow/backend_lease.hpp"
#include <sqlite3.h>
#include <array>
#include <barrier>
#include <exception>
#include <filesystem>
#include <iostream>
#include <random>
#include <thread>
#include <system_error>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace agentflow;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Error, class Action> void rejects(Action action) {
    try { action(); } catch (const Error&) { return; }
    throw std::runtime_error("Expected rejection did not occur");
}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        std::random_device random;
        for (int attempt=0; attempt<32; ++attempt) {
            auto candidate = std::filesystem::temp_directory_path() /
                ("agentflow-native-contract-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) { path=std::move(candidate); return; }
        }
        throw std::runtime_error("Cannot create unique contract test directory");
    }
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
void persistence(const std::string& path) {
    std::int64_t cursor;
    {
        Store a(path), b(path);
        const std::string title("title\0with-nul",14);
        a.create_session("session",title);
        require(b.session("session").title==title,"Binary-safe title must persist across connections");
        a.create_run("run","session");
        require(b.events("run").size()==1,"Queued event must be published atomically");
        rejects<Conflict>([&]{b.create_run("competitor","session");});
        rejects<NotFound>([&]{a.create_run("missing-run","absent");});
        rejects<NotFound>([&]{a.run("competitor");});
        a.transition("run",RunState::queued,RunState::running);
        cursor=b.events("run").back().sequence;
        rejects<DatabaseError>([&]{a.transition("run",RunState::running,RunState::completed,"invalid JSON");});
        require(b.run("run").state==RunState::running,"Failed event insertion must roll back state change");
        require(b.events("run",cursor).empty(),"Failed transaction must publish no events");
        rejects<Conflict>([&]{b.transition("run",RunState::queued,RunState::failed);});
        require(b.events("run",cursor).empty(),"Stale state transition must publish no event");
        rejects<std::invalid_argument>([&]{a.transition("run",RunState::running,RunState::queued);});
        rejects<std::invalid_argument>([&]{a.append_event("run","run.completed","{}");});
        a.append_message("session","user",R"({"content":"persist me"})");
        a.append_message("session","assistant",R"({"content":"reply"})");
        rejects<DatabaseError>([&]{a.append_message("session","user","invalid");});
        require(b.history("session").size()==2,"Invalid message must not persist");
        const auto e=a.append_event("run","model.text",R"({"text":"hello"})");
        require(e.sequence>cursor,"Committed event sequence must increase");
        a.transition("run",RunState::running,RunState::completed,R"({"text":"hello"})");
        const auto replay=b.events("run",cursor);
        require(replay.size()==2 && replay[0].kind=="model.text" && replay[1].kind=="run.completed","Cursor must replay committed events in order");
        rejects<Conflict>([&]{b.append_event("run","model.text",R"({"text":"late"})");});
        require(a.events("run",cursor).size()==2,"Late execution events must not persist");
        rejects<std::invalid_argument>([&]{a.transition("run",RunState::completed,RunState::running);});
        rejects<std::invalid_argument>([&]{a.events("run",-1);});
        a.create_run("next","session");
        a.transition("next",RunState::queued,RunState::cancelled);
    }
    Store reopened(path);
    require(reopened.history("session").size()==2,"Messages must survive reopen");
    require(reopened.run("run").state==RunState::completed,"Terminal run must survive reopen");
    require(reopened.events("run",cursor).size()==2,"Cursor replay must survive reopen");
}
void races(const std::string& path) {
    Store a(path), b(path);
    a.create_session("race","race");
    std::barrier start(2);
    std::array<int,2> outcomes{};
    std::array<std::exception_ptr,2> errors{};
    auto compete=[&](Store& store, int index) {
        start.arrive_and_wait();
        try { store.create_run("race-"+std::to_string(index),"race"); outcomes[index]=1; }
        catch(const Conflict&) { outcomes[index]=-1; }
        catch(...) { errors[index]=std::current_exception(); }
    };
    std::thread first([&]{compete(a,0);}), second([&]{compete(b,1);});
    first.join(); second.join();
    for(auto error:errors) if(error) std::rethrow_exception(error);
    require(outcomes[0]+outcomes[1]==0 && outcomes[0]!=0,"Exactly one competing root run must succeed");
    const auto winner=outcomes[0]==1 ? "race-0" : "race-1";
    require(a.events(winner).size()==1,"Winning run must have exactly one queued event");
    a.transition(winner,RunState::queued,RunState::running);
    std::barrier transitions(2);
    outcomes={}; errors={};
    auto finish=[&](Store& store,int index) {
        transitions.arrive_and_wait();
        try { store.transition(winner,RunState::running,index==0?RunState::completed:RunState::cancelled); outcomes[index]=1; }
        catch(const Conflict&) { outcomes[index]=-1; }
        catch(...) { errors[index]=std::current_exception(); }
    };
    std::thread third([&]{finish(a,0);}), fourth([&]{finish(b,1);});
    third.join(); fourth.join();
    for(auto error:errors) if(error) std::rethrow_exception(error);
    require(outcomes[0]+outcomes[1]==0 && outcomes[0]!=0,"Exactly one competing terminal transition must succeed");
    require(a.events(winner).size()==3,"Losing transition must not add an event");
}
void recovery(const std::string& path) {
    {
        Store a(path);
        for(const auto* id:{"queued","running","paused"}) { a.create_session(id,id); a.create_run(id,id); }
        a.transition("running",RunState::queued,RunState::running);
        a.transition("paused",RunState::queued,RunState::running);
        a.transition("paused",RunState::running,RunState::paused);
    }
    Store reopened(path);
    require(reopened.run("running").state==RunState::running,"Opening a reader must not trigger recovery");
    BackendLease owner(path);
    BackendLease wrong(path+".other");
    rejects<Conflict>([&]{reopened.recover_interrupted(wrong);});
    require(reopened.run("running").state==RunState::running,"Wrong owner must not change state");
    require(reopened.recover_interrupted(owner)==2,"Explicit recovery must fail queued/running roots");
    require(reopened.recover_interrupted(owner)==0,"Recovery must be idempotent");
    require(reopened.run("paused").state==RunState::paused,"Human pause must survive restart");
    rejects<Conflict>([&]{reopened.create_run("conflict","paused");});
    require(reopened.events("running").back().json==R"({"reason":"server_restart"})","Recovery reason must be durable");
    reopened.transition("paused",RunState::paused,RunState::running);
    reopened.transition("paused",RunState::running,RunState::completed);
}
void foreign_database(const std::string& path) {
    sqlite3* db=nullptr;
    require(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"Fixture open failed");
    const auto code=sqlite3_exec(db,"CREATE TABLE prototype(id TEXT)",nullptr,nullptr,nullptr);
    sqlite3_close(db);
    require(code==SQLITE_OK,"Fixture create failed");
    rejects<DatabaseError>([&]{Store unexpected(path);});
}
void wrong_application(const std::string& path) {
    sqlite3* db=nullptr;
    require(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"Fixture open failed");
    const auto code=sqlite3_exec(db,"PRAGMA user_version=1; PRAGMA application_id=123",nullptr,nullptr,nullptr);
    sqlite3_close(db);
    require(code==SQLITE_OK,"Fixture create failed");
    rejects<DatabaseError>([&]{Store unexpected(path);});
}
int lease_probe(const std::filesystem::path& executable, const std::string& path) {
#if defined(_WIN32)
    const auto argument=std::filesystem::u8path(path).wstring();
    auto command=L"\""+executable.wstring()+L"\" --lease-probe \""+argument+L"\"";
    STARTUPINFOW startup{}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))
        throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Lease child start failed");
    CloseHandle(process.hThread);
    const auto wait=WaitForSingleObject(process.hProcess,30000);
    if(wait!=WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess,1);
        CloseHandle(process.hProcess);
        throw std::runtime_error("Lease child did not finish");
    }
    DWORD result=1;
    const auto read=GetExitCodeProcess(process.hProcess,&result);
    CloseHandle(process.hProcess);
    require(read!=FALSE,"Lease child status unavailable");
    return static_cast<int>(result);
#else
    const auto child=fork();
    if(child<0) throw std::system_error(errno,std::generic_category(),"Lease child start failed");
    if(child==0) {
        execl(executable.c_str(),executable.c_str(),"--lease-probe",path.c_str(),static_cast<char*>(nullptr));
        _exit(1);
    }
    int status;
    while(waitpid(child,&status,0)<0) {
        if(errno!=EINTR) throw std::system_error(errno,std::generic_category(),"Lease child wait failed");
    }
    require(WIFEXITED(status),"Lease child terminated abnormally");
    return WEXITSTATUS(status);
#endif
}
void leases(const std::string& path, const std::filesystem::path& executable) {
    {
        BackendLease first(path);
        rejects<Conflict>([&]{BackendLease second(path);});
        auto alias=std::filesystem::path(path).parent_path()/"."/std::filesystem::path(path).filename();
        rejects<Conflict>([&]{BackendLease second(alias.string());});
        require(lease_probe(executable,path)==17,"Separate process must reject a held backend lease");
    }
    // An existing sidecar is not evidence of a live owner.
    require(std::filesystem::exists(path+".backend-lock"),"Sidecar should persist after release");
    require(lease_probe(executable,path)==0,"Separate process must acquire a released backend lease");
    BackendLease next(path);
    rejects<std::invalid_argument>([]{BackendLease memory(":memory:");});
}
void creation_rollback(const std::string& path) {
    Store store(path); store.create_session("session","fault injection");
    auto sql=[&](const char* statement) {
        sqlite3* db=nullptr;
        require(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"Fixture open failed");
        const auto result=sqlite3_exec(db,statement,nullptr,nullptr,nullptr);
        sqlite3_close(db);
        require(result==SQLITE_OK,"Fixture SQL failed");
    };
    sql("CREATE TRIGGER reject_event BEFORE INSERT ON events BEGIN SELECT RAISE(ABORT,'test fault'); END");
    rejects<DatabaseError>([&]{store.create_run("rollback","session");});
    rejects<NotFound>([&]{store.run("rollback");});
    sql("DROP TRIGGER reject_event");
    store.create_run("rollback","session");
    require(store.events("rollback").size()==1,"Failed creation must leave no orphan run or event");
}
}
int main(int argc, char** argv) {
    if(argc==3 && std::string(argv[1])=="--lease-probe") {
        try { BackendLease lease(argv[2]); return 0; }
        catch(const Conflict&) { return 17; }
        catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    }
    try {
        TemporaryDirectory directory;
        persistence((directory.path/"persistence.sqlite").string());
        races((directory.path/"races.sqlite").string());
        recovery((directory.path/"recovery.sqlite").string());
        foreign_database((directory.path/"prototype.sqlite").string());
        wrong_application((directory.path/"foreign.sqlite").string());
        leases((directory.path/"lease.sqlite").string(),std::filesystem::absolute(argv[0]));
        creation_rollback((directory.path/"rollback.sqlite").string());
        std::cout<<"Native store contracts passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Native store contract failed: "<<error.what()<<'\n';
        return 1;
    }
}
