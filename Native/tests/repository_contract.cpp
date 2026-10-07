#include "agentflow/repository.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/xlang_sqlite.hpp"
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
}
int main(int argc,char** argv) {
    if(argc!=3) {std::cerr<<"Expected package and stdlib-source roots\n";return 2;}
    try {
        Directory directory; const auto path=(directory.path/"state.sqlite").string();
        const std::vector<std::string> roots{argv[1],argv[2]};
        std::int64_t cursor=0;
        {
            BackendLease owner(path); Repository first(path,roots),second(path,roots);
            first.create_session("session","shared");
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
        }
        std::cout<<"xlang3-backed repository contracts passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
