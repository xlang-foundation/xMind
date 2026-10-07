#include "agentflow/store.hpp"
#include "agentflow/backend_lease.hpp"
#include <sqlite3.h>
#include <limits>
#include <mutex>

namespace agentflow {
namespace {
void check(sqlite3* db, int code) {
    if (code != SQLITE_OK && code != SQLITE_DONE && code != SQLITE_ROW)
        throw DatabaseError(sqlite3_errmsg(db));
}
void execute(sqlite3* db, const char* sql) { check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr)); }
void identifier(const std::string& id) {
    if (id.empty() || id.find('\0') != std::string::npos) throw std::invalid_argument("Empty or invalid identifier");
}
class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) { check(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr)); }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void bind(int index, const std::string& text) {
        if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::length_error("Text too large");
        check(db_, sqlite3_bind_text(stmt_, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT));
    }
    void bind(int index, std::int64_t number) { check(db_, sqlite3_bind_int64(stmt_, index, number)); }
    bool row() { const auto code = sqlite3_step(stmt_); check(db_, code); return code == SQLITE_ROW; }
    void done() { if (row()) throw DatabaseError("Unexpected result row"); }
    std::string text(int index) const {
        const auto* value = sqlite3_column_text(stmt_, index);
        const auto size = sqlite3_column_bytes(stmt_, index);
        return value ? std::string(reinterpret_cast<const char*>(value), static_cast<std::size_t>(size)) : std::string{};
    }
    std::int64_t integer(int index) const { return sqlite3_column_int64(stmt_, index); }
private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};
class Transaction {
public:
    explicit Transaction(sqlite3* db) : db_(db) { execute(db_, "BEGIN IMMEDIATE"); }
    ~Transaction() { if (!committed_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { execute(db_, "COMMIT"); committed_ = true; }
private:
    sqlite3* db_;
    bool committed_ = false;
};
RunState parse_state(const std::string& value) {
    for (auto s : {RunState::queued, RunState::running, RunState::paused, RunState::completed, RunState::failed, RunState::cancelled})
        if (to_string(s) == value) return s;
    throw DatabaseError("Unknown stored run state");
}
bool permitted(RunState from, RunState to) {
    if (from == RunState::queued) return to == RunState::running || to == RunState::cancelled || to == RunState::failed;
    if (from == RunState::running) return to == RunState::completed || to == RunState::failed || to == RunState::cancelled || to == RunState::paused;
    if (from == RunState::paused) return to == RunState::running || to == RunState::cancelled || to == RunState::failed;
    return false;
}
Session get_session(sqlite3* db, const std::string& id) {
    Statement query(db, "SELECT id,title FROM sessions WHERE id=?"); query.bind(1, id);
    if (!query.row()) throw NotFound("Session not found");
    return {query.text(0), query.text(1)};
}
Run get_run(sqlite3* db, const std::string& id) {
    Statement query(db, "SELECT id,session_id,state FROM runs WHERE id=?"); query.bind(1, id);
    if (!query.row()) throw NotFound("Run not found");
    return {query.text(0), query.text(1), parse_state(query.text(2))};
}
Event insert_event(sqlite3* db, const std::string& id, const std::string& kind, const std::string& json) {
    Statement insert(db, "INSERT INTO events(run_id,kind,payload) VALUES(?,?,?)");
    insert.bind(1, id); insert.bind(2, kind); insert.bind(3, json); insert.done();
    return {sqlite3_last_insert_rowid(db), id, kind, json};
}
}
struct Store::Impl {
    sqlite3* db = nullptr;
    std::string path;
    std::mutex mutex;
    ~Impl() { if (db) sqlite3_close_v2(db); }
};
Store::Store(const std::string& path) : impl_(std::make_unique<Impl>()) {
    identifier(path);
    const auto opened = sqlite3_open_v2(path.c_str(), &impl_->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    check(impl_->db, opened);
    auto* db = impl_->db;
    const auto* filename=sqlite3_db_filename(db,"main");
    impl_->path=filename ? filename : "";
    check(db, sqlite3_busy_timeout(db, 5000));
    execute(db, "PRAGMA foreign_keys=ON");
    Transaction tx(db);
    std::int64_t version;
    { Statement q(db, "PRAGMA user_version"); q.row(); version = q.integer(0); }
    std::int64_t application;
    { Statement q(db, "PRAGMA application_id"); q.row(); application = q.integer(0); }
    if ((version == 0 && application != 0) || (version != 0 && application != 0x41464c57))
        throw DatabaseError("Database belongs to another application");
    if (version == 0) {
        { Statement q(db, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'");
          q.row();
          if (q.integer(0) != 0) throw DatabaseError("Unversioned existing database requires explicit migration"); }
        execute(db, R"sql(
          CREATE TABLE sessions(id TEXT PRIMARY KEY NOT NULL, title TEXT NOT NULL);
          CREATE TABLE runs(id TEXT PRIMARY KEY NOT NULL, session_id TEXT NOT NULL REFERENCES sessions(id),
            state TEXT NOT NULL CHECK(state IN ('queued','running','paused','completed','failed','cancelled')));
          CREATE UNIQUE INDEX one_active_run ON runs(session_id) WHERE state IN ('queued','running','paused');
          CREATE TABLE events(seq INTEGER PRIMARY KEY AUTOINCREMENT, run_id TEXT NOT NULL REFERENCES runs(id),
            kind TEXT NOT NULL, payload TEXT NOT NULL CHECK(json_valid(payload)));
          CREATE INDEX run_events ON events(run_id,seq);
          CREATE TABLE messages(seq INTEGER PRIMARY KEY AUTOINCREMENT, session_id TEXT NOT NULL REFERENCES sessions(id),
            role TEXT NOT NULL, payload TEXT NOT NULL CHECK(json_valid(payload)));
          CREATE INDEX session_messages ON messages(session_id,seq);
          PRAGMA user_version=1;
          PRAGMA application_id=0x41464c57;
        )sql");
    } else if (version != 1) throw DatabaseError("Unsupported native database version");
    tx.commit();
    execute(db, "PRAGMA journal_mode=WAL");
    execute(db, "PRAGMA synchronous=FULL");
}
Store::~Store() = default;
Session Store::create_session(const std::string& id, const std::string& title) {
    identifier(id); std::lock_guard lock(impl_->mutex);
    Statement q(impl_->db, "INSERT INTO sessions(id,title) VALUES(?,?)"); q.bind(1,id); q.bind(2,title); q.done();
    return {id,title};
}
Session Store::session(const std::string& id) { std::lock_guard lock(impl_->mutex); return get_session(impl_->db,id); }
std::vector<Session> Store::sessions() {
    std::lock_guard lock(impl_->mutex); std::vector<Session> result;
    Statement q(impl_->db, "SELECT id,title FROM sessions ORDER BY rowid");
    while (q.row()) result.push_back({q.text(0),q.text(1)});
    return result;
}
Run Store::create_run(const std::string& id, const std::string& session_id) {
    identifier(id); std::lock_guard lock(impl_->mutex); auto* db = impl_->db; Transaction tx(db);
    get_session(db,session_id);
    { Statement q(db, "SELECT id FROM runs WHERE session_id=? AND state IN ('queued','running','paused')");
      q.bind(1,session_id); if (q.row()) throw Conflict("Session already has an active run"); }
    Statement q(db, "INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')");
    q.bind(1,id); q.bind(2,session_id); q.done();
    insert_event(db,id,"run.queued","{}"); tx.commit(); return {id,session_id,RunState::queued};
}
Run Store::run(const std::string& id) { std::lock_guard lock(impl_->mutex); return get_run(impl_->db,id); }
Run Store::transition(const std::string& id, RunState expected, RunState next, const std::string& json) {
    if (!permitted(expected,next)) throw std::invalid_argument("Invalid run transition");
    std::lock_guard lock(impl_->mutex); auto* db = impl_->db; Transaction tx(db);
    auto result = get_run(db,id);
    Statement q(db, "UPDATE runs SET state=? WHERE id=? AND state=?");
    q.bind(1,to_string(next)); q.bind(2,id); q.bind(3,to_string(expected)); q.done();
    if (sqlite3_changes(db) != 1) throw Conflict("Run state changed");
    insert_event(db,id,"run." + to_string(next),json); tx.commit(); result.state=next; return result;
}
Event Store::append_event(const std::string& id, const std::string& kind, const std::string& json) {
    identifier(kind);
    if (kind.rfind("run.",0)==0) throw std::invalid_argument("Run lifecycle events require a state transition");
    std::lock_guard lock(impl_->mutex); Transaction tx(impl_->db);
    const auto current=get_run(impl_->db,id);
    if (current.state==RunState::completed || current.state==RunState::failed || current.state==RunState::cancelled)
        throw Conflict("Cannot publish execution events after a terminal run");
    auto result=insert_event(impl_->db,id,kind,json); tx.commit(); return result;
}
std::vector<Event> Store::events(const std::string& id, std::int64_t after) {
    if (after < 0) throw std::invalid_argument("Negative event cursor");
    std::lock_guard lock(impl_->mutex); get_run(impl_->db,id); std::vector<Event> result;
    Statement q(impl_->db,"SELECT seq,kind,payload FROM events WHERE run_id=? AND seq>? ORDER BY seq");
    q.bind(1,id); q.bind(2,after);
    while (q.row()) result.push_back({q.integer(0),id,q.text(1),q.text(2)});
    return result;
}
void Store::append_message(const std::string& id, const std::string& role, const std::string& json) {
    identifier(role); std::lock_guard lock(impl_->mutex); Transaction tx(impl_->db); get_session(impl_->db,id);
    Statement q(impl_->db,"INSERT INTO messages(session_id,role,payload) VALUES(?,?,?)");
    q.bind(1,id); q.bind(2,role); q.bind(3,json); q.done(); tx.commit();
}
std::vector<Message> Store::history(const std::string& id) {
    std::lock_guard lock(impl_->mutex); get_session(impl_->db,id); std::vector<Message> result;
    Statement q(impl_->db,"SELECT seq,role,payload FROM messages WHERE session_id=? ORDER BY seq"); q.bind(1,id);
    while(q.row()) result.push_back({q.integer(0),q.text(1),q.text(2)});
    return result;
}
std::size_t Store::recover_interrupted(const BackendLease& owner) {
    if(!owner.covers(impl_->path)) throw Conflict("Recovery requires this database's backend lease");
    std::lock_guard lock(impl_->mutex); auto* db=impl_->db; Transaction tx(db); std::vector<std::string> ids;
    { Statement q(db,"SELECT id FROM runs WHERE state IN ('queued','running')"); while(q.row()) ids.push_back(q.text(0)); }
    execute(db,"UPDATE runs SET state='failed' WHERE state IN ('queued','running')");
    for (const auto& id : ids) insert_event(db,id,"run.failed",R"({"reason":"server_restart"})");
    tx.commit(); return ids.size();
}
}
