#include "agentflow/repository.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/backend_lease.hpp"
#include <limits>
#include <chrono>
#include <set>
#include "nlohmann/json.hpp"

namespace agentflow {
namespace {
using Json=nlohmann::json;
std::int64_t now_ms() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string object_json(const std::string& source) {
    if(source.size()>2*1024*1024) throw std::invalid_argument("Operation JSON exceeds its limit");
    try {
        std::vector<std::set<std::string>> objects;
        auto value=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& parsed) {
            if(depth>64) throw std::invalid_argument("Operation JSON nesting exceeds its limit");
            if(event==Json::parse_event_t::object_start) objects.emplace_back();
            else if(event==Json::parse_event_t::object_end) objects.pop_back();
            else if(event==Json::parse_event_t::key && !objects.back().insert(parsed.get<std::string>()).second)
                throw std::invalid_argument("Duplicate operation JSON field");
            return true;
        });
        if(!value.is_object()) throw std::invalid_argument("Operation JSON must be an object");
        // Preserve the exact approved bytes. In particular, parsing and dumping
        // JSON numbers can lose precision before a remote effect adapter sees
        // them; a semantic comparison must not authorize changed payload bytes.
        return source;
    } catch(const Json::exception&) {throw std::invalid_argument("Invalid operation JSON");}
}
const std::string& text(const SqlValue& value) { return std::get<std::string>(value); }
std::int64_t integer(const SqlValue& value) { return std::get<std::int64_t>(value); }
void identifier(const std::string& value) {
    if(value.empty() || value.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid identifier");
}
std::string credential_context(const std::string& scope,const std::string& id,
    const std::string& purpose,std::int64_t revision) {
    // Length prefixes prevent identity collisions when identifiers contain delimiters.
    std::string context="xMind.credential.v1:";
    for(const auto* part:{&scope,&id,&purpose}) {
        identifier(*part);
        if(part->size()>4096) throw std::invalid_argument("Credential identifier too long");
        context+=std::to_string(part->size())+":"+*part;
    }
    return context+":"+std::to_string(revision);
}
std::string state_name(RunState state) {
    switch(state) {
    case RunState::queued: return "queued"; case RunState::running: return "running";
    case RunState::paused: return "paused"; case RunState::completed: return "completed";
    case RunState::failed: return "failed"; case RunState::cancelled: return "cancelled";
    }
    throw std::invalid_argument("Invalid run state");
}
RunState state_value(const std::string& name) {
    for(auto state:{RunState::queued,RunState::running,RunState::paused,RunState::completed,RunState::failed,RunState::cancelled})
        if(state_name(state)==name) return state;
    throw DatabaseError("Invalid stored run state");
}
bool terminal(RunState state) { return state==RunState::completed || state==RunState::failed || state==RunState::cancelled; }
bool allowed(RunState from,RunState to) {
    if(from==RunState::queued) return to==RunState::running || to==RunState::cancelled || to==RunState::failed;
    if(from==RunState::running) return terminal(to) || to==RunState::paused;
    if(from==RunState::paused) return to==RunState::running || to==RunState::failed || to==RunState::cancelled;
    return false;
}
class Transaction {
public:
    explicit Transaction(XlangSqlite& database):database_(database) {database_.begin();}
    ~Transaction() {if(!committed_) {try {database_.rollback();} catch(...) { /* Adapter poisons itself on failed rollback. */ }}}
    void commit() {database_.commit(); committed_=true;}
private:
    XlangSqlite& database_;
    bool committed_=false;
};
void changed_one(const SqlResult& result) {
    if(result.affected_rows!=1) throw DatabaseError("Expected one affected row; got "+std::to_string(result.affected_rows));
}
OperationState operation_state(const std::string& name) {
    for(auto value:{OperationState::awaiting_approval,OperationState::ready,OperationState::denied,OperationState::expired,OperationState::cancelled,OperationState::executing,OperationState::succeeded,OperationState::failed,OperationState::uncertain})
        if(to_string(value)==name) return value;
    throw DatabaseError("Invalid stored operation state");
}
Operation decode_operation(const std::vector<SqlValue>& row) {
    return {text(row[0]),{text(row[1]),text(row[2]),text(row[3]),text(row[4])},operation_state(text(row[5])),integer(row[6]),text(row[7]),text(row[8])};
}
}
struct Repository::Impl {
    std::string path;
    XlangSqlite database;
    Impl(const std::string& file,const std::vector<std::string>& roots):path(file),database(file,roots) {}
    Event event(const std::string& id,const std::string& kind,const std::string& json) {
        const auto result=database.execute("INSERT INTO events(run_id,kind,payload) VALUES(?,?,?)",{id,kind,json});
        changed_one(result);
        if(!result.last_insert_id) throw DatabaseError("Event insert ID missing");
        return {*result.last_insert_id,id,kind,json};
    }
    void operation_state_event(const Operation& operation) {
        Json record={{"operation_id",operation.id},{"tool",operation.spec.tool}};
        if(!operation.decision_actor.empty()) record["decision_actor"]=operation.decision_actor;
        event(operation.spec.run_id,"operation."+to_string(operation.state),record.dump());
    }
    void cancel_waiting(const std::string& run_id) {
        const auto rows=database.execute("SELECT id,run_id,workspace,tool,arguments,state,expires_ms,result,decision_actor FROM operations WHERE run_id=? AND state IN ('awaiting_approval','ready')",{run_id}).rows;
        for(const auto& row:rows) {
            auto op=decode_operation(row);
            changed_one(database.execute("UPDATE operations SET state='cancelled' WHERE id=? AND state=?",{op.id,to_string(op.state)}));
            op.state=OperationState::cancelled;operation_state_event(op);
        }
    }
    bool has_operations(const std::string& id,const std::string& states) {
        // State list is an internal SQL literal, never caller-provided text.
        return !database.execute("SELECT id FROM operations WHERE run_id=? AND state IN ("+states+") LIMIT 1",{id}).rows.empty();
    }
};
Repository::Repository(const std::string& file,const std::vector<std::string>& roots):impl_(std::make_unique<Impl>(file,roots)) {
    auto& db=impl_->database; Transaction transaction(db);
    const auto version=integer(db.execute("PRAGMA user_version").rows.at(0).at(0));
    const auto application=integer(db.execute("PRAGMA application_id").rows.at(0).at(0));
    if((version==0 && application!=0) || (version!=0 && application!=0x584d494e))
        throw DatabaseError("Database is not the target xMind repository");
    if(version==0) {
        if(integer(db.execute("SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'").rows.at(0).at(0))!=0)
            throw DatabaseError("Existing database requires an explicit migration");
        for(const auto* sql:{
            "CREATE TABLE sessions(id TEXT PRIMARY KEY NOT NULL,title TEXT NOT NULL)",
            "CREATE TABLE runs(id TEXT PRIMARY KEY NOT NULL,session_id TEXT NOT NULL REFERENCES sessions(id),state TEXT NOT NULL CHECK(state IN ('queued','running','paused','completed','failed','cancelled')))",
            "CREATE UNIQUE INDEX one_active_run ON runs(session_id) WHERE state IN ('queued','running','paused')",
            "CREATE TABLE events(seq INTEGER PRIMARY KEY AUTOINCREMENT,run_id TEXT NOT NULL REFERENCES runs(id),kind TEXT NOT NULL,payload TEXT NOT NULL CHECK(json_valid(payload)))",
            "CREATE INDEX run_events ON events(run_id,seq)",
            "CREATE TABLE messages(seq INTEGER PRIMARY KEY AUTOINCREMENT,session_id TEXT NOT NULL REFERENCES sessions(id),role TEXT NOT NULL,payload TEXT NOT NULL CHECK(json_valid(payload)))",
            "CREATE INDEX session_messages ON messages(session_id,seq)",
            "CREATE TABLE information(category TEXT NOT NULL,id TEXT NOT NULL,payload TEXT NOT NULL CHECK(json_valid(payload)),PRIMARY KEY(category,id))",
            "PRAGMA application_id=0x584d494e", "PRAGMA user_version=1"}) db.execute(sql);
    } else if(version!=1 && version!=2 && version!=3) throw DatabaseError("Unsupported target repository version");
    if(version<2) {
        db.execute("CREATE TABLE credentials(scope TEXT NOT NULL,id TEXT NOT NULL,purpose TEXT NOT NULL,label TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>0),protection TEXT NOT NULL,ciphertext BLOB NOT NULL CHECK(length(ciphertext)>0),PRIMARY KEY(scope,id))");
        db.execute("CREATE TABLE retired_credentials(scope TEXT NOT NULL,id TEXT NOT NULL,PRIMARY KEY(scope,id))");
        db.execute("PRAGMA user_version=2");
    }
    if(version<3) {
        db.execute("CREATE TABLE operations(id TEXT PRIMARY KEY NOT NULL,run_id TEXT NOT NULL REFERENCES runs(id),workspace TEXT NOT NULL,tool TEXT NOT NULL,arguments TEXT NOT NULL CHECK(json_valid(arguments) AND json_type(arguments)='object'),state TEXT NOT NULL CHECK(state IN ('awaiting_approval','ready','denied','expired','cancelled','executing','succeeded','failed','uncertain')),expires_ms INTEGER NOT NULL,result TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(result) AND json_type(result)='object'),decision_actor TEXT NOT NULL DEFAULT '',CHECK(state NOT IN ('ready','denied','executing','succeeded','failed','uncertain') OR length(decision_actor)>0))");
        db.execute("CREATE INDEX run_operations ON operations(run_id,state)");
        db.execute("CREATE UNIQUE INDEX one_workspace_effect ON operations(workspace) WHERE state='executing'");
        db.execute("PRAGMA user_version=3");
    }
    transaction.commit(); db.execute("PRAGMA journal_mode=WAL"); db.execute("PRAGMA synchronous=FULL");
}
Repository::~Repository()=default;
Session Repository::create_session(const std::string& id,const std::string& title) {
    identifier(id); Transaction transaction(impl_->database);
    if(!impl_->database.execute("SELECT id FROM sessions WHERE id=?",{id}).rows.empty()) throw Conflict("Session already exists");
    changed_one(impl_->database.execute("INSERT INTO sessions(id,title) VALUES(?,?)",{id,title}));
    transaction.commit(); return {id,title};
}
Session Repository::session(const std::string& id) {
    const auto result=impl_->database.execute("SELECT id,title FROM sessions WHERE id=?",{id});
    if(result.rows.empty()) throw NotFound("Session not found");
    return {text(result.rows[0][0]),text(result.rows[0][1])};
}
std::vector<Session> Repository::sessions() {
    std::vector<Session> result;
    for(const auto& row:impl_->database.execute("SELECT id,title FROM sessions ORDER BY rowid").rows) result.push_back({text(row[0]),text(row[1])});
    return result;
}
Run Repository::run(const std::string& id) {
    const auto result=impl_->database.execute("SELECT id,session_id,state FROM runs WHERE id=?",{id});
    if(result.rows.empty()) throw NotFound("Run not found");
    return {text(result.rows[0][0]),text(result.rows[0][1]),state_value(text(result.rows[0][2]))};
}
Run Repository::create_run(const std::string& id,const std::string& session_id) {
    identifier(id); auto& db=impl_->database; Transaction transaction(db); session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE id=?",{id}).rows.empty()) throw Conflict("Run already exists");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND state IN ('queued','running','paused')",{session_id}).rows.empty())
        throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    impl_->event(id,"run.queued","{}"); transaction.commit(); return {id,session_id,RunState::queued};
}
Run Repository::start_prompt_run(const std::string& id,const std::string& session_id,const std::string& prompt_json) {
    identifier(id);auto& db=impl_->database;Transaction transaction(db);session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE id=?",{id}).rows.empty()) throw Conflict("Run already exists");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND state IN ('queued','running','paused')",{session_id}).rows.empty()) throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,prompt_json}));
    impl_->event(id,"run.queued","{}");transaction.commit();return {id,session_id,RunState::queued};
}
void Repository::append_user_message(const std::string& session_id,const std::string& json) {
    auto& db=impl_->database;Transaction transaction(db);session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND state IN ('queued','running','paused')",{session_id}).rows.empty()) throw Conflict("Session has an active root run");
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,json}));transaction.commit();
}
void Repository::record_tool_turn(const std::string& id,const std::string& assistant_json,const std::vector<std::string>& tool_json) {
    if(tool_json.empty() || tool_json.size()>64) throw std::invalid_argument("Invalid tool result batch");
    auto& db=impl_->database;Transaction transaction(db);const auto current=run(id);
    if(current.state!=RunState::running) throw Conflict("Run is not running");
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'assistant',?)",{current.session_id,assistant_json}));
    for(const auto& json:tool_json) changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'tool',?)",{current.session_id,json}));
    impl_->event(id,"conversation.tool_turn","{}");transaction.commit();
}
Run Repository::complete_run(const std::string& id,const std::string& assistant_json) {
    auto& db=impl_->database;Transaction transaction(db);auto current=run(id);
    if(current.state!=RunState::running) throw Conflict("Run is not running");
    if(impl_->has_operations(id,"'awaiting_approval','ready','executing','uncertain'")) throw Conflict("Run has unresolved operations");
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'assistant',?)",{current.session_id,assistant_json}));
    changed_one(db.execute("UPDATE runs SET state='completed' WHERE id=? AND state='running'",{id}));
    impl_->event(id,"conversation.assistant",assistant_json);impl_->event(id,"run.completed","{}");transaction.commit();current.state=RunState::completed;return current;
}
std::vector<Run> Repository::runs(const std::string& session_id) {
    session(session_id);std::vector<Run> result;
    for(const auto& row:impl_->database.execute("SELECT id,state FROM runs WHERE session_id=? ORDER BY rowid",{session_id}).rows)
        result.push_back({text(row[0]),session_id,state_value(text(row[1]))});
    return result;
}
Run Repository::transition(const std::string& id,RunState expected,RunState next,const std::string& json) {
    if(!allowed(expected,next)) throw std::invalid_argument("Invalid run transition");
    auto& db=impl_->database; Transaction transaction(db); auto result=run(id);
    if(impl_->has_operations(id,"'executing'")) throw Conflict("Run has an executing operation");
    if(next==RunState::completed && impl_->has_operations(id,"'awaiting_approval','ready','uncertain'")) throw Conflict("Run has unresolved operations");
    const auto changed=db.execute("UPDATE runs SET state=? WHERE id=? AND state=?",{state_name(next),id,state_name(expected)});
    if(changed.affected_rows==0) throw Conflict("Run state changed");
    changed_one(changed);
    if(terminal(next)) impl_->cancel_waiting(id);
    impl_->event(id,"run."+state_name(next),json);
    transaction.commit(); result.state=next; return result;
}
Event Repository::append_event(const std::string& id,const std::string& kind,const std::string& json) {
    identifier(kind); if(kind.rfind("run.",0)==0 || kind.rfind("operation.",0)==0) throw std::invalid_argument("Lifecycle events require their owning repository operation");
    Transaction transaction(impl_->database);
    if(terminal(run(id).state)) throw Conflict("Run is terminal");
    auto result=impl_->event(id,kind,json); transaction.commit(); return result;
}
std::vector<Event> Repository::events(const std::string& id,std::int64_t after) {
    if(after<0) throw std::invalid_argument("Negative cursor"); run(id); std::vector<Event> result;
    for(const auto& row:impl_->database.execute("SELECT seq,kind,payload FROM events WHERE run_id=? AND seq>? ORDER BY seq",{id,after}).rows)
        result.push_back({integer(row[0]),id,text(row[1]),text(row[2])});
    return result;
}
void Repository::append_message(const std::string& id,const std::string& role,const std::string& json) {
    identifier(role); Transaction transaction(impl_->database); session(id);
    changed_one(impl_->database.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,?,?)",{id,role,json}));
    transaction.commit();
}
std::vector<Message> Repository::history(const std::string& id) {
    session(id); std::vector<Message> result;
    for(const auto& row:impl_->database.execute("SELECT seq,role,payload FROM messages WHERE session_id=? ORDER BY seq",{id}).rows)
        result.push_back({integer(row[0]),text(row[1]),text(row[2])});
    return result;
}
void Repository::put_information(const std::string& category,const std::string& id,const std::string& json) {
    identifier(category); identifier(id);
    if(category=="secrets" || category=="credentials") throw std::invalid_argument("Use the encrypted credential repository");
    Transaction transaction(impl_->database);
    changed_one(impl_->database.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO UPDATE SET payload=excluded.payload",{category,id,json}));
    transaction.commit();
}
std::string Repository::information(const std::string& category,const std::string& id) {
    const auto result=impl_->database.execute("SELECT payload FROM information WHERE category=? AND id=?",{category,id});
    if(result.rows.empty()) throw NotFound("Information not found"); return text(result.rows[0][0]);
}
CredentialMetadata Repository::put_credential(const std::string& scope,const std::string& id,
    const std::string& purpose,const std::string& label,const SecretBytes& secret,std::int64_t expected_revision) {
    if(expected_revision<0 || expected_revision==std::numeric_limits<std::int64_t>::max())
        throw std::invalid_argument("Invalid credential revision");
    const auto revision=expected_revision+1;
    const auto context=credential_context(scope,id,purpose,revision);
    if(label.size()>4096 || label.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid credential label");
    const auto protected_value=protect_secret(secret,context);
    auto& db=impl_->database; Transaction transaction(db);
    const auto existing=db.execute("SELECT revision FROM credentials WHERE scope=? AND id=?",{scope,id});
    if(expected_revision==0) {
        if(!existing.rows.empty()) throw Conflict("Credential already exists");
        if(!db.execute("SELECT id FROM retired_credentials WHERE scope=? AND id=?",{scope,id}).rows.empty())
            throw Conflict("Credential identity was retired; use a new ID");
        changed_one(db.execute("INSERT INTO credentials(scope,id,purpose,label,revision,protection,ciphertext) VALUES(?,?,?,?,?,?,?)",
            {scope,id,purpose,label,revision,protected_value.protection,protected_value.ciphertext}));
    } else {
        if(existing.rows.empty()) throw NotFound("Credential not found");
        if(integer(existing.rows[0][0])!=expected_revision) throw Conflict("Credential revision changed");
        changed_one(db.execute("UPDATE credentials SET purpose=?,label=?,revision=?,protection=?,ciphertext=? WHERE scope=? AND id=? AND revision=?",
            {purpose,label,revision,protected_value.protection,protected_value.ciphertext,scope,id,expected_revision}));
    }
    transaction.commit();return {scope,id,purpose,label,revision};
}
std::vector<CredentialMetadata> Repository::credentials(const std::string& scope) {
    identifier(scope);std::vector<CredentialMetadata> result;
    for(const auto& row:impl_->database.execute("SELECT id,purpose,label,revision FROM credentials WHERE scope=? ORDER BY id",{scope}).rows)
        result.push_back({scope,text(row[0]),text(row[1]),text(row[2]),integer(row[3])});
    return result;
}
SecretBytes Repository::resolve_credential(const std::string& scope,const std::string& id,const std::string& purpose) {
    // Purpose is supplied by the calling connector, not accepted from public views.
    credential_context(scope,id,purpose,1);
    const auto rows=impl_->database.execute("SELECT purpose,revision,protection,ciphertext FROM credentials WHERE scope=? AND id=?",{scope,id}).rows;
    if(rows.empty()) throw NotFound("Credential not found");
    const auto& row=rows[0];
    if(text(row[0])!=purpose) throw Conflict("Credential purpose differs");
    return reveal_secret({text(row[2]),std::get<SqlBytes>(row[3])},credential_context(scope,id,purpose,integer(row[1])));
}
void Repository::delete_credential(const std::string& scope,const std::string& id,std::int64_t expected_revision) {
    identifier(scope);identifier(id);
    if(expected_revision<=0) throw std::invalid_argument("Invalid credential revision");
    auto& db=impl_->database;Transaction transaction(db);
    const auto rows=db.execute("SELECT revision FROM credentials WHERE scope=? AND id=?",{scope,id}).rows;
    if(rows.empty()) throw NotFound("Credential not found");
    if(integer(rows[0][0])!=expected_revision) throw Conflict("Credential revision changed");
    changed_one(db.execute("INSERT INTO retired_credentials(scope,id) VALUES(?,?)",{scope,id}));
    changed_one(db.execute("DELETE FROM credentials WHERE scope=? AND id=? AND revision=?",{scope,id,expected_revision}));
    transaction.commit();
}
Operation Repository::request_operation(const std::string& id,const OperationSpec& input,std::int64_t expiry) {
    identifier(id);identifier(input.run_id);identifier(input.workspace);identifier(input.tool);
    if(id.size()>256 || input.workspace.size()>32768 || input.tool.size()>128) throw std::invalid_argument("Operation identity exceeds its limit");
    const auto now=now_ms();
    if(expiry<=now || expiry>now+3600000) throw std::invalid_argument("Operation approval expiry must be within one hour");
    auto spec=input;spec.arguments_json=object_json(spec.arguments_json);
    auto& db=impl_->database;Transaction transaction(db);
    if(run(spec.run_id).state!=RunState::running) throw Conflict("Operation requires a running owner");
    if(!db.execute("SELECT id FROM operations WHERE id=?",{id}).rows.empty()) throw Conflict("Operation already exists");
    changed_one(db.execute("INSERT INTO operations(id,run_id,workspace,tool,arguments,state,expires_ms) VALUES(?,?,?,?,?,'awaiting_approval',?)",{id,spec.run_id,spec.workspace,spec.tool,spec.arguments_json,expiry}));
    Operation result{id,std::move(spec),OperationState::awaiting_approval,expiry,"{}",{}};
    impl_->operation_state_event(result);transaction.commit();return result;
}
Operation Repository::operation(const std::string& id) {
    const auto rows=impl_->database.execute("SELECT id,run_id,workspace,tool,arguments,state,expires_ms,result,decision_actor FROM operations WHERE id=?",{id}).rows;
    if(rows.empty()) throw NotFound("Operation not found");return decode_operation(rows[0]);
}
std::vector<Operation> Repository::operations(const std::string& id) {
    run(id);std::vector<Operation> result;
    for(const auto& row:impl_->database.execute("SELECT id,run_id,workspace,tool,arguments,state,expires_ms,result,decision_actor FROM operations WHERE run_id=? ORDER BY rowid",{id}).rows)
        result.push_back(decode_operation(row));
    return result;
}
Operation Repository::decide_operation(const std::string& id,OperationDecision decision,const std::string& actor) {
    identifier(actor);if(actor.size()>4096) throw std::invalid_argument("Controller identity exceeds its limit");
    try {(void)Json(actor).dump();} catch(const Json::exception&) {throw std::invalid_argument("Invalid controller identity encoding");}
    if(decision!=OperationDecision::allow && decision!=OperationDecision::deny) throw std::invalid_argument("Invalid operation decision");
    auto& db=impl_->database;Transaction transaction(db);auto result=operation(id);
    if(result.state!=OperationState::awaiting_approval) throw Conflict("Operation is no longer awaiting approval");
    const auto owner=run(result.spec.run_id);
    if(owner.state!=RunState::running && owner.state!=RunState::paused) throw Conflict("Operation owner is no longer active");
    const bool expired=result.expires_unix_ms<=now_ms();
    result.state=expired?OperationState::expired:(decision==OperationDecision::allow?OperationState::ready:OperationState::denied);
    if(!expired) result.decision_actor=actor;
    changed_one(db.execute("UPDATE operations SET state=?,decision_actor=? WHERE id=? AND state='awaiting_approval'",{to_string(result.state),result.decision_actor,id}));
    impl_->operation_state_event(result);transaction.commit();
    if(expired) throw Conflict("Operation approval expired");return result;
}
Operation Repository::claim_operation(const std::string& id,const OperationSpec& actual) {
    const auto arguments=object_json(actual.arguments_json);
    auto& db=impl_->database;Transaction transaction(db);auto result=operation(id);
    if(result.state!=OperationState::ready) throw Conflict("Operation has no unconsumed approval");
    if(result.spec.run_id!=actual.run_id || result.spec.workspace!=actual.workspace || result.spec.tool!=actual.tool || result.spec.arguments_json!=arguments)
        throw Conflict("Operation differs from its approval");
    if(run(actual.run_id).state!=RunState::running) throw Conflict("Operation owner is not running");
    const bool expired=result.expires_unix_ms<=now_ms();result.state=expired?OperationState::expired:OperationState::executing;
    if(!expired) {
        const auto blocked=db.execute("SELECT state FROM operations WHERE workspace=? AND state IN ('executing','uncertain') AND id<>? ORDER BY CASE state WHEN 'uncertain' THEN 0 ELSE 1 END LIMIT 1",{actual.workspace,id}).rows;
        if(!blocked.empty()) {
            if(text(blocked[0][0])=="uncertain") throw WorkspaceEffectUncertain("Workspace has an uncertain effect");
            throw WorkspaceEffectBusy("Workspace has an executing effect");
        }
    }
    changed_one(db.execute("UPDATE operations SET state=? WHERE id=? AND state='ready'",{to_string(result.state),id}));
    impl_->operation_state_event(result);transaction.commit();
    if(expired) throw Conflict("Operation approval expired");return result;
}
Operation Repository::cancel_operation(const std::string& id,const OperationSpec& actual) {
    const auto arguments=object_json(actual.arguments_json);
    auto& db=impl_->database;Transaction transaction(db);auto result=operation(id);
    if(result.spec.run_id!=actual.run_id || result.spec.workspace!=actual.workspace || result.spec.tool!=actual.tool || result.spec.arguments_json!=arguments)
        throw Conflict("Operation differs from its cancellation owner");
    if(result.state!=OperationState::awaiting_approval && result.state!=OperationState::ready) throw Conflict("Operation is no longer waiting");
    changed_one(db.execute("UPDATE operations SET state='cancelled' WHERE id=? AND state=?",{id,to_string(result.state)}));
    result.state=OperationState::cancelled;impl_->operation_state_event(result);transaction.commit();return result;
}
Operation Repository::expire_operation(const std::string& id) {
    auto& db=impl_->database;Transaction transaction(db);auto result=operation(id);
    if(result.state!=OperationState::awaiting_approval && result.state!=OperationState::ready) throw Conflict("Operation is no longer waiting");
    if(result.expires_unix_ms>now_ms()) throw Conflict("Operation has not expired");
    changed_one(db.execute("UPDATE operations SET state='expired' WHERE id=? AND state=?",{id,to_string(result.state)}));
    result.state=OperationState::expired;impl_->operation_state_event(result);transaction.commit();return result;
}
Operation Repository::finish_operation(const std::string& id,OperationState outcome,const std::string& source) {
    if(outcome!=OperationState::succeeded && outcome!=OperationState::failed && outcome!=OperationState::uncertain) throw std::invalid_argument("Invalid operation outcome");
    const auto result_json=object_json(source);
    auto& db=impl_->database;Transaction transaction(db);auto result=operation(id);
    if(result.state!=OperationState::executing) throw Conflict("Operation is not executing");
    // The executor must supply its actual outcome. An interrupted effect can
    // explicitly remain uncertain; it must never be silently granted again.
    changed_one(db.execute("UPDATE operations SET state=?,result=? WHERE id=? AND state='executing'",{to_string(outcome),result_json,id}));
    result.state=outcome;result.result_json=result_json;impl_->operation_state_event(result);transaction.commit();return result;
}
std::size_t Repository::recover_interrupted(const BackendLease& owner) {
    if(!owner.covers(impl_->path)) throw Conflict("Recovery requires this database's owner lease");
    auto& db=impl_->database; Transaction transaction(db);
    const auto roots=db.execute("SELECT id FROM runs WHERE state IN ('queued','running')").rows;
    for(const auto& row:roots) {
        const auto& id=text(row[0]);
        const auto executing=db.execute("SELECT id,run_id,workspace,tool,arguments,state,expires_ms,result,decision_actor FROM operations WHERE run_id=? AND state='executing'",{id}).rows;
        for(const auto& value:executing) {
            auto operation=decode_operation(value);
            changed_one(db.execute("UPDATE operations SET state='uncertain',result=? WHERE id=? AND state='executing'",{std::string(R"({"reason":"server_restart"})"),operation.id}));
            operation.state=OperationState::uncertain;impl_->operation_state_event(operation);
        }
        impl_->cancel_waiting(id);
    }
    const auto changed=db.execute("UPDATE runs SET state='failed' WHERE state IN ('queued','running')");
    if(changed.affected_rows!=static_cast<std::int64_t>(roots.size())) throw DatabaseError("Recovery affected-row count differs");
    for(const auto& row:roots) impl_->event(text(row[0]),"run.failed",R"({"reason":"server_restart"})");
    transaction.commit(); return roots.size();
}
}
