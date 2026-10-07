#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace agentflow {
class BackendLease;
enum class RunState { queued, running, paused, completed, failed, cancelled };
inline std::string to_string(RunState state) {
    switch(state) {
    case RunState::queued:return "queued"; case RunState::running:return "running";
    case RunState::paused:return "paused"; case RunState::completed:return "completed";
    case RunState::failed:return "failed"; case RunState::cancelled:return "cancelled";
    }
    throw std::invalid_argument("Invalid run state");
}
struct NotFound : std::runtime_error { using std::runtime_error::runtime_error; };
struct Conflict : std::runtime_error { using std::runtime_error::runtime_error; };
struct DatabaseError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Session { std::string id, title; };
struct Run { std::string id, session_id; RunState state; };
struct Event { std::int64_t sequence; std::string run_id, kind, json; };
struct Message { std::int64_t sequence; std::string role, json; };

// IDs are supplied by the caller. JSON is validated by SQLite, never interpolated.
// Thread-safe per instance; transactions arbitrate writes across connections.
class Store {
public:
    explicit Store(const std::string& database_path);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Session create_session(const std::string& id, const std::string& title);
    Session session(const std::string& id);
    std::vector<Session> sessions();
    Run create_run(const std::string& id, const std::string& session_id);
    Run run(const std::string& id);
    Run transition(const std::string& id, RunState expected, RunState next,
                   const std::string& json = "{}");
    // Execution events only: terminal runs and reserved run.* events are rejected.
    Event append_event(const std::string& run_id, const std::string& kind,
                       const std::string& json);
    std::vector<Event> events(const std::string& run_id, std::int64_t after = 0);
    void append_message(const std::string& session_id, const std::string& role,
                        const std::string& json);
    std::vector<Message> history(const std::string& session_id);
    // Call only after the service has established exclusive backend ownership.
    // Paused human-input runs survive restart. Effect reconciliation is separate.
    std::size_t recover_interrupted(const BackendLease& owner);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
