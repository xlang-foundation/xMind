#include "agentflow/repository.hpp"
#include "agentflow/backend_lease.hpp"
#include <filesystem>
#include <iostream>

using namespace agentflow;
int main(int argc,char** argv) {
    if(argc!=5 && argc!=6) {
        std::cerr<<"Usage: xmind_persistence_demo seed|read DATABASE PACKAGES STDLIB [EVENT_CURSOR]\n"; return 2;
    }
    try {
        const std::string mode=argv[1],path=argv[2];
        if(mode!="seed" && mode!="read") throw std::invalid_argument("Mode must be seed or read");
        if(mode=="read" && !std::filesystem::exists(std::filesystem::u8path(path))) throw NotFound("Demo database not found");
        const std::vector<std::string> roots{argv[3],argv[4]};
        std::unique_ptr<BackendLease> owner;
        if(mode=="seed") owner=std::make_unique<BackendLease>(path);
        Repository repository(path,roots);
        if(mode=="seed") {
            if(!repository.sessions().empty()) throw Conflict("Use a new demo database; existing sessions are preserved");
            repository.create_session("milestone-session","Persistence milestone");
            repository.put_information("agents","coding",R"({"name":"Coding agent","runtime":"xlang3","core":"C++"})");
            repository.create_run("milestone-run","milestone-session");
            repository.transition("milestone-run",RunState::queued,RunState::running);
            repository.append_message("milestone-session","user",R"({"content":"Show persistent sessions"})");
            repository.append_message("milestone-session","assistant",R"({"content":"Session saved by C++ through xlang3 SQLite"})");
            repository.append_event("milestone-run","demo.saved",R"({"messages":2})");
            repository.transition("milestone-run",RunState::running,RunState::completed,R"({"demo":true})");
        }
        std::int64_t cursor=0;
        if(argc==6) {
            std::size_t consumed=0;cursor=std::stoll(argv[5],&consumed);
            if(consumed!=std::string(argv[5]).size() || cursor<0) throw std::invalid_argument("Invalid cursor");
        }
        const auto session=repository.session("milestone-session");
        std::cout<<"Session: "<<session.id<<" | "<<session.title<<'\n';
        std::cout<<"Run: "<<to_string(repository.run("milestone-run").state)<<'\n';
        std::cout<<"Agent: "<<repository.information("agents","coding")<<'\n';
        for(const auto& message:repository.history(session.id)) std::cout<<message.role<<": "<<message.json<<'\n';
        std::cout<<"Events after "<<cursor<<":\n";
        for(const auto& event:repository.events("milestone-run",cursor)) std::cout<<event.sequence<<" "<<event.kind<<" "<<event.json<<'\n';
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
