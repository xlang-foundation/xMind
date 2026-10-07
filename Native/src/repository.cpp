#include "agentflow/repository.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/graph.hpp"
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
std::vector<std::string> effect_resources(const OperationSpec& spec) {
    if(spec.resources.size()>16)throw std::invalid_argument("Operation resource count exceeds limits");
    std::vector<std::string> result{"workspace:"+spec.workspace};std::set<std::string> seen{result[0]};
    for(const auto& resource:spec.resources) {
        identifier(resource);if(resource.size()>4096 || !seen.insert(resource).second)throw std::invalid_argument("Invalid or duplicate operation resource");
        try{(void)Json(resource).dump();}catch(const Json::exception&){throw std::invalid_argument("Invalid operation resource encoding");}
        result.push_back(resource);
    }
    return result;
}
Operation decode_operation(const std::vector<SqlValue>& row,XlangSqlite& database) {
    Operation result{text(row[0]),{text(row[1]),text(row[2]),text(row[3]),text(row[4])},operation_state(text(row[5])),integer(row[6]),text(row[7]),text(row[8])};
    for(const auto& resource:database.execute("SELECT resource FROM operation_resources WHERE operation_id=? AND position>=0 ORDER BY position",{result.id}).rows)result.spec.resources.push_back(text(resource[0]));
    return result;
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
        if(kind=="run.queued"||kind=="run.running"||kind=="run.paused"||kind=="run.completed"||kind=="run.failed"||kind=="run.cancelled")
            changed_one(database.execute("INSERT INTO run_status_clock(run_id,updated_ms,status_seq) VALUES(?,?,?) ON CONFLICT(run_id) DO UPDATE SET updated_ms=excluded.updated_ms,status_seq=excluded.status_seq",{id,now_ms(),*result.last_insert_id}));
        return {*result.last_insert_id,id,kind,json};
    }
    void operation_state_event(const Operation& operation) {
        database.execute("UPDATE operation_resources SET state=? WHERE operation_id=?",{to_string(operation.state),operation.id});
        Json record={{"operation_id",operation.id},{"tool",operation.spec.tool}};
        if(!operation.decision_actor.empty()) record["decision_actor"]=operation.decision_actor;
        event(operation.spec.run_id,"operation."+to_string(operation.state),record.dump());
    }
    void cancel_waiting(const std::string& run_id) {
        const auto rows=database.execute("SELECT id,run_id,workspace,tool,arguments,state,expires_ms,result,decision_actor FROM operations WHERE run_id=? AND state IN ('awaiting_approval','ready')",{run_id}).rows;
        for(const auto& row:rows) {
            auto op=decode_operation(row,database);
            changed_one(database.execute("UPDATE operations SET state='cancelled' WHERE id=? AND state=?",{op.id,to_string(op.state)}));
            op.state=OperationState::cancelled;operation_state_event(op);
        }
    }
    bool has_operations(const std::string& id,const std::string& states) {
        // State list is an internal SQL literal, never caller-provided text.
        return !database.execute("SELECT id FROM operations WHERE run_id=? AND state IN ("+states+") LIMIT 1",{id}).rows.empty();
    }
    void message(const Run& run,const std::string& role,const std::string& json){changed_one(database.execute("INSERT INTO messages(session_id,execution_run_id,role,payload) VALUES(?,?,?,?)",{run.session_id,run.parent_id.empty()?SqlValue(nullptr):SqlValue(run.id),role,json}));changed_one(database.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{run.id}));}
    void root_boundary(const Run& current,RunState next){
        if(current.graph_root && (next==RunState::paused || terminal(next))){
            if(!database.execute("SELECT id FROM runs WHERE parent_run_id=? AND state IN ('queued','running','paused') LIMIT 1",{current.id}).rows.empty())throw Conflict("Graph still owns active child executions");
            if(next==RunState::completed && !database.execute("SELECT id FROM runs WHERE parent_run_id=? AND state!='completed' LIMIT 1",{current.id}).rows.empty())throw Conflict("Graph child did not complete");
            if(next==RunState::completed){const auto row=database.execute("SELECT specification,checkpoint FROM graph_roots WHERE run_id=?",{current.id}).rows.at(0);if(!GraphCoordinator(GraphPlan(text(row[0])),text(row[1]),GraphRestoreMode::live).inspect().finished)throw Conflict("Graph coordinator has unfinished nodes");}
        }
        if(!current.parent_id.empty() && (next==RunState::running || next==RunState::paused)){
            if(next==RunState::paused)throw Conflict("Agent graph children cannot own a human pause");
            if(database.execute("SELECT id FROM runs WHERE id=? AND state='running'",{current.parent_id}).rows.empty())throw Conflict("Graph parent is not running");
        }
    }
    void graph_checkpoint(GraphRootRecord& root,const GraphCoordinator& coordinator,std::int64_t expected=0){
        if(expected<0 || (expected>0 && expected!=root.checkpoint_revision))throw Conflict("Graph checkpoint revision changed");
        if(root.checkpoint_revision>=9007199254740991)throw std::overflow_error("Graph checkpoint revision exhausted");
        const auto checkpoint=coordinator.checkpoint();const auto result=database.execute("UPDATE graph_roots SET checkpoint=?,checkpoint_revision=checkpoint_revision+1 WHERE run_id=? AND checkpoint_revision=?",{checkpoint,root.run.id,root.checkpoint_revision});if(result.affected_rows!=1)throw Conflict("Graph checkpoint changed");root.checkpoint_json=checkpoint;++root.checkpoint_revision;
    }
    bool graph_waiting_only(const GraphCoordinator& coordinator){
        GraphDecision decisions;try{decisions=coordinator.inspect();}catch(const std::invalid_argument&){return false;}
        return !decisions.halted && !decisions.waiting_human.empty() && decisions.ready.empty() && decisions.skippable.empty() && decisions.running.empty();
    }
    void graph_human_pause(GraphRootRecord& root,const GraphCoordinator& coordinator){
        if(root.run.state==RunState::running && graph_waiting_only(coordinator)){
            root_boundary(root.run,RunState::paused);changed_one(database.execute("UPDATE runs SET state='paused' WHERE id=? AND state='running'",{root.run.id}));event(root.run.id,"run.paused",R"({"reason":"graph_human_wait"})");root.run.state=RunState::paused;
        }
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
    } else if(version<1 || version>9) throw DatabaseError("Unsupported target repository version");
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
    if(version<4) {
        db.execute("CREATE TABLE operation_resources(operation_id TEXT NOT NULL REFERENCES operations(id) ON DELETE CASCADE,resource TEXT NOT NULL,position INTEGER NOT NULL,state TEXT NOT NULL CHECK(state IN ('awaiting_approval','ready','denied','expired','cancelled','executing','succeeded','failed','uncertain')),PRIMARY KEY(operation_id,resource),UNIQUE(operation_id,position))");
        db.execute("CREATE INDEX resource_operations ON operation_resources(resource,state)");
        db.execute("INSERT INTO operation_resources(operation_id,resource,position,state) SELECT id,'workspace:'||workspace,-1,state FROM operations");
        if(!db.execute("SELECT id FROM operations WHERE tool='mcp_tool' AND (json_type(arguments,'$.server_config_id') IS NOT 'text' OR length(json_extract(arguments,'$.server_config_id')) NOT BETWEEN 1 AND 128 OR instr(json_extract(arguments,'$.server_config_id'),char(0))>0) LIMIT 1").rows.empty())throw DatabaseError("Legacy MCP operation lacks an attributable server resource");
        db.execute("INSERT INTO operation_resources(operation_id,resource,position,state) SELECT id,'mcp-server:'||json_extract(arguments,'$.server_config_id'),0,state FROM operations WHERE tool='mcp_tool'");
        db.execute("PRAGMA user_version=4");
    }
    if(version<5){
        db.execute("ALTER TABLE runs ADD COLUMN parent_run_id TEXT REFERENCES runs(id)");
        db.execute("ALTER TABLE runs ADD COLUMN node_id TEXT");
        db.execute("ALTER TABLE messages ADD COLUMN execution_run_id TEXT REFERENCES runs(id)");
        db.execute("DROP INDEX one_active_run");
        db.execute("CREATE UNIQUE INDEX one_active_run ON runs(session_id) WHERE parent_run_id IS NULL AND state IN ('queued','running','paused')");
        db.execute("CREATE UNIQUE INDEX graph_child_identity ON runs(parent_run_id,node_id) WHERE parent_run_id IS NOT NULL");
        db.execute("CREATE INDEX execution_messages ON messages(execution_run_id,seq)");
        db.execute("CREATE TABLE graph_roots(run_id TEXT PRIMARY KEY NOT NULL REFERENCES runs(id),graph_id TEXT NOT NULL,graph_revision INTEGER NOT NULL CHECK(graph_revision>0),specification TEXT NOT NULL CHECK(json_valid(specification)))");
        db.execute("CREATE TRIGGER graph_child_boundary BEFORE INSERT ON runs WHEN NEW.parent_run_id IS NOT NULL BEGIN SELECT CASE WHEN NEW.node_id IS NULL OR NOT EXISTS(SELECT 1 FROM runs r JOIN graph_roots g ON g.run_id=r.id WHERE r.id=NEW.parent_run_id AND r.parent_run_id IS NULL AND r.session_id=NEW.session_id AND r.state='running') THEN RAISE(ABORT,'invalid graph child boundary') END; END");
        db.execute("PRAGMA user_version=5");
    }
    if(version<6){
        db.execute("ALTER TABLE graph_roots ADD COLUMN checkpoint_revision INTEGER NOT NULL DEFAULT 1 CHECK(checkpoint_revision>0)");
        db.execute("ALTER TABLE graph_roots ADD COLUMN checkpoint TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(checkpoint))");
        for(const auto& row:db.execute("SELECT run_id,specification FROM graph_roots").rows){
            const auto id=text(row[0]);const GraphPlan plan(text(row[1]));auto saved=Json::parse(GraphCoordinator(plan).checkpoint());
            for(auto& node:saved["nodes"]){const auto child=db.execute("SELECT id,state FROM runs WHERE parent_run_id=? AND node_id=?",{id,node.at("id").get<std::string>()}).rows;if(child.empty())continue;
                if(text(child[0][1])=="completed"){const auto output=db.execute("SELECT payload FROM messages WHERE execution_run_id=? AND role='assistant' ORDER BY seq DESC LIMIT 1",{text(child[0][0])}).rows;if(output.empty())throw DatabaseError("Completed legacy graph child has no output");node["state"]="completed";node["output"]=Json::parse(text(output[0][0]));}else node["state"]="uncertain";
            }
            GraphCoordinator coordinator(plan,saved.dump());
            changed_one(db.execute("UPDATE graph_roots SET checkpoint=? WHERE run_id=?",{coordinator.checkpoint(),id}));
        }
        db.execute("PRAGMA user_version=6");
    }
    if(version<7){db.execute("ALTER TABLE graph_roots ADD COLUMN input TEXT CHECK(input IS NULL OR json_valid(input))");db.execute("PRAGMA user_version=7");}
    if(version<8){
        db.execute("CREATE TABLE task_history_owners(run_id TEXT PRIMARY KEY NOT NULL REFERENCES runs(id))");
        db.execute("CREATE TABLE task_messages(message_seq INTEGER PRIMARY KEY NOT NULL REFERENCES messages(seq),run_id TEXT NOT NULL REFERENCES runs(id))");
        db.execute("CREATE INDEX task_message_order ON task_messages(run_id,message_seq)");
        db.execute("CREATE TABLE incoming_messages(message_id TEXT PRIMARY KEY NOT NULL,run_id TEXT NOT NULL UNIQUE REFERENCES runs(id),context_id TEXT NOT NULL REFERENCES sessions(id),identity TEXT NOT NULL CHECK(json_valid(identity)),content TEXT NOT NULL)");
        db.execute("PRAGMA user_version=8");
    }
    if(version<9){
        // Existing journals have ordering but no recoverable wall-clock times.
        // Do not backfill fictional timestamps for those status transitions.
        db.execute("CREATE TABLE run_status_clock(run_id TEXT PRIMARY KEY NOT NULL REFERENCES runs(id),updated_ms INTEGER NOT NULL,status_seq INTEGER NOT NULL UNIQUE REFERENCES events(seq))");
        db.execute("CREATE INDEX root_status_order ON run_status_clock(updated_ms DESC,status_seq DESC)");db.execute("PRAGMA user_version=9");
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
    const auto result=impl_->database.execute("SELECT id,session_id,state,COALESCE(parent_run_id,''),COALESCE(node_id,''),EXISTS(SELECT 1 FROM graph_roots WHERE run_id=runs.id) FROM runs WHERE id=?",{id});
    if(result.rows.empty()) throw NotFound("Run not found");
    return {text(result.rows[0][0]),text(result.rows[0][1]),state_value(text(result.rows[0][2])),text(result.rows[0][3]),text(result.rows[0][4]),integer(result.rows[0][5])!=0};
}
Run Repository::create_run(const std::string& id,const std::string& session_id) {
    identifier(id); auto& db=impl_->database; Transaction transaction(db); session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE id=?",{id}).rows.empty()) throw Conflict("Run already exists");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty())
        throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));impl_->event(id,"run.queued","{}"); transaction.commit(); return {id,session_id,RunState::queued};
}
std::optional<Run> Repository::incoming_message(const std::string& message,const std::string& context,const std::string& identity,const std::string& content){
    if(message.empty()||message.size()>256||message.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid incoming message identity");if(!context.empty())identifier(context);object_json(identity);
    const auto rows=impl_->database.execute("SELECT run_id,context_id,identity,content FROM incoming_messages WHERE message_id=?",{message}).rows;if(rows.empty())return {};
    if((!context.empty()&&text(rows[0][1])!=context)||text(rows[0][2])!=Json::parse(identity).dump()||text(rows[0][3])!=content)throw Conflict("Incoming message identity was reused with changed input");return run(text(rows[0][0]));
}
Run Repository::start_incoming_message(const std::string& id,const std::string& context,const std::string& message,const std::string& prompt,const std::string& identity){
    identifier(id);object_json(prompt);const auto data=Json::parse(prompt);if(!data.contains("content")||!data["content"].is_string())throw std::invalid_argument("Incoming prompt must contain text");const auto content=data["content"].get<std::string>();if(content.empty()||content.size()>65536||content.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid incoming text");Transaction transaction(impl_->database);if(const auto replay=incoming_message(message,context,identity,content)){transaction.commit();return *replay;}
    const auto session_id=context.empty()?"ctx_"+id:context;identifier(session_id);auto& db=impl_->database;
    if(context.empty())changed_one(db.execute("INSERT INTO sessions(id,title) VALUES(?,'Inbound agent conversation')",{session_id}));else session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty())throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,prompt}));
    changed_one(db.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{id}));
    changed_one(db.execute("INSERT INTO incoming_messages(message_id,run_id,context_id,identity,content) VALUES(?,?,?,?,?)",{message,id,session_id,Json::parse(identity).dump(),content}));
    impl_->event(id,"run.queued","{}");transaction.commit();return {id,session_id,RunState::queued};
}
std::optional<std::vector<Message>> Repository::task_history(const std::string& id){
    run(id);if(impl_->database.execute("SELECT run_id FROM task_history_owners WHERE run_id=?",{id}).rows.empty())return {};
    std::vector<Message> result;for(const auto& row:impl_->database.execute("SELECT m.seq,m.role,m.payload FROM task_messages t JOIN messages m ON m.seq=t.message_seq WHERE t.run_id=? ORDER BY m.seq",{id}).rows)result.push_back({integer(row[0]),text(row[1]),text(row[2])});return result;
}
std::optional<std::string> Repository::incoming_message_payload(const std::string& id){
    run(id);const auto rows=impl_->database.execute("SELECT identity FROM incoming_messages WHERE run_id=?",{id}).rows;
    if(rows.empty())return {};return text(rows[0][0]);
}
Run Repository::start_prompt_run(const std::string& id,const std::string& session_id,const std::string& prompt_json) {
    identifier(id);auto& db=impl_->database;Transaction transaction(db);session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE id=?",{id}).rows.empty()) throw Conflict("Run already exists");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty()) throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,prompt_json}));
    changed_one(db.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));impl_->event(id,"run.queued","{}");transaction.commit();return {id,session_id,RunState::queued};
}
void Repository::append_user_message(const std::string& session_id,const std::string& json) {
    auto& db=impl_->database;Transaction transaction(db);session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND state IN ('queued','running','paused')",{session_id}).rows.empty()) throw Conflict("Session has an active root run");
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,json}));transaction.commit();
}
Run Repository::start_graph_run(const std::string& id,const std::string& session_id,const std::string& graph_id,std::int64_t revision,const GraphPlan& plan,const std::string& prompt){
    identifier(id);if(graph_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos || graph_id.empty())throw std::invalid_argument("Invalid graph identity");if(graph_id.size()>64 || revision<1 || revision>9007199254740991)throw std::invalid_argument("Invalid graph identity or revision");object_json(prompt);
    auto& db=impl_->database;Transaction transaction(db);session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE id=?",{id}).rows.empty())throw Conflict("Run already exists");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty())throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO graph_roots(run_id,graph_id,graph_revision,specification,checkpoint,input) VALUES(?,?,?,?,?,?)",{id,graph_id,revision,plan.json(),GraphCoordinator(plan).checkpoint(),prompt}));
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,prompt}));
    changed_one(db.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));impl_->event(id,"run.queued",Json{{"kind","graph"},{"graph_id",graph_id},{"graph_revision",revision}}.dump());transaction.commit();return {id,session_id,RunState::queued,{},{},true};
}
GraphRootRecord Repository::graph_run(const std::string& id){
    const auto current=run(id);if(!current.graph_root)throw Conflict("Run is not a graph root");const auto row=impl_->database.execute("SELECT graph_id,graph_revision,specification,checkpoint_revision,checkpoint,COALESCE(input,'') FROM graph_roots WHERE run_id=?",{id}).rows.at(0);return {current,text(row[0]),integer(row[1]),text(row[2]),integer(row[3]),text(row[4]),text(row[5])};
}
Run Repository::start_graph_child(const std::string& id,const std::string& parent_id,const std::string& node_id,const std::string& prompt,std::int64_t expected){
    identifier(id);if(node_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos || node_id.empty())throw std::invalid_argument("Invalid graph node identity");if(node_id.size()>64)throw std::invalid_argument("Graph node ID exceeds limits");object_json(prompt);auto& db=impl_->database;Transaction transaction(db);auto parent=graph_run(parent_id);
    if(parent.run.state!=RunState::running)throw Conflict("Graph parent is not running");
    const auto specification=Json::parse(parent.specification_json);bool executable=false;for(const auto& node:specification.at("nodes"))if(node.at("id")==node_id && (node.at("type")=="agent" || node.at("type")=="tool"))executable=true;if(!executable)throw std::invalid_argument("Child must name a declared executable node");
    if(!db.execute("SELECT id FROM runs WHERE id=? OR (parent_run_id=? AND node_id=?)",{id,parent_id,node_id}).rows.empty())throw Conflict("Graph child identity already exists");
    GraphCoordinator coordinator(GraphPlan(parent.specification_json),parent.checkpoint_json,GraphRestoreMode::live);coordinator.start(node_id);impl_->graph_checkpoint(parent,coordinator,expected);
    changed_one(db.execute("INSERT INTO runs(id,session_id,state,parent_run_id,node_id) VALUES(?,?,'queued',?,?)",{id,parent.run.session_id,parent_id,node_id}));
    const Run result{id,parent.run.session_id,RunState::queued,parent_id,node_id,false};impl_->message(result,"user",prompt);
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));impl_->event(id,"run.queued",Json{{"parent_run_id",parent_id},{"node_id",node_id}}.dump());impl_->event(parent_id,"graph.child.queued",Json{{"child_run_id",id},{"node_id",node_id}}.dump());transaction.commit();return result;
}
GraphRootRecord Repository::settle_graph_child(const std::string& id,std::int64_t expected){
    auto& db=impl_->database;Transaction transaction(db);const auto child=run(id);if(child.parent_id.empty() || !terminal(child.state))throw Conflict("Graph child has no observed terminal outcome");auto root=graph_run(child.parent_id);if(root.run.state!=RunState::running)throw Conflict("Graph root is not running");
    if(expected<0 || (expected>0 && root.checkpoint_revision!=expected))throw Conflict("Graph checkpoint revision changed");GraphCoordinator coordinator(GraphPlan(root.specification_json),root.checkpoint_json,GraphRestoreMode::live);
    const auto state=coordinator.state(child.node_id);if(state==GraphNodeState::completed || state==GraphNodeState::failed || state==GraphNodeState::uncertain){transaction.commit();return root;}
    if(child.state==RunState::completed){const auto history=run_history(child.id);if(history.empty() || history.back().role!="assistant")throw DatabaseError("Completed child has no assistant output");try{coordinator.complete(child.node_id,history.back().json);}catch(const std::invalid_argument&){coordinator.fail(child.node_id,false);}}
    else coordinator.fail(child.node_id,impl_->has_operations(child.id,"'executing','uncertain'") || !db.execute("SELECT seq FROM events WHERE run_id=? AND kind='run.failed' AND json_extract(payload,'$.reason')='server_restart'",{child.id}).rows.empty());
    impl_->graph_checkpoint(root,coordinator,expected);impl_->event(root.run.id,"graph.child.settled",Json{{"child_run_id",id},{"node_id",child.node_id},{"checkpoint_revision",root.checkpoint_revision},{"child_state",state_name(child.state)}}.dump());impl_->graph_human_pause(root,coordinator);transaction.commit();return root;
}
GraphRootRecord Repository::start_graph_human(const std::string& id,const std::string& node,std::int64_t expected){
    if(expected<1)throw std::invalid_argument("A graph checkpoint revision is required");auto& db=impl_->database;Transaction transaction(db);auto root=graph_run(id);if(root.run.state!=RunState::running)throw Conflict("Graph is not running");GraphCoordinator coordinator(GraphPlan(root.specification_json),root.checkpoint_json,GraphRestoreMode::live);if(coordinator.start(node).definition.kind!=GraphNodeKind::human)throw std::invalid_argument("Node is not a human step");impl_->graph_checkpoint(root,coordinator,expected);impl_->event(id,"graph.human.waiting",Json{{"node_id",node},{"checkpoint_revision",root.checkpoint_revision}}.dump());
    impl_->graph_human_pause(root,coordinator);
    transaction.commit();return root;
}
GraphRootRecord Repository::input_graph_human(const std::string& id,const std::string& node,const std::string& input,const std::string& actor,std::int64_t expected){
    identifier(actor);if(actor.size()>256 || expected<1)throw std::invalid_argument("Invalid graph controller input");object_json(input);auto& db=impl_->database;Transaction transaction(db);auto root=graph_run(id);if(root.run.state!=RunState::running && root.run.state!=RunState::paused)throw Conflict("Graph cannot receive human input");GraphCoordinator coordinator(GraphPlan(root.specification_json),root.checkpoint_json,GraphRestoreMode::live);coordinator.provide_human(node,input);impl_->graph_checkpoint(root,coordinator,expected);impl_->event(id,"graph.human.input",Json{{"node_id",node},{"actor",actor},{"input",Json::parse(input)},{"checkpoint_revision",root.checkpoint_revision}}.dump());if(root.run.state==RunState::paused && !impl_->graph_waiting_only(coordinator)){changed_one(db.execute("UPDATE runs SET state='running' WHERE id=? AND state='paused'",{id}));impl_->event(id,"run.running",R"({"reason":"graph_human_input"})");root.run.state=RunState::running;}transaction.commit();return root;
}
GraphRootRecord Repository::skip_graph_node(const std::string& id,const std::string& node,std::int64_t expected){if(expected<1)throw std::invalid_argument("A graph checkpoint revision is required");auto& db=impl_->database;Transaction transaction(db);auto root=graph_run(id);if(root.run.state!=RunState::running)throw Conflict("Graph is not running");GraphCoordinator coordinator(GraphPlan(root.specification_json),root.checkpoint_json,GraphRestoreMode::live);coordinator.skip(node);impl_->graph_checkpoint(root,coordinator,expected);impl_->event(id,"graph.node.skipped",Json{{"node_id",node},{"checkpoint_revision",root.checkpoint_revision}}.dump());impl_->graph_human_pause(root,coordinator);transaction.commit();return root;}
Run Repository::retire_graph_run(const std::string& id,RunState next,const std::string& reason){
    if(next!=RunState::failed && next!=RunState::cancelled)throw std::invalid_argument("Graph retirement requires a failed/cancelled outcome");object_json(reason);auto& db=impl_->database;Transaction transaction(db);auto root=graph_run(id);if(!allowed(root.run.state,next))throw Conflict("Graph cannot retire from its current state");impl_->root_boundary(root.run,next);GraphCoordinator coordinator(GraphPlan(root.specification_json),root.checkpoint_json,GraphRestoreMode::live);coordinator.cancel_pending();impl_->graph_checkpoint(root,coordinator);changed_one(db.execute("UPDATE runs SET state=? WHERE id=? AND state=?",{state_name(next),id,state_name(root.run.state)}));impl_->cancel_waiting(id);impl_->event(id,"run."+state_name(next),reason);transaction.commit();root.run.state=next;return root.run;
}
std::vector<Run> Repository::children(const std::string& id){graph_run(id);std::vector<Run> result;for(const auto& row:impl_->database.execute("SELECT id FROM runs WHERE parent_run_id=? ORDER BY rowid",{id}).rows)result.push_back(run(text(row[0])));return result;}
std::vector<Message> Repository::run_history(const std::string& id){const auto current=run(id);if(current.parent_id.empty())return history(current.session_id);std::vector<Message> result;for(const auto& row:impl_->database.execute("SELECT seq,role,payload FROM messages WHERE execution_run_id=? ORDER BY seq",{id}).rows)result.push_back({integer(row[0]),text(row[1]),text(row[2])});return result;}
std::vector<Event> Repository::graph_events(const std::string& id,std::int64_t after){if(after<0)throw std::invalid_argument("Negative cursor");graph_run(id);std::vector<Event> result;for(const auto& row:impl_->database.execute("SELECT e.seq,e.run_id,e.kind,e.payload FROM events e JOIN runs r ON r.id=e.run_id WHERE (r.id=? OR r.parent_run_id=?) AND e.seq>? ORDER BY e.seq",{id,id,after}).rows)result.push_back({integer(row[0]),text(row[1]),text(row[2]),text(row[3])});return result;}
void Repository::record_tool_turn(const std::string& id,const std::string& assistant_json,const std::vector<std::string>& tool_json) {
    if(tool_json.empty() || tool_json.size()>64) throw std::invalid_argument("Invalid tool result batch");
    auto& db=impl_->database;Transaction transaction(db);const auto current=run(id);
    if(current.state!=RunState::running) throw Conflict("Run is not running");
    impl_->message(current,"assistant",assistant_json);
    for(const auto& json:tool_json) impl_->message(current,"tool",json);
    impl_->event(id,"conversation.tool_turn","{}");transaction.commit();
}
Run Repository::complete_run(const std::string& id,const std::string& assistant_json) {
    auto& db=impl_->database;Transaction transaction(db);auto current=run(id);
    if(current.state!=RunState::running) throw Conflict("Run is not running");
    impl_->root_boundary(current,RunState::completed);
    if(impl_->has_operations(id,"'awaiting_approval','ready','executing','uncertain'")) throw Conflict("Run has unresolved operations");
    impl_->message(current,"assistant",assistant_json);
    changed_one(db.execute("UPDATE runs SET state='completed' WHERE id=? AND state='running'",{id}));
    impl_->event(id,"conversation.assistant",assistant_json);impl_->event(id,"run.completed","{}");transaction.commit();current.state=RunState::completed;return current;
}
std::vector<Run> Repository::runs(const std::string& session_id) {
    session(session_id);std::vector<Run> result;
    for(const auto& row:impl_->database.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL ORDER BY rowid",{session_id}).rows)result.push_back(run(text(row[0])));
    return result;
}
RootRunPage Repository::list_root_runs(const std::string& context,const std::string& state,std::optional<std::int64_t> since,std::size_t count,std::int64_t watermark,std::optional<std::int64_t> cursor_ms,std::int64_t cursor_sequence){
    if(count<1||count>100||watermark<0||cursor_sequence<0||(cursor_ms&&watermark==0))throw std::invalid_argument("Invalid task page");
    if(!context.empty())identifier(context);if(!state.empty()&&state!="queued"&&state!="running"&&state!="paused"&&state!="completed"&&state!="failed"&&state!="cancelled")throw std::invalid_argument("Invalid task state");
    auto& db=impl_->database;Transaction transaction(db);
    const auto latest=integer(db.execute("SELECT COALESCE(max(seq),0) FROM events").rows[0][0]);if(watermark==0)watermark=latest;else if(watermark>latest||cursor_sequence>watermark)throw std::invalid_argument("Invalid task page watermark");
    const std::string sequence="COALESCE(c.status_seq,(SELECT max(e.seq) FROM events e WHERE e.run_id=r.id AND e.kind IN ('run.queued','run.running','run.paused','run.completed','run.failed','run.cancelled')),0)";
    const std::string join=" FROM runs r LEFT JOIN run_status_clock c ON c.run_id=r.id WHERE r.parent_run_id IS NULL AND (?='' OR r.session_id=?) AND (?='' OR r.state=?)";
    const std::vector<SqlValue> filter{context,context,state,state};
    if(since&&!db.execute("SELECT r.id"+join+" AND c.run_id IS NULL LIMIT 1",filter).rows.empty())throw StatusTimeUnavailable("Legacy status timestamps are unavailable");
    const std::string bounded=join+" AND "+sequence+"<=? AND (? IS NULL OR c.updated_ms>=?)";auto parameters=filter;
    parameters.push_back(watermark);parameters.push_back(since?SqlValue(*since):SqlValue(nullptr));parameters.push_back(since?SqlValue(*since):SqlValue(nullptr));
    RootRunPage result{{},integer(db.execute("SELECT count(*)"+bounded,parameters).rows[0][0]),watermark,false};
    auto selection=bounded;if(cursor_ms){selection+=" AND (COALESCE(c.updated_ms,-1)<? OR (COALESCE(c.updated_ms,-1)=? AND "+sequence+"<?))";parameters.push_back(*cursor_ms);parameters.push_back(*cursor_ms);parameters.push_back(cursor_sequence);}
    parameters.push_back(static_cast<std::int64_t>(count+1));
    const auto rows=db.execute("SELECT r.id,c.updated_ms,"+sequence+selection+" ORDER BY COALESCE(c.updated_ms,-1) DESC,"+sequence+" DESC LIMIT ?",parameters).rows;
    for(const auto& row:rows){if(result.entries.size()==count){result.more=true;break;}if(integer(row[2])<1)throw DatabaseError("Task status journal is unavailable");result.entries.push_back({run(text(row[0])),std::holds_alternative<std::nullptr_t>(row[1])?std::optional<std::int64_t>{}:std::optional<std::int64_t>{integer(row[1])},integer(row[2])});}
    transaction.commit();return result;
}
Run Repository::transition(const std::string& id,RunState expected,RunState next,const std::string& json) {
    if(!allowed(expected,next)) throw std::invalid_argument("Invalid run transition");
    auto& db=impl_->database; Transaction transaction(db); auto result=run(id);
    impl_->root_boundary(result,next);
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
std::vector<Event> Repository::event_batch(const std::string& id,std::int64_t after,std::size_t count){
    if(after<0||count<1||count>256)throw std::invalid_argument("Invalid event batch");run(id);std::vector<Event> result;
    for(const auto& row:impl_->database.execute("SELECT seq,kind,payload FROM events WHERE run_id=? AND seq>? ORDER BY seq LIMIT ?",{id,after,static_cast<std::int64_t>(count)}).rows)
        result.push_back({integer(row[0]),id,text(row[1]),text(row[2])});return result;
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
    for(const auto& row:impl_->database.execute("SELECT seq,role,payload FROM messages WHERE session_id=? AND execution_run_id IS NULL ORDER BY seq",{id}).rows)
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
    const auto resources=effect_resources(input);
    auto spec=input;spec.arguments_json=object_json(spec.arguments_json);
    auto& db=impl_->database;Transaction transaction(db);
    if(run(spec.run_id).state!=RunState::running) throw Conflict("Operation requires a running owner");
    if(!db.execute("SELECT id FROM operations WHERE id=?",{id}).rows.empty()) throw Conflict("Operation already exists");
    changed_one(db.execute("INSERT INTO operations(id,run_id,workspace,tool,arguments,state,expires_ms) VALUES(?,?,?,?,?,'awaiting_approval',?)",{id,spec.run_id,spec.workspace,spec.tool,spec.arguments_json,expiry}));
    for(std::size_t index=0;index<resources.size();++index)changed_one(db.execute("INSERT INTO operation_resources(operation_id,resource,position,state) VALUES(?,?,?,'awaiting_approval')",{id,resources[index],static_cast<std::int64_t>(index)-1}));
    Operation result{id,std::move(spec),OperationState::awaiting_approval,expiry,"{}",{}};
    impl_->operation_state_event(result);transaction.commit();return result;
}
Operation Repository::operation(const std::string& id) {
    const auto rows=impl_->database.execute("SELECT id,run_id,workspace,tool,arguments,state,expires_ms,result,decision_actor FROM operations WHERE id=?",{id}).rows;
    if(rows.empty()) throw NotFound("Operation not found");return decode_operation(rows[0],impl_->database);
}
std::vector<Operation> Repository::operations(const std::string& id) {
    run(id);std::vector<Operation> result;
    for(const auto& row:impl_->database.execute("SELECT id,run_id,workspace,tool,arguments,state,expires_ms,result,decision_actor FROM operations WHERE run_id=? ORDER BY rowid",{id}).rows)
        result.push_back(decode_operation(row,impl_->database));
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
    const auto resources=effect_resources(actual);
    auto& db=impl_->database;Transaction transaction(db);auto result=operation(id);
    if(result.state!=OperationState::ready) throw Conflict("Operation has no unconsumed approval");
    if(result.spec.run_id!=actual.run_id || result.spec.workspace!=actual.workspace || result.spec.tool!=actual.tool || result.spec.arguments_json!=arguments || result.spec.resources!=actual.resources)
        throw Conflict("Operation differs from its approval");
    if(run(actual.run_id).state!=RunState::running) throw Conflict("Operation owner is not running");
    const bool expired=result.expires_unix_ms<=now_ms();result.state=expired?OperationState::expired:OperationState::executing;
    if(!expired) {
        // BEGIN IMMEDIATE serializes the complete multi-resource check/claim.
        // Check every key for uncertainty before considering ordinary busy
        // claims, so a wait cannot obscure a quarantined remote domain.
        bool busy=false;
        for(const auto& resource:resources) {
            const auto blocked=db.execute("SELECT o.state FROM operation_resources r JOIN operations o ON o.id=r.operation_id WHERE r.resource=? AND o.state IN ('executing','uncertain') AND o.id<>? ORDER BY CASE o.state WHEN 'uncertain' THEN 0 ELSE 1 END LIMIT 1",{resource,id}).rows;
            if(!blocked.empty()) {
                if(text(blocked[0][0])=="uncertain")throw WorkspaceEffectUncertain("Effect resource has an uncertain operation");
                busy=true;
            }
        }
        if(busy)throw WorkspaceEffectBusy("Effect resource has an executing operation");
    }
    changed_one(db.execute("UPDATE operations SET state=? WHERE id=? AND state='ready'",{to_string(result.state),id}));
    impl_->operation_state_event(result);transaction.commit();
    if(expired) throw Conflict("Operation approval expired");return result;
}
Operation Repository::cancel_operation(const std::string& id,const OperationSpec& actual) {
    const auto arguments=object_json(actual.arguments_json);
    (void)effect_resources(actual);
    auto& db=impl_->database;Transaction transaction(db);auto result=operation(id);
    if(result.spec.run_id!=actual.run_id || result.spec.workspace!=actual.workspace || result.spec.tool!=actual.tool || result.spec.arguments_json!=arguments || result.spec.resources!=actual.resources)
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
            auto operation=decode_operation(value,db);
            changed_one(db.execute("UPDATE operations SET state='uncertain',result=? WHERE id=? AND state='executing'",{std::string(R"({"reason":"server_restart"})"),operation.id}));
            operation.state=OperationState::uncertain;impl_->operation_state_event(operation);
        }
        impl_->cancel_waiting(id);
    }
    const auto changed=db.execute("UPDATE runs SET state='failed' WHERE state IN ('queued','running')");
    if(changed.affected_rows!=static_cast<std::int64_t>(roots.size())) throw DatabaseError("Recovery affected-row count differs");
    for(const auto& row:roots) impl_->event(text(row[0]),"run.failed",R"({"reason":"server_restart"})");
    for(const auto& row:db.execute("SELECT run_id FROM graph_roots").rows){auto root=graph_run(text(row[0]));const GraphCoordinator recovered(GraphPlan(root.specification_json),root.checkpoint_json);if(recovered.checkpoint()!=root.checkpoint_json){impl_->graph_checkpoint(root,recovered);impl_->event(root.run.id,"graph.recovered",Json{{"checkpoint_revision",root.checkpoint_revision},{"reason","interrupted_nodes_not_replayed"}}.dump());}}
    transaction.commit(); return roots.size();
}
}
