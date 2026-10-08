#include "agentflow/repository.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/graph.hpp"
#include <algorithm>
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
std::string provider_context(const Json& value){
    if(!value.is_object()||value.size()!=6)throw DatabaseError("Invalid saved provider context");
    for(const auto* field:{"profile_id","route_id","provider","wire","model_id"}){
        if(!value.contains(field)||!value[field].is_string())throw DatabaseError("Invalid saved provider identity");
        const auto identity=value[field].get<std::string>();if(identity.empty()||identity.size()>256||identity.starts_with("sk-")||identity.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)throw DatabaseError("Invalid saved provider identity");
    }
    if(!value.contains("profile_revision")||!value["profile_revision"].is_number_integer()||value["profile_revision"]<1||value["profile_revision"]>9007199254740991)throw DatabaseError("Invalid saved provider version");
    if(value["wire"]!="chat-completions"&&value["wire"]!="responses"&&value["wire"]!="anthropic-messages"&&value["wire"]!="gemini-generate-content")throw DatabaseError("Invalid saved provider wire");
    return value.dump();
}
std::string prompt_context(const std::string& prompt){
    try{
        const auto value=Json::parse(prompt);
        if(!value.is_object())throw DatabaseError("Invalid saved prompt JSON");
        return value.contains("provider_context")?provider_context(value["provider_context"]):std::string{};
    }catch(const Json::exception&){throw DatabaseError("Invalid saved prompt JSON");}
}
std::int64_t integer(const SqlValue& value) { return std::get<std::int64_t>(value); }
void identifier(const std::string& value) {
    if(value.empty() || value.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid identifier");
}
void bounded_identity(const std::string& value,std::size_t limit=128){
    identifier(value);if(value.size()>limit || value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos)
        throw std::invalid_argument("Invalid execution identity");
}
Json budget_provider_identity(const RootBudgetSpec& spec){
    if(spec.policy_id!="native.delegation" || spec.policy_revision!=1 || spec.workspace_identity.empty() || spec.workspace_identity.size()>4096 || spec.workspace_identity.find('\0')!=std::string::npos ||
        spec.max_children<1 || spec.max_children>8 || spec.max_parallel<1 || spec.max_parallel>2 || spec.max_model_calls<1 || spec.max_model_calls>32 || spec.wall_limit_ms<1 || spec.wall_limit_ms>3600000 || spec.provider_identity_json.size()>4096)
        throw std::invalid_argument("Invalid native execution budget policy");
    const auto identity=Json::parse(object_json(spec.provider_identity_json));
    if(identity.size()==6){(void)provider_context(identity);return identity;}
    if(identity.size()!=2 || !identity.contains("wire") || !identity["wire"].is_string() || !identity.contains("model_id") || !identity["model_id"].is_string())throw std::invalid_argument("Invalid public execution provider identity");
    const auto model=identity.at("model_id").get<std::string>();if(model.empty()||model.size()>256||model.starts_with("sk-")||model.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)throw std::invalid_argument("Invalid execution model identity");
    const auto wire=identity.at("wire").get<std::string>();if(wire!="chat-completions"&&wire!="responses"&&wire!="anthropic-messages"&&wire!="gemini-generate-content")throw std::invalid_argument("Invalid execution provider wire");
    return identity;
}
const char* call_role(ModelCallRole role){switch(role){case ModelCallRole::parent:return "parent";case ModelCallRole::leaf:return "leaf";}throw std::invalid_argument("Invalid model attempt role");}
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
    void initialize_budget(const std::string& id,const std::string& prompt,const RootBudgetSpec& spec){
        const auto provider=budget_provider_identity(spec);const auto input=Json::parse(object_json(prompt));
        if(provider.size()==6){if(!input.contains("provider_context") || input["provider_context"]!=provider)throw std::invalid_argument("Root prompt provider does not match its immutable budget");}
        else if(input.contains("provider_context"))throw std::invalid_argument("Unexpected root provider context");
        changed_one(database.execute("INSERT INTO agent_execution_budgets(root_run_id,policy_id,policy_revision,workspace_identity,provider_identity_json,max_children,max_parallel,max_model_calls,wall_limit_ms) VALUES(?,?,?,?,?,?,?,?,?)",{id,spec.policy_id,spec.policy_revision,spec.workspace_identity,provider.dump(),spec.max_children,spec.max_parallel,spec.max_model_calls,spec.wall_limit_ms}));
    }
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
        if(current.parent_id.empty() && !current.graph_root && (next==RunState::paused || terminal(next)) &&
            !database.execute("SELECT id FROM delegation_batches WHERE parent_run_id=? AND state IN ('accepted','working') LIMIT 1",{current.id}).rows.empty())
            throw Conflict("Parent still owns unsettled delegation outcomes");
        if(current.parent_id.empty() && (next==RunState::paused || terminal(next)) &&
            !database.execute("SELECT id FROM runs WHERE parent_run_id=? AND state IN ('queued','running','paused') LIMIT 1",{current.id}).rows.empty())
            throw Conflict("Parent still owns active child executions");
        if(current.graph_root && (next==RunState::paused || terminal(next))){
            if(!database.execute("SELECT id FROM runs WHERE parent_run_id=? AND state IN ('queued','running','paused') LIMIT 1",{current.id}).rows.empty())throw Conflict("Graph still owns active child executions");
            if(next==RunState::completed && !database.execute("SELECT id FROM runs WHERE parent_run_id=? AND state!='completed' LIMIT 1",{current.id}).rows.empty())throw Conflict("Graph child did not complete");
            if(next==RunState::completed){const auto row=database.execute("SELECT specification,checkpoint FROM graph_roots WHERE run_id=?",{current.id}).rows.at(0);if(!GraphCoordinator(GraphPlan(text(row[0])),text(row[1]),GraphRestoreMode::live).inspect().finished)throw Conflict("Graph coordinator has unfinished nodes");}
        }
        if(!current.parent_id.empty() && (next==RunState::running || next==RunState::paused)){
            if(next==RunState::paused)throw Conflict("Owned children cannot own a human pause");
            if(database.execute("SELECT id FROM runs WHERE id=? AND state='running'",{current.parent_id}).rows.empty())throw Conflict("Graph parent is not running");
            const auto delegation=database.execute("SELECT b.state FROM delegation_tasks t JOIN delegation_batches b ON b.id=t.batch_id WHERE t.child_run_id=?",{current.id}).rows;
            if(!delegation.empty()&&text(delegation[0][0])!="accepted"&&text(delegation[0][0])!="working")throw Conflict("Delegated admission is already retired");
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
    } else if(version<1 || version>10) throw DatabaseError("Unsupported target repository version");
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
    if(version<10){
        db.execute("CREATE TABLE agent_execution_budgets(root_run_id TEXT PRIMARY KEY NOT NULL REFERENCES runs(id),policy_id TEXT NOT NULL,policy_revision INTEGER NOT NULL CHECK(policy_revision>0),workspace_identity TEXT NOT NULL,provider_identity_json TEXT NOT NULL CHECK(json_valid(provider_identity_json) AND json_type(provider_identity_json)='object'),max_children INTEGER NOT NULL CHECK(max_children BETWEEN 1 AND 8),max_parallel INTEGER NOT NULL CHECK(max_parallel BETWEEN 1 AND 2),max_model_calls INTEGER NOT NULL CHECK(max_model_calls BETWEEN 1 AND 32),wall_limit_ms INTEGER NOT NULL CHECK(wall_limit_ms BETWEEN 1 AND 3600000),children_admitted INTEGER NOT NULL DEFAULT 0 CHECK(children_admitted BETWEEN 0 AND max_children),model_calls_reserved INTEGER NOT NULL DEFAULT 0 CHECK(model_calls_reserved BETWEEN 0 AND max_model_calls),parent_calls_held INTEGER NOT NULL DEFAULT 0 CHECK(parent_calls_held>=0),revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),CHECK(model_calls_reserved+parent_calls_held<=max_model_calls))");
        db.execute("CREATE TABLE agent_model_call_reservations(root_run_id TEXT NOT NULL REFERENCES agent_execution_budgets(root_run_id),attempt_id TEXT NOT NULL,owner_run_id TEXT NOT NULL REFERENCES runs(id),role TEXT NOT NULL CHECK(role IN ('parent','leaf')),state TEXT NOT NULL CHECK(state IN ('reserved','started','finished','interrupted')),PRIMARY KEY(root_run_id,attempt_id))");
        db.execute("CREATE TABLE delegation_batches(id TEXT PRIMARY KEY NOT NULL,parent_run_id TEXT NOT NULL REFERENCES runs(id),root_run_id TEXT NOT NULL REFERENCES agent_execution_budgets(root_run_id),provider_tool_call_id TEXT NOT NULL,arguments_json TEXT NOT NULL CHECK(json_valid(arguments_json) AND json_type(arguments_json)='object'),parent_assistant_json TEXT NOT NULL CHECK(json_valid(parent_assistant_json) AND json_type(parent_assistant_json)='object'),preset_id TEXT NOT NULL,preset_revision INTEGER NOT NULL CHECK(preset_revision>0),state TEXT NOT NULL CHECK(state IN ('accepted','working','completed','failed','cancelled','interrupted')),result_json TEXT CHECK(result_json IS NULL OR (json_valid(result_json) AND json_type(result_json)='object')),UNIQUE(parent_run_id,provider_tool_call_id))");
        db.execute("CREATE TABLE delegation_tasks(batch_id TEXT NOT NULL REFERENCES delegation_batches(id),task_id TEXT NOT NULL,node_id TEXT NOT NULL,child_run_id TEXT NOT NULL UNIQUE REFERENCES runs(id) DEFERRABLE INITIALLY DEFERRED,objective TEXT NOT NULL,outcome_json TEXT CHECK(outcome_json IS NULL OR (json_valid(outcome_json) AND json_type(outcome_json)='object')),settled_event_seq INTEGER REFERENCES events(seq),CHECK((outcome_json IS NULL AND settled_event_seq IS NULL) OR (outcome_json IS NOT NULL AND settled_event_seq IS NOT NULL)),PRIMARY KEY(batch_id,task_id),UNIQUE(batch_id,node_id))");
        db.execute("CREATE INDEX delegation_parent_order ON delegation_batches(parent_run_id)");
        db.execute("DROP TRIGGER graph_child_boundary");
        db.execute("CREATE TRIGGER owned_child_boundary BEFORE INSERT ON runs WHEN NEW.parent_run_id IS NOT NULL BEGIN SELECT CASE WHEN NEW.node_id IS NULL OR NOT (EXISTS(SELECT 1 FROM runs p JOIN graph_roots g ON g.run_id=p.id WHERE p.id=NEW.parent_run_id AND p.parent_run_id IS NULL AND p.session_id=NEW.session_id AND p.state='running') OR EXISTS(SELECT 1 FROM delegation_tasks t JOIN delegation_batches b ON b.id=t.batch_id JOIN agent_execution_budgets e ON e.root_run_id=b.root_run_id JOIN runs p ON p.id=b.parent_run_id WHERE t.child_run_id=NEW.id AND t.node_id=NEW.node_id AND b.parent_run_id=NEW.parent_run_id AND b.root_run_id=b.parent_run_id AND b.state IN ('accepted','working') AND p.parent_run_id IS NULL AND p.state='running' AND p.session_id=NEW.session_id AND NEW.state='queued' AND NOT EXISTS(SELECT 1 FROM graph_roots g WHERE g.run_id=p.id))) THEN RAISE(ABORT,'invalid owned child boundary') END; END");
        db.execute("CREATE TRIGGER delegation_task_initial_outcome BEFORE INSERT ON delegation_tasks WHEN NEW.outcome_json IS NOT NULL OR NEW.settled_event_seq IS NOT NULL BEGIN SELECT RAISE(ABORT,'new delegation task must be unsettled'); END");
        db.execute("CREATE TRIGGER delegation_task_identity_immutable BEFORE UPDATE OF batch_id,task_id,node_id,child_run_id,objective ON delegation_tasks BEGIN SELECT RAISE(ABORT,'accepted delegation task identity is immutable'); END");
        db.execute("CREATE TRIGGER delegation_task_settlement_boundary BEFORE UPDATE OF outcome_json,settled_event_seq ON delegation_tasks BEGIN SELECT CASE WHEN OLD.outcome_json IS NOT NULL OR OLD.settled_event_seq IS NOT NULL OR NOT EXISTS(SELECT 1 FROM events e JOIN runs c ON c.id=OLD.child_run_id WHERE e.seq=NEW.settled_event_seq AND e.run_id=c.id AND e.kind='delegation.child.settled' AND e.payload=NEW.outcome_json AND c.state IN ('completed','failed','cancelled') AND json_extract(NEW.outcome_json,'$.child_run_id')=c.id AND json_extract(NEW.outcome_json,'$.child_state')=c.state) THEN RAISE(ABORT,'invalid delegation settlement boundary') END; END");
        db.execute("CREATE TRIGGER delegation_batch_identity_immutable BEFORE UPDATE OF id,parent_run_id,root_run_id,provider_tool_call_id,arguments_json,parent_assistant_json,preset_id,preset_revision ON delegation_batches BEGIN SELECT RAISE(ABORT,'accepted delegation batch identity is immutable'); END");
        db.execute("CREATE TRIGGER agent_budget_identity_immutable BEFORE UPDATE OF root_run_id,policy_id,policy_revision,workspace_identity,provider_identity_json,max_children,max_parallel,max_model_calls,wall_limit_ms ON agent_execution_budgets BEGIN SELECT RAISE(ABORT,'admitted execution policy is immutable'); END");
        db.execute("CREATE TRIGGER model_call_identity_immutable BEFORE UPDATE OF root_run_id,attempt_id,owner_run_id,role ON agent_model_call_reservations BEGIN SELECT RAISE(ABORT,'model attempt identity is immutable'); END");
        db.execute("PRAGMA user_version=10");
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
Session Repository::rename_session(const std::string& id,const std::string& title,const std::string& expected_title) {
    identifier(id);
    if(title.empty()||title.size()>4096||title.find_first_not_of(" \t\r\n")==std::string::npos||expected_title.size()>4096||expected_title.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid session title");
    for(unsigned char byte:title)if(byte<32)throw std::invalid_argument("Use a single-line session title");
    Transaction transaction(impl_->database);const auto previous=session(id);
    if(previous.title!=expected_title)throw Conflict("Session title changed; refresh before renaming");
    changed_one(impl_->database.execute("UPDATE sessions SET title=? WHERE id=? AND title=?",{title,id,expected_title}));
    transaction.commit();return {id,title};
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
    const auto result=impl_->database.execute("SELECT id,session_id,state,COALESCE(parent_run_id,''),COALESCE(node_id,''),EXISTS(SELECT 1 FROM graph_roots WHERE run_id=runs.id),COALESCE((SELECT CASE WHEN json_type(m.payload,'$.provider_context') IS NULL THEN '' ELSE json_quote(json_extract(m.payload,'$.provider_context')) END FROM task_messages t JOIN messages m ON m.seq=t.message_seq WHERE t.run_id=runs.id AND m.role='user' ORDER BY m.seq LIMIT 1),'') FROM runs WHERE id=?",{id});
    if(result.rows.empty()) throw NotFound("Run not found");
    const auto& context=text(result.rows[0][6]);return {text(result.rows[0][0]),text(result.rows[0][1]),state_value(text(result.rows[0][2])),text(result.rows[0][3]),text(result.rows[0][4]),integer(result.rows[0][5])!=0,context.empty()?std::string{}:provider_context(Json::parse(context))};
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
Run Repository::start_incoming_message(const std::string& id,const std::string& context,const std::string& message,const std::string& prompt,const std::string& identity,std::optional<RootBudgetSpec> budget){
    identifier(id);object_json(prompt);const auto provider=prompt_context(prompt);const auto data=Json::parse(prompt);if(!data.contains("content")||!data["content"].is_string())throw std::invalid_argument("Incoming prompt must contain text");const auto content=data["content"].get<std::string>();if(content.empty()||content.size()>65536||content.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid incoming text");Transaction transaction(impl_->database);if(const auto replay=incoming_message(message,context,identity,content)){transaction.commit();return *replay;}
    const auto session_id=context.empty()?"ctx_"+id:context;identifier(session_id);auto& db=impl_->database;
    if(context.empty())changed_one(db.execute("INSERT INTO sessions(id,title) VALUES(?,'Inbound agent conversation')",{session_id}));else session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty())throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,prompt}));
    changed_one(db.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{id}));
    changed_one(db.execute("INSERT INTO incoming_messages(message_id,run_id,context_id,identity,content) VALUES(?,?,?,?,?)",{message,id,session_id,Json::parse(identity).dump(),content}));
    if(budget)impl_->initialize_budget(id,prompt,*budget);
    impl_->event(id,"run.queued","{}");transaction.commit();return {id,session_id,RunState::queued,{},{},false,provider};
}
std::optional<std::vector<Message>> Repository::task_history(const std::string& id){
    run(id);if(impl_->database.execute("SELECT run_id FROM task_history_owners WHERE run_id=?",{id}).rows.empty())return {};
    std::vector<Message> result;for(const auto& row:impl_->database.execute("SELECT m.seq,m.role,m.payload FROM task_messages t JOIN messages m ON m.seq=t.message_seq WHERE t.run_id=? ORDER BY m.seq",{id}).rows)result.push_back({integer(row[0]),text(row[1]),text(row[2])});return result;
}
std::optional<std::string> Repository::incoming_message_payload(const std::string& id){
    run(id);const auto rows=impl_->database.execute("SELECT identity FROM incoming_messages WHERE run_id=?",{id}).rows;
    if(rows.empty())return {};return text(rows[0][0]);
}
Run Repository::start_prompt_run(const std::string& id,const std::string& session_id,const std::string& prompt_json,std::optional<RootBudgetSpec> budget) {
    identifier(id);const auto provider=prompt_context(prompt_json);auto& db=impl_->database;Transaction transaction(db);session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE id=?",{id}).rows.empty()) throw Conflict("Run already exists");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty()) throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,prompt_json}));
    changed_one(db.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));if(budget)impl_->initialize_budget(id,prompt_json,*budget);impl_->event(id,"run.queued","{}");transaction.commit();return {id,session_id,RunState::queued,{},{},false,provider};
}
RootBudgetRecord Repository::root_budget(const std::string& id){
    const auto current=run(id);if(!current.parent_id.empty()||current.graph_root)throw std::invalid_argument("Native delegation budget requires an ordinary root");
    const auto rows=impl_->database.execute("SELECT policy_id,policy_revision,workspace_identity,provider_identity_json,max_children,max_parallel,max_model_calls,wall_limit_ms,children_admitted,model_calls_reserved,parent_calls_held,revision FROM agent_execution_budgets WHERE root_run_id=?",{id}).rows;
    if(rows.empty())throw NotFound("Execution budget not found");const auto& r=rows[0];
    RootBudgetRecord result{id,{text(r[0]),integer(r[1]),text(r[2]),text(r[3]),integer(r[4]),integer(r[5]),integer(r[6]),integer(r[7])},integer(r[8]),integer(r[9]),integer(r[10]),integer(r[11])};
    (void)budget_provider_identity(result.spec);return result;
}
ModelCallReservation Repository::reserve_model_call(const std::string& root,const std::string& owner,const std::string& attempt,ModelCallRole role){
    bounded_identity(attempt);const auto role_name=std::string(call_role(role));auto& db=impl_->database;Transaction transaction(db);
    const auto budget=root_budget(root);const auto parent=run(root),actor=run(owner);
    if(parent.state!=RunState::running || actor.state!=RunState::running)throw Conflict("Model attempt requires its running execution owners");
    if(role==ModelCallRole::parent){if(owner!=root)throw Conflict("Parent model attempt owner differs");}
    else {const auto admitted=child_admission(owner);if(admitted.kind!=ChildAdmissionKind::delegated_leaf||admitted.root_run_id!=root)throw Conflict("Leaf model attempt is not owned by this root");}
    const auto previous=db.execute("SELECT owner_run_id,role,state FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{root,attempt}).rows;
    if(!previous.empty()){if(text(previous[0][0])!=owner||text(previous[0][1])!=role_name)throw Conflict("Model attempt identity changed");transaction.commit();return {root,attempt,owner,role,text(previous[0][2])};}
    const auto held=role==ModelCallRole::parent&&budget.parent_calls_held>0?1:0;
    if(budget.model_calls_reserved+budget.parent_calls_held-held>=budget.spec.max_model_calls)throw RootBudgetExhausted("Root model-call allowance exhausted");
    if(budget.revision>=9007199254740991)throw std::overflow_error("Root budget revision exhausted");
    changed_one(db.execute("UPDATE agent_execution_budgets SET model_calls_reserved=model_calls_reserved+1,parent_calls_held=parent_calls_held-?,revision=revision+1 WHERE root_run_id=? AND revision=?",{static_cast<std::int64_t>(held),root,budget.revision}));
    changed_one(db.execute("INSERT INTO agent_model_call_reservations(root_run_id,attempt_id,owner_run_id,role,state) VALUES(?,?,?,?,'reserved')",{root,attempt,owner,role_name}));
    impl_->event(owner,"budget.model_call.reserved",Json{{"root_run_id",root},{"attempt_id",attempt},{"role",role_name}}.dump());transaction.commit();return {root,attempt,owner,role,"reserved"};
}
ModelCallReservation Repository::start_model_call(const std::string& root,const std::string& owner,const std::string& attempt){
    auto& db=impl_->database;Transaction transaction(db);(void)root_budget(root);const auto parent=run(root),actor=run(owner);
    if(parent.state!=RunState::running||actor.state!=RunState::running)throw Conflict("Model attempt owners are not running");
    const auto rows=db.execute("SELECT role,state FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=? AND owner_run_id=?",{root,attempt,owner}).rows;
    if(rows.empty())throw NotFound("Owned model attempt not found");if(text(rows[0][1])!="reserved")throw Conflict("Model attempt has already started or retired");
    const auto role=text(rows[0][0])=="parent"?ModelCallRole::parent:ModelCallRole::leaf;
    if(role==ModelCallRole::parent){if(root!=owner)throw Conflict("Invalid parent attempt owner");}else {const auto admitted=child_admission(owner);if(admitted.kind!=ChildAdmissionKind::delegated_leaf||admitted.root_run_id!=root)throw Conflict("Invalid leaf attempt owner");}
    changed_one(db.execute("UPDATE agent_model_call_reservations SET state='started' WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND state='reserved'",{root,attempt,owner}));
    impl_->event(owner,"budget.model_call.started",Json{{"root_run_id",root},{"attempt_id",attempt},{"role",call_role(role)}}.dump());transaction.commit();return {root,attempt,owner,role,"started"};
}
ModelCallReservation Repository::finish_model_call(const std::string& root,const std::string& owner,const std::string& attempt){
    auto& db=impl_->database;Transaction transaction(db);const auto rows=db.execute("SELECT role,state FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=? AND owner_run_id=?",{root,attempt,owner}).rows;
    if(rows.empty())throw NotFound("Owned model attempt not found");const auto role=text(rows[0][0])=="parent"?ModelCallRole::parent:ModelCallRole::leaf;const auto state=text(rows[0][1]);
    if(state=="finished"){transaction.commit();return {root,attempt,owner,role,state};}
    if(state!="reserved"&&state!="started")throw Conflict("Interrupted attempt cannot be adopted");
    changed_one(db.execute("UPDATE agent_model_call_reservations SET state='finished' WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND state=?",{root,attempt,owner,state}));
    impl_->event(owner,"budget.model_call.finished",Json{{"root_run_id",root},{"attempt_id",attempt},{"role",call_role(role)},{"was_started",state=="started"}}.dump());transaction.commit();return {root,attempt,owner,role,"finished"};
}
DelegationBatchRecord Repository::delegation_batch(const std::string& id){
    const auto rows=impl_->database.execute("SELECT parent_run_id,root_run_id,provider_tool_call_id,arguments_json,parent_assistant_json,preset_id,preset_revision,state,COALESCE(result_json,'') FROM delegation_batches WHERE id=?",{id}).rows;
    if(rows.empty())throw NotFound("Delegation batch not found");const auto& r=rows[0];
    DelegationBatchRecord result{id,text(r[0]),text(r[1]),text(r[2]),text(r[3]),text(r[4]),text(r[5]),integer(r[6]),text(r[7]),text(r[8]),{},false};
    for(const auto& t:impl_->database.execute("SELECT child_run_id,task_id,COALESCE(outcome_json,''),settled_event_seq FROM delegation_tasks WHERE batch_id=? ORDER BY rowid",{id}).rows)
        result.tasks.push_back({run(text(t[0])),id,text(t[1]),result.preset_id,result.preset_revision,text(t[2]),std::holds_alternative<std::nullptr_t>(t[3])?std::optional<std::int64_t>{}:std::optional<std::int64_t>{integer(t[3])}});
    return result;
}
std::vector<DelegationBatchRecord> Repository::delegation_batches(const std::string& parent){
    const auto current=run(parent);if(!current.parent_id.empty())throw std::invalid_argument("Delegation batches require their root parent");
    std::vector<DelegationBatchRecord> result;for(const auto& row:impl_->database.execute("SELECT id FROM delegation_batches WHERE parent_run_id=? ORDER BY rowid",{parent}).rows)result.push_back(delegation_batch(text(row[0])));return result;
}
DelegationBatchRecord Repository::accept_delegation_batch(const DelegationBatchSpec& spec){
    bounded_identity(spec.id);bounded_identity(spec.provider_tool_call_id,256);
    if(spec.preset_id!="workspace.inspect"||spec.preset_revision!=1||spec.expected_budget_revision<1||spec.arguments_json.size()>65536||spec.tasks.empty()||spec.tasks.size()>4)throw std::invalid_argument("Invalid registered delegation request");
    const auto arguments=Json::parse(object_json(spec.arguments_json)),assistant=Json::parse(object_json(spec.parent_assistant_json));
    if(arguments.size()!=1||!arguments.contains("tasks")||!arguments["tasks"].is_array()||arguments["tasks"].size()!=spec.tasks.size())throw std::invalid_argument("Invalid delegation task arguments");
    bool matched_call=false;std::set<std::string> call_ids;
    if(!assistant.contains("tool_calls")||!assistant["tool_calls"].is_array())throw std::invalid_argument("Delegation requires its real assistant tool call");
    for(const auto& call:assistant["tool_calls"]){
        if(!call.is_object()||!call.contains("id")||!call["id"].is_string()||!call_ids.insert(call["id"].get<std::string>()).second)throw std::invalid_argument("Invalid assistant tool-call identity");
        if(call["id"]==spec.provider_tool_call_id){if(!call.contains("name")||call["name"]!="delegate_tasks"||!call.contains("arguments")||!call["arguments"].is_string()||call["arguments"].get<std::string>()!=spec.arguments_json)throw std::invalid_argument("Delegation call does not match its actual assistant turn");matched_call=true;}
    }
    if(!matched_call)throw std::invalid_argument("Delegation call is absent from its assistant turn");
    auto& db=impl_->database;Transaction transaction(db);const auto parent=run(spec.parent_run_id);const auto budget=root_budget(parent.id);
    if(parent.state!=RunState::running)throw Conflict("Delegation parent is not running");
    const auto provider=Json::parse(budget.spec.provider_identity_json);std::set<std::string> labels,nodes,children;
    std::size_t position=0;
    for(const auto& task:spec.tasks){
        bounded_identity(task.task_id,32);bounded_identity(task.node_id,128);bounded_identity(task.child_run_id,128);
        if(task.task_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid delegation task label");
        if(task.objective.empty()||task.objective.size()>8192||task.objective.find('\0')!=std::string::npos||!labels.insert(task.task_id).second||!nodes.insert(task.node_id).second||!children.insert(task.child_run_id).second)throw std::invalid_argument("Invalid or duplicated delegated task");
        const auto& requested=arguments["tasks"][position++];
        if(!requested.is_object()||requested.size()!=3||!requested.contains("id")||requested["id"]!=task.task_id||!requested.contains("objective")||requested["objective"]!=task.objective||!requested.contains("preset")||requested["preset"]!=spec.preset_id)throw std::invalid_argument("Delegated child does not match the requested preset and objective");
        const auto prompt=Json::parse(object_json(task.prompt_json));
        if(!prompt.contains("content")||prompt["content"]!=task.objective||prompt.size()!=(provider.size()==6?2:1))throw std::invalid_argument("Delegated child prompt is not its bounded objective");
        if(provider.size()==6){if(!prompt.contains("provider_context")||prompt["provider_context"]!=provider)throw std::invalid_argument("Delegated child provider identity differs");}
        else if(prompt.contains("provider_context"))throw std::invalid_argument("Unexpected delegated provider context");
    }
    const auto previous=db.execute("SELECT id FROM delegation_batches WHERE parent_run_id=? AND provider_tool_call_id=?",{parent.id,spec.provider_tool_call_id}).rows;
    if(!previous.empty()){
        auto result=delegation_batch(text(previous[0][0]));
        if(result.arguments_json!=spec.arguments_json||result.parent_assistant_json!=spec.parent_assistant_json||result.preset_id!=spec.preset_id||result.preset_revision!=spec.preset_revision)throw Conflict("Delegation tool-call identity changed");
        if(result.state=="interrupted")throw Conflict("Interrupted delegation cannot be adopted");transaction.commit();return result;
    }
    if(budget.revision!=spec.expected_budget_revision)throw Conflict("Execution budget changed before child admission");
    if(budget.children_admitted+static_cast<std::int64_t>(spec.tasks.size())>budget.spec.max_children || budget.model_calls_reserved+budget.parent_calls_held>=budget.spec.max_model_calls)throw RootBudgetExhausted("Delegation cannot preserve its child and parent allowances");
    if(budget.revision>=9007199254740991)throw std::overflow_error("Root budget revision exhausted");
    for(const auto& task:spec.tasks){
        if(!db.execute("SELECT id FROM runs WHERE id=? OR (parent_run_id=? AND node_id=?)",{task.child_run_id,parent.id,task.node_id}).rows.empty())throw Conflict("Delegated child identity already exists");
    }
    changed_one(db.execute("INSERT INTO delegation_batches(id,parent_run_id,root_run_id,provider_tool_call_id,arguments_json,parent_assistant_json,preset_id,preset_revision,state) VALUES(?,?,?,?,?,?,?,?,'accepted')",{spec.id,parent.id,parent.id,spec.provider_tool_call_id,spec.arguments_json,spec.parent_assistant_json,spec.preset_id,spec.preset_revision}));
    changed_one(db.execute("UPDATE agent_execution_budgets SET children_admitted=children_admitted+?,parent_calls_held=parent_calls_held+1,revision=revision+1 WHERE root_run_id=? AND revision=?",{static_cast<std::int64_t>(spec.tasks.size()),parent.id,budget.revision}));
    auto identities=Json::array();
    for(const auto& task:spec.tasks){
        changed_one(db.execute("INSERT INTO delegation_tasks(batch_id,task_id,node_id,child_run_id,objective) VALUES(?,?,?,?,?)",{spec.id,task.task_id,task.node_id,task.child_run_id,task.objective}));
        changed_one(db.execute("INSERT INTO runs(id,session_id,state,parent_run_id,node_id) VALUES(?,?,'queued',?,?)",{task.child_run_id,parent.session_id,parent.id,task.node_id}));
        changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{task.child_run_id}));
        const Run child{task.child_run_id,parent.session_id,RunState::queued,parent.id,task.node_id,false,parent.provider_context_json};impl_->message(child,"user",task.prompt_json);impl_->event(child.id,"run.queued","{}");
        const Json admitted={{"batch_id",spec.id},{"parent_run_id",parent.id},{"task_id",task.task_id},{"child_run_id",child.id},{"preset_id",spec.preset_id},{"preset_revision",spec.preset_revision}};
        impl_->event(child.id,"delegation.child.admitted",admitted.dump());identities.push_back(admitted);
    }
    impl_->event(parent.id,"delegation.batch.accepted",Json{{"batch_id",spec.id},{"provider_tool_call_id",spec.provider_tool_call_id},{"preset_id",spec.preset_id},{"preset_revision",spec.preset_revision},{"children",identities}}.dump());
    auto result=delegation_batch(spec.id);result.created=true;transaction.commit();return result;
}
ChildAdmissionRecord Repository::child_admission(const std::string& id){
    const auto child=run(id);if(child.parent_id.empty())throw std::invalid_argument("Child admission requires a child run");
    const auto parent=run(child.parent_id);if(!parent.parent_id.empty()||parent.session_id!=child.session_id)throw Conflict("Owned child boundary differs");
    if(parent.graph_root){
        const auto graph=Json::parse(graph_run(parent.id).specification_json);for(const auto& node:graph["nodes"])if(node["id"]==child.node_id){
            if(node["type"]=="agent")return {ChildAdmissionKind::graph_agent,parent.id,{},{},0};
            if(node["type"]=="tool")return {ChildAdmissionKind::graph_tool,parent.id,{},{},0};
        }throw Conflict("Graph child is not its declared executable node");
    }
    const auto rows=impl_->database.execute("SELECT b.id,b.root_run_id,b.preset_id,b.preset_revision,b.state,t.node_id FROM delegation_tasks t JOIN delegation_batches b ON b.id=t.batch_id WHERE t.child_run_id=? AND b.parent_run_id=?",{id,parent.id}).rows;
    if(rows.empty())throw Conflict("Child has no accepted native delegation");const auto& r=rows[0];
    if(text(r[1])!=parent.id||text(r[5])!=child.node_id||text(r[2])!="workspace.inspect"||integer(r[3])!=1)throw Conflict("Delegated child ownership or preset differs");
    (void)root_budget(parent.id);return {ChildAdmissionKind::delegated_leaf,parent.id,text(r[0]),text(r[2]),integer(r[3])};
}
std::vector<OwnedChildRecord> Repository::owned_children(const std::string& id){
    const auto parent=run(id);if(!parent.parent_id.empty())throw std::invalid_argument("Owned child observation requires a root parent");std::vector<OwnedChildRecord> result;
    for(const auto& row:impl_->database.execute("SELECT id FROM runs WHERE parent_run_id=? ORDER BY rowid",{id}).rows){const auto child=run(text(row[0]));const auto admission=child_admission(child.id);std::string label;
        if(admission.kind==ChildAdmissionKind::delegated_leaf){const auto task=impl_->database.execute("SELECT task_id FROM delegation_tasks WHERE child_run_id=?",{child.id}).rows;label=text(task.at(0).at(0));}
        result.push_back({child,admission.kind==ChildAdmissionKind::delegated_leaf?"delegated_leaf":admission.kind==ChildAdmissionKind::graph_agent?"graph_agent":"graph_tool",admission.batch_id,label,admission.preset_id,admission.preset_revision});
    }return result;
}
std::vector<Message> Repository::owned_child_history(const std::string& parent,const std::string& child){
    const auto root=run(parent),owned=run(child);if(!root.parent_id.empty()||owned.parent_id!=parent||owned.session_id!=root.session_id)throw NotFound("Child does not belong to this root");(void)child_admission(child);return run_history(child);
}
std::vector<Event> Repository::tree_events(const std::string& id,std::int64_t after,std::size_t count){
    if(after<0||count<1||count>256)throw std::invalid_argument("Invalid execution tree cursor");const auto parent=run(id);if(!parent.parent_id.empty())throw std::invalid_argument("Tree events require a root parent");std::vector<Event> result;
    for(const auto& row:impl_->database.execute("SELECT e.seq,e.run_id,e.kind,e.payload FROM events e JOIN runs r ON r.id=e.run_id WHERE (r.id=? OR r.parent_run_id=?) AND r.session_id=? AND e.seq>? ORDER BY e.seq LIMIT ?",{id,id,parent.session_id,after,static_cast<std::int64_t>(count)}).rows)result.push_back({integer(row[0]),text(row[1]),text(row[2]),text(row[3])});return result;
}
DelegationTaskRecord Repository::settle_delegation_child(const std::string& id){
    auto& db=impl_->database;Transaction transaction(db);const auto rows=db.execute("SELECT batch_id FROM delegation_tasks WHERE child_run_id=?",{id}).rows;
    if(rows.empty())throw NotFound("Delegated child not found");const auto batch=delegation_batch(text(rows[0][0]));
    auto found=std::find_if(batch.tasks.begin(),batch.tasks.end(),[&](const auto& task){return task.run.id==id;});if(found==batch.tasks.end())throw DatabaseError("Delegation ownership differs");auto task=*found;
    if(!task.outcome_json.empty()){transaction.commit();return task;}
    if(batch.state!="accepted"&&batch.state!="working")throw Conflict("Retired delegation cannot acquire another outcome");
    const auto current=run(id);if(!terminal(current.state))throw Conflict("Delegated child has not retired");
    Json output={{"child_run_id",id},{"task_id",task.task_id},{"child_state",state_name(current.state)},{"preset_id",task.preset_id},{"preset_revision",task.preset_revision}};
    const auto history_ref="/v1/runs/"+batch.parent_run_id+"/children/"+id+"/history";
    if(current.state==RunState::completed){
        const auto history=run_history(id);if(history.empty()||history.back().role!="assistant")throw DatabaseError("Completed delegated child has no assistant output");
        const auto answer=Json::parse(history.back().json);if(!answer.contains("content")||!answer["content"].is_string())throw DatabaseError("Delegated child has invalid result text");
        for(const auto* name:{"content","model","usage","elapsed_ms","first_token_ms"})if(answer.contains(name))output[name]=answer[name];
    }else {
        const auto failure=db.execute("SELECT payload FROM events WHERE run_id=? AND kind=? ORDER BY seq DESC LIMIT 1",{id,"run."+state_name(current.state)}).rows;
        output["error"]=failure.empty()?Json{{"code","child_outcome_unavailable"}}:Json::parse(text(failure[0][0]));
    }
    if(output.dump().size()>32768){output={{"child_run_id",id},{"task_id",task.task_id},{"child_state",state_name(current.state)},{"preset_id",task.preset_id},{"preset_revision",task.preset_revision},{"history_ref",history_ref},{"error",{{"code","result_limit_exceeded"}}}};}
    task.outcome_json=output.dump();const auto event=impl_->event(id,"delegation.child.settled",task.outcome_json);task.settled_event_seq=event.sequence;
    changed_one(db.execute("UPDATE delegation_tasks SET outcome_json=?,settled_event_seq=? WHERE child_run_id=? AND outcome_json IS NULL AND settled_event_seq IS NULL",{task.outcome_json,event.sequence,id}));transaction.commit();return task;
}
DelegationBatchRecord Repository::settle_delegation_batch(const std::string& id){
    auto& db=impl_->database;Transaction transaction(db);auto batch=delegation_batch(id);
    if(!batch.result_json.empty()){transaction.commit();return batch;}
    if(batch.state!="accepted"&&batch.state!="working")throw Conflict("Interrupted delegation cannot be adopted");
    auto output=Json{{"batch_id",id},{"source","native_delegation"},{"children",Json::array()}};bool failed=false,cancelled=false;
    for(const auto& task:batch.tasks){
        if(!terminal(task.run.state)||task.outcome_json.empty()||!task.settled_event_seq)throw Conflict("Delegation still owns unfinished outcomes");
        const auto child=Json::parse(task.outcome_json);output["children"].push_back(child);failed|=task.run.state==RunState::failed||child.contains("error");cancelled|=task.run.state==RunState::cancelled;
    }
    if(output.dump().size()>65536){
        output={{"batch_id",id},{"source","native_delegation"},{"error",{{"code","result_limit_exceeded"}}},{"children",Json::array()}};failed=true;
        for(const auto& task:batch.tasks)output["children"].push_back({{"child_run_id",task.run.id},{"task_id",task.task_id},{"child_state",state_name(task.run.state)},{"preset_id",task.preset_id},{"preset_revision",task.preset_revision},{"history_ref","/v1/runs/"+batch.parent_run_id+"/children/"+task.run.id+"/history"}});
    }
    batch.result_json=output.dump();if(batch.result_json.size()>65536)throw DatabaseError("Bounded delegation identity envelope exceeds its limit");batch.state=cancelled?"cancelled":failed?"failed":"completed";
    changed_one(db.execute("UPDATE delegation_batches SET state=?,result_json=? WHERE id=? AND state IN ('accepted','working') AND result_json IS NULL",{batch.state,batch.result_json,id}));
    impl_->event(batch.parent_run_id,"delegation.batch.settled",Json{{"batch_id",id},{"state",batch.state},{"result",output}}.dump());transaction.commit();return batch;
}
void Repository::append_user_message(const std::string& session_id,const std::string& json) {
    auto& db=impl_->database;Transaction transaction(db);session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND state IN ('queued','running','paused')",{session_id}).rows.empty()) throw Conflict("Session has an active root run");
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,json}));transaction.commit();
}
Run Repository::start_graph_run(const std::string& id,const std::string& session_id,const std::string& graph_id,std::int64_t revision,const GraphPlan& plan,const std::string& prompt){
    identifier(id);if(graph_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos || graph_id.empty())throw std::invalid_argument("Invalid graph identity");if(graph_id.size()>64 || revision<1 || revision>9007199254740991)throw std::invalid_argument("Invalid graph identity or revision");object_json(prompt);
    const auto provider=prompt_context(prompt);auto& db=impl_->database;Transaction transaction(db);session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE id=?",{id}).rows.empty())throw Conflict("Run already exists");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty())throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO graph_roots(run_id,graph_id,graph_revision,specification,checkpoint,input) VALUES(?,?,?,?,?,?)",{id,graph_id,revision,plan.json(),GraphCoordinator(plan).checkpoint(),prompt}));
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,prompt}));
    changed_one(db.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));impl_->event(id,"run.queued",Json{{"kind","graph"},{"graph_id",graph_id},{"graph_revision",revision}}.dump());transaction.commit();return {id,session_id,RunState::queued,{},{},true,provider};
}
GraphRootRecord Repository::graph_run(const std::string& id){
    const auto current=run(id);if(!current.graph_root)throw Conflict("Run is not a graph root");const auto row=impl_->database.execute("SELECT graph_id,graph_revision,specification,checkpoint_revision,checkpoint,COALESCE(input,'') FROM graph_roots WHERE run_id=?",{id}).rows.at(0);return {current,text(row[0]),integer(row[1]),text(row[2]),integer(row[3]),text(row[4]),text(row[5])};
}
Run Repository::start_graph_child(const std::string& id,const std::string& parent_id,const std::string& node_id,const std::string& prompt,std::int64_t expected){
    identifier(id);if(node_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos || node_id.empty())throw std::invalid_argument("Invalid graph node identity");if(node_id.size()>64)throw std::invalid_argument("Graph node ID exceeds limits");object_json(prompt);const auto provider=prompt_context(prompt);auto& db=impl_->database;Transaction transaction(db);auto parent=graph_run(parent_id);
    if(parent.run.state!=RunState::running)throw Conflict("Graph parent is not running");
    const auto specification=Json::parse(parent.specification_json);bool executable=false;for(const auto& node:specification.at("nodes"))if(node.at("id")==node_id && (node.at("type")=="agent" || node.at("type")=="tool"))executable=true;if(!executable)throw std::invalid_argument("Child must name a declared executable node");
    if(!db.execute("SELECT id FROM runs WHERE id=? OR (parent_run_id=? AND node_id=?)",{id,parent_id,node_id}).rows.empty())throw Conflict("Graph child identity already exists");
    GraphCoordinator coordinator(GraphPlan(parent.specification_json),parent.checkpoint_json,GraphRestoreMode::live);coordinator.start(node_id);impl_->graph_checkpoint(parent,coordinator,expected);
    changed_one(db.execute("INSERT INTO runs(id,session_id,state,parent_run_id,node_id) VALUES(?,?,'queued',?,?)",{id,parent.run.session_id,parent_id,node_id}));
    const Run result{id,parent.run.session_id,RunState::queued,parent_id,node_id,false,provider};impl_->message(result,"user",prompt);
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));impl_->event(id,"run.queued",Json{{"parent_run_id",parent_id},{"node_id",node_id}}.dump());impl_->event(parent_id,"graph.child.queued",Json{{"child_run_id",id},{"node_id",node_id}}.dump());transaction.commit();return result;
}
GraphRootRecord Repository::settle_graph_child(const std::string& id,std::int64_t expected){
    auto& db=impl_->database;Transaction transaction(db);const auto child=run(id);if(child.parent_id.empty() || !terminal(child.state))throw Conflict("Graph child has no observed terminal outcome");auto root=graph_run(child.parent_id);if(root.run.state!=RunState::running)throw Conflict("Graph root is not running");
    if(expected<0 || (expected>0 && root.checkpoint_revision!=expected))throw Conflict("Graph checkpoint revision changed");GraphCoordinator coordinator(GraphPlan(root.specification_json),root.checkpoint_json,GraphRestoreMode::live);
    const auto state=coordinator.state(child.node_id);if(state==GraphNodeState::completed || state==GraphNodeState::failed || state==GraphNodeState::uncertain){transaction.commit();return root;}
    if(child.state==RunState::completed){const auto history=run_history(child.id);if(history.empty() || history.back().role!="assistant")throw DatabaseError("Completed child has no assistant output");try{auto output=Json::parse(history.back().json);output.erase("provider_items");output.erase("provider_context");coordinator.complete(child.node_id,output.dump());}catch(const std::invalid_argument&){coordinator.fail(child.node_id,false);}}
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
    identifier(kind); if(kind.rfind("run.",0)==0 || kind.rfind("operation.",0)==0 || kind.rfind("delegation.",0)==0 || kind.rfind("budget.",0)==0) throw std::invalid_argument("Lifecycle events require their owning repository operation");
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
void Repository::compare_information(const std::string& category,const std::string& id,const std::string& json,const std::optional<std::string>& expected) {
    identifier(category);identifier(id);
    if(category=="secrets"||category=="credentials")throw std::invalid_argument("Use the encrypted credential repository");
    Transaction transaction(impl_->database);
    const auto result=expected
        ?impl_->database.execute("UPDATE information SET payload=? WHERE category=? AND id=? AND payload=?",{json,category,id,*expected})
        :impl_->database.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{category,id,json});
    if(result.affected_rows!=1)throw Conflict("Information changed before publication");
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
    for(const auto& row:db.execute("SELECT id,parent_run_id FROM delegation_batches WHERE state IN ('accepted','working')").rows){
        changed_one(db.execute("UPDATE delegation_batches SET state='interrupted' WHERE id=? AND state IN ('accepted','working')",{text(row[0])}));
        impl_->event(text(row[1]),"delegation.batch.interrupted",Json{{"batch_id",text(row[0])},{"reason","server_restart"},{"children_replayed",false}}.dump());
    }
    db.execute("UPDATE agent_model_call_reservations SET state='interrupted' WHERE state IN ('reserved','started')");
    for(const auto& row:db.execute("SELECT run_id FROM graph_roots").rows){auto root=graph_run(text(row[0]));const GraphCoordinator recovered(GraphPlan(root.specification_json),root.checkpoint_json);if(recovered.checkpoint()!=root.checkpoint_json){impl_->graph_checkpoint(root,recovered);impl_->event(root.run.id,"graph.recovered",Json{{"checkpoint_revision",root.checkpoint_revision},{"reason","interrupted_nodes_not_replayed"}}.dump());}}
    transaction.commit(); return roots.size();
}
}
