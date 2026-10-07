#include "agentflow/persistence_service.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>
#include <thread>

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
            auto candidate=std::filesystem::temp_directory_path()/("xmind-service-"+std::to_string(random())+"-"+std::to_string(random()));
            if(std::filesystem::create_directory(candidate)) {path=std::move(candidate);return;}
        }
        throw std::runtime_error("Cannot create test directory");
    }
    ~Directory() {std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        Directory directory;const auto path=(directory.path/"state.sqlite").string();
        const std::vector<std::string> roots{argv[1],argv[2]};
        {
            Repository before(path,roots);
            before.create_session("interrupted","Before restart");before.create_run("interrupted","interrupted");
            before.create_session("paused","Awaiting approval");before.create_run("paused","paused");
            before.transition("paused",RunState::queued,RunState::running);
            before.transition("paused",RunState::running,RunState::paused);
        }
        {
            PersistenceService service(path,roots);
            require(service.run("interrupted").get().state==RunState::failed,"Owner startup must reconcile interrupted run");
            require(service.events("interrupted").get().size()==2,"Recovery must add one failure event");
            require(service.run("paused").get().state==RunState::paused,"Paused run must survive recovery");
            rejects<Conflict>([&]{PersistenceService competitor(path,roots);});
            std::string id="shared",title="Owned request";
            auto created=service.create_session(id,title);id="changed";title="changed";
            require(created.get().id=="shared","Requests must own their arguments");
            rejects<NotFound>([&]{service.session("missing").get();});
            require(service.session("shared").get().title=="Owned request","Failed request must not stop worker");
            std::vector<std::future<void>> producers;
            for(int i=0;i<4;++i) producers.push_back(std::async(std::launch::async,[&service] {
                for(int j=0;j<25;++j) service.append_message("shared","user",R"({"text":"concurrent"})").get();
            }));
            for(auto& producer:producers) producer.get();
            require(service.history("shared").get().size()==100,"Concurrent clients must persist all requests");
            service.create_run("race","shared").get();
            auto a=service.transition("race",RunState::queued,RunState::running);
            auto b=service.transition("race",RunState::queued,RunState::running);
            require(a.get().state==RunState::running,"First transition must succeed");
            rejects<Conflict>([&]{b.get();});
            require(service.events("race").get().size()==2,"Rejected transition must not publish an event");
#if defined(_WIN32)
            const std::vector<std::uint8_t> bytes{12,0,255,33};
            service.put_credential("scope","key","model","Synthetic",SecretBytes(bytes),0).get();
            auto resolved=service.resolve_credential("scope","key","model").get();
            require(std::ranges::equal(resolved.view(),bytes),"Move-only secrets must cross request/result boundary");
#endif
            // close must drain every accepted operation before releasing ownership.
            std::vector<std::future<void>> accepted;
            for(int i=0;i<25;++i) accepted.push_back(service.append_message("shared","assistant","{}"));
            auto close_a=std::async(std::launch::async,[&]{service.close();});
            auto close_b=std::async(std::launch::async,[&]{service.close();});
            close_a.get();close_b.get();
            for(auto& future:accepted) future.get();
            rejects<PersistenceClosed>([&]{service.sessions();});
        }
        {
            PersistenceService reopened(path,roots);
            require(reopened.history("shared").get().size()==125,"Drained requests must survive restart");
            require(reopened.run("race").get().state==RunState::failed,"Next owner must recover running root");
            require(reopened.events("interrupted").get().size()==2,"Startup recovery must be idempotent");
        }
        {
            // Hold a real write transaction to make backpressure observable without
            // exposing arbitrary callbacks or runtime objects through the service.
            PersistenceService bounded(path,roots,1);XlangSqlite blocker(path,roots);blocker.begin();
            std::vector<std::future<Session>> accepted;
            bool busy=false;
            for(int i=0;i<10;++i) {
                try {accepted.push_back(bounded.create_session("bounded-"+std::to_string(i),"Bounded"));}
                catch(const PersistenceBusy&) {busy=true;break;}
            }
            blocker.rollback();require(busy,"Full queue must reject before unbounded growth");
            for(auto& future:accepted) future.get();
        }
        rejects<std::invalid_argument>([&]{PersistenceService invalid(path,roots,0);});
        // Missing module roots produce the embedding SDK's runtime_error at import.
        rejects<std::runtime_error>([&]{PersistenceService invalid(path,{});});
        // Initialization failure must join its thread and release its lease.
        {PersistenceService after_failure(path,roots);require(!after_failure.sessions().get().empty(),"Lease must release after initialization failure");}
        std::cout<<"Persistence service ownership/concurrency contracts passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
