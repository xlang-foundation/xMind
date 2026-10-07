#pragma once
#include "agentflow/store.hpp"
#include <memory>
#include <vector>

namespace agentflow {
// Target repository: C++ contracts, all database operations through xlang3.
// Construct/use/destroy on the persistence thread. No direct SQLite linkage.
class Repository {
public:
    Repository(const std::string& database,const std::vector<std::string>& import_roots);
    ~Repository();
    Repository(const Repository&)=delete;
    Repository& operator=(const Repository&)=delete;
    Session create_session(const std::string& id,const std::string& title);
    Session session(const std::string& id);
    std::vector<Session> sessions();
    Run create_run(const std::string& id,const std::string& session_id);
    Run run(const std::string& id);
    Run transition(const std::string& id,RunState expected,RunState next,const std::string& json="{}");
    Event append_event(const std::string& id,const std::string& kind,const std::string& json);
    std::vector<Event> events(const std::string& id,std::int64_t after=0);
    void append_message(const std::string& id,const std::string& role,const std::string& json);
    std::vector<Message> history(const std::string& id);
    void put_information(const std::string& category,const std::string& id,const std::string& json);
    std::string information(const std::string& category,const std::string& id);
    std::size_t recover_interrupted(const BackendLease& owner);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
