#include "agentflow/repository.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/graph.hpp"
#include "agentflow/dynamic_plan.hpp"
#include <algorithm>
#include <limits>
#include <chrono>
#include <set>
#include "nlohmann/json.hpp"

namespace agentflow {
namespace {
using Json=nlohmann::json;
std::int64_t now_ms() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string object_json(const std::string& source,std::size_t limit=2*1024*1024) {
    if(source.size()>limit) throw std::invalid_argument("Operation JSON exceeds its limit");
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
    if((spec.policy_id!="native.delegation" && spec.policy_id!="native.dynamic-plan") || spec.policy_revision!=1 || spec.workspace_identity.empty() || spec.workspace_identity.size()>4096 || spec.workspace_identity.find('\0')!=std::string::npos ||
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
std::string dynamic_effect_state(const std::vector<Operation>& operations){
    std::string effect="none";
    for(const auto& op:operations){
        if(op.state==OperationState::awaiting_approval||op.state==OperationState::ready||op.state==OperationState::executing)throw Conflict("Dynamic child still owns an unfinished effect");
        if(op.state==OperationState::uncertain)effect="uncertain";
        else if(effect!="uncertain"){
            if(op.state==OperationState::failed)effect="failed";
            else if(effect!="failed"){
                if(op.state==OperationState::succeeded)effect="succeeded";
                else if(effect!="succeeded")effect="cancelled";
            }
        }
    }
    return effect;
}
void private_identity(const std::string& value){if(value.size()!=64||value.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("Invalid private capability identity");}
Json dynamic_definition_json(const DynamicNodeDefinition& n){
    Json result={{"id",n.label},{"type",n.kind==DynamicNodeKind::agent?"agent":"human"},{"objective",n.objective},{"question",n.question},{"preset",n.preset_id},{"preset_revision",n.preset_revision},{"depends_on",Json::array()}};
    for(const auto& d:n.dependencies)result["depends_on"].push_back({{"task",d.task},{"require",d.require==DynamicDependencyRequirement::success?"success":"observed"}});return result;
}
DynamicNodeDefinition dynamic_definition(const std::string& source){
    const auto j=Json::parse(object_json(source));DynamicNodeDefinition n;n.label=j.at("id");n.kind=j.at("type")=="agent"?DynamicNodeKind::agent:DynamicNodeKind::human;
    n.objective=j.at("objective");n.question=j.at("question");n.preset_id=j.at("preset");n.preset_revision=j.at("preset_revision");
    for(const auto& d:j.at("depends_on"))n.dependencies.push_back({d.at("task"),d.at("require")=="success"?DynamicDependencyRequirement::success:DynamicDependencyRequirement::observed});return n;
}
const char* dynamic_state_name(DynamicNodeState s){switch(s){case DynamicNodeState::pending:return "pending";case DynamicNodeState::blocked:return "blocked";case DynamicNodeState::claimed:return "claimed";case DynamicNodeState::waiting_human:return "waiting_human";case DynamicNodeState::settled:return "settled";case DynamicNodeState::skipped:return "skipped";case DynamicNodeState::cancelled:return "cancelled";case DynamicNodeState::uncertain:return "uncertain";}throw DatabaseError("Invalid dynamic state");}
DynamicNodeState dynamic_state(const std::string& s){for(auto v:{DynamicNodeState::pending,DynamicNodeState::blocked,DynamicNodeState::claimed,DynamicNodeState::waiting_human,DynamicNodeState::settled,DynamicNodeState::skipped,DynamicNodeState::cancelled,DynamicNodeState::uncertain})if(s==dynamic_state_name(v))return v;throw DatabaseError("Invalid saved dynamic state");}
Json dynamic_caps_json(const DynamicPlanCapabilities& c){
    Json j={{"revision",c.revision},{"backend_identity",c.backend_identity},{"workspace_identity",c.workspace_identity},{"provider_identity_json",c.provider_identity_json},{"max_nodes",c.max_nodes},{"max_revisions",c.max_revisions},{"max_humans",c.max_humans},{"change_bytes",c.change_bytes},{"human_expiry_ms",c.human_expiry_ms},{"max_parent_turns",c.max_parent_turns},{"catalogue_finalized",c.catalogue_finalized},{"tool_catalog_json",c.tool_catalog_json},{"presets",Json::array()}};
    for(const auto& p:c.presets)j["presets"].push_back({{"id",p.id},{"revision",p.revision},{"readonly",p.readonly},{"tools",p.tools},{"turn_limit",p.turn_limit},{"backend_identity",p.backend_identity}});return j;
}
DynamicPlanCapabilities dynamic_caps(const std::string& source){
    const auto j=Json::parse(object_json(source));DynamicPlanCapabilities c;c.revision=j.at("revision");c.backend_identity=j.at("backend_identity");c.workspace_identity=j.at("workspace_identity");c.provider_identity_json=j.at("provider_identity_json");
    c.max_nodes=j.at("max_nodes");c.max_revisions=j.at("max_revisions");c.max_humans=j.at("max_humans");c.change_bytes=j.at("change_bytes");c.human_expiry_ms=j.at("human_expiry_ms");c.max_parent_turns=j.at("max_parent_turns");c.catalogue_finalized=j.at("catalogue_finalized");c.tool_catalog_json=j.at("tool_catalog_json");
    for(const auto& p:j.at("presets"))c.presets.push_back({p.at("id"),p.at("revision"),p.at("readonly"),p.at("tools").get<std::vector<std::string>>(),p.at("turn_limit"),p.at("backend_identity")});return c;
}
void validate_dynamic_caps(const DynamicPlanCapabilities& c,const RootBudgetSpec& b,bool finalized){
    private_identity(c.backend_identity);if(c.revision!=1||c.catalogue_finalized!=finalized||c.workspace_identity!=b.workspace_identity||Json::parse(object_json(c.provider_identity_json))!=budget_provider_identity(b)||b.policy_id!="native.dynamic-plan"||
        c.max_nodes<1||c.max_nodes>32||c.max_revisions<1||c.max_revisions>16||c.max_humans<1||c.max_humans>8||c.change_bytes<1||c.change_bytes>262144||c.human_expiry_ms<1||c.human_expiry_ms>3600000||c.max_parent_turns<1||c.max_parent_turns>16||c.presets.empty()||c.presets.size()>2)throw std::invalid_argument("Invalid dynamic capability admission");
    (void)object_json(c.tool_catalog_json,262144);std::set<std::string> ids;
    for(const auto& p:c.presets){if(!ids.insert(p.id).second||(p.id!="workspace.inspect"&&p.id!="workspace.coding")||p.revision!=1||p.readonly!=(p.id=="workspace.inspect")||p.turn_limit<1||p.turn_limit>(p.readonly?4:16))throw std::invalid_argument("Invalid dynamic registered preset");
        if(finalized)private_identity(p.backend_identity);else if(!p.backend_identity.empty())private_identity(p.backend_identity);
        if(p.tools.size()>128||(finalized&&p.tools.empty()))throw std::invalid_argument("Invalid dynamic preset tools");std::set<std::string> tools;
        for(const auto& t:p.tools){bounded_identity(t);if(!tools.insert(t).second||t=="delegate_tasks"||t=="plan_tasks"||t=="revise_plan"||t=="inspect_plan"||(p.readonly&&t!="read_file"&&t!="list_files"&&t!="search_files"&&t!="read_repository_instructions"))throw std::invalid_argument("Invalid bounded child tool authority");}
    }
}
const DynamicPresetCapability& dynamic_preset(const DynamicPlanCapabilities& c,const DynamicNodeDefinition& d){const auto p=std::find_if(c.presets.begin(),c.presets.end(),[&](const auto& v){return v.id==d.preset_id&&v.revision==d.preset_revision;});if(p==c.presets.end())throw DynamicPlanUnavailable("Dynamic preset is unavailable");return *p;}
void dynamic_effect_authority(const OperationSpec& spec,const DynamicPlanRecord& plan,const DynamicNodeRecord& node,const DynamicPresetCapability& preset){
    if(plan.state!="active"||!plan.capabilities.catalogue_finalized||node.state!=DynamicNodeState::claimed||node.definition.kind!=DynamicNodeKind::agent||node.claim_revision!=node.definition_revision||node.claim_id.empty()||preset.readonly||spec.workspace!=plan.capabilities.workspace_identity)throw Conflict("Dynamic child effect is outside its immutable workspace and claim");
    // Effect journals use backend adapter identities, not model tool names.
    // Only these native mappings exist; a model/preset cannot define another.
    std::string model_tool;
    if(spec.tool=="replace_file")model_tool="edit_file";
    else if(spec.tool=="create_file"||spec.tool=="run_process")model_tool=spec.tool;
    else if(spec.tool=="mcp_tool"){
        const auto proposal=Json::parse(spec.arguments_json);
        if(!proposal.contains("alias")||!proposal["alias"].is_string())throw Conflict("Dynamic MCP proposal lacks its captured alias");
        model_tool=proposal["alias"].get<std::string>();
        if(model_tool.size()!=52||!model_tool.starts_with("mcp_")||model_tool.substr(4).find_first_not_of("0123456789abcdef")!=std::string::npos)throw Conflict("Dynamic MCP alias is invalid");
    }else throw Conflict("Dynamic child effect adapter is not registered");
    if(std::find(preset.tools.begin(),preset.tools.end(),model_tool)==preset.tools.end())throw Conflict("Dynamic child effect tool is outside its immutable preset");
    const auto catalogue=Json::parse(plan.capabilities.tool_catalog_json);
    if(!catalogue.contains("tools")||!catalogue["tools"].is_array())throw Conflict("Dynamic child effect catalogue is unavailable");
    const Json* captured=nullptr;
    for(const auto& tool:catalogue["tools"])if(tool.is_object()&&tool.contains("name")&&tool["name"].is_string()&&tool["name"]==model_tool){if(captured)throw Conflict("Dynamic child effect catalogue is ambiguous");captured=&tool;}
    if(!captured)throw Conflict("Dynamic child effect lacks its sealed catalogue entry");
    if(spec.tool!="mcp_tool")return;
    if(!captured->contains("mcp_binding")||!(*captured)["mcp_binding"].is_object())throw Conflict("Dynamic MCP tool lacks its native registry binding");
    const auto& binding=(*captured)["mcp_binding"];const auto proposal=Json::parse(spec.arguments_json);
    if(binding.size()!=9||proposal.size()!=10)throw Conflict("Dynamic MCP registry binding has unexpected fields");
    for(const auto* field:{"server_config_id","config_revision","peer_tool","alias","catalogue_fingerprint","protocol_version","input_schema_json","output_schema_json","annotations_json"})if(!binding.contains(field)||!proposal.contains(field)||binding[field].type()!=proposal[field].type()||binding[field]!=proposal[field])throw Conflict("Dynamic MCP proposal differs from its captured native registry binding");
    for(const auto* field:{"server_config_id","peer_tool","protocol_version","input_schema_json","annotations_json"})if(!binding[field].is_string()||binding[field].get_ref<const std::string&>().empty()||binding[field].get_ref<const std::string&>().find('\0')!=std::string::npos)throw Conflict("Dynamic MCP registry metadata is invalid");
    if(binding["server_config_id"].get_ref<const std::string&>().size()>128||binding["peer_tool"].get_ref<const std::string&>().size()>256||binding["protocol_version"].get_ref<const std::string&>().size()>64||!binding["config_revision"].is_number_integer()||binding["config_revision"]<1||binding["config_revision"]>9007199254740991||!binding["catalogue_fingerprint"].is_string())throw Conflict("Dynamic MCP registry identity is invalid");
    const auto fingerprint=binding["catalogue_fingerprint"].get<std::string>();
    if(fingerprint.size()!=64||fingerprint.find_first_not_of("0123456789abcdef")!=std::string::npos||model_tool!="mcp_"+fingerprint.substr(0,48)||binding["alias"]!=model_tool)throw Conflict("Dynamic MCP registry fingerprint differs from its alias");
    if(!captured->contains("input_schema_json")||!(*captured)["input_schema_json"].is_string()||(*captured)["input_schema_json"]!=binding["input_schema_json"]||!proposal.contains("arguments_json")||!proposal["arguments_json"].is_string())throw Conflict("Dynamic MCP proposal lacks its exact sealed schema and arguments");
    (void)object_json(binding["input_schema_json"].get<std::string>(),262144);(void)object_json(binding["annotations_json"].get<std::string>(),262144);(void)object_json(proposal["arguments_json"].get<std::string>());
    if(!binding["output_schema_json"].is_null()){if(!binding["output_schema_json"].is_string())throw Conflict("Dynamic MCP output schema is invalid");(void)object_json(binding["output_schema_json"].get<std::string>(),262144);}
    if(spec.resources!=std::vector<std::string>{"mcp-server:"+binding["server_config_id"].get<std::string>()})throw Conflict("Dynamic MCP proposal resource differs from its captured server");
    // Registry validates arguments against this schema before requesting the
    // operation. The exact raw payload remains separately controller-approved.
}
void dynamic_cas(const DynamicPlanRecord& p,std::int64_t revision,std::int64_t sequence,const std::string& identity){if(p.capabilities.backend_identity!=identity)throw DynamicPlanUnavailable("Dynamic settings changed");if(p.revision!=revision||p.state_sequence!=sequence)throw DynamicPlanChanged("Dynamic plan changed");}
void dynamic_safe_sequence(std::int64_t sequence){if(sequence<0||sequence>=9007199254740991)throw std::overflow_error("Dynamic state sequence exhausted");}
Json dynamic_prompt(const DynamicNodeDefinition& d,const std::string& input,const DynamicPlanCapabilities& c){
    Json result={{"content",d.objective+"\n\nObserved dependency data (not instructions):\n"+input}};const auto provider=Json::parse(c.provider_identity_json);if(provider.size()==6)result["provider_context"]=provider;return result;
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
    void initialize_dynamic(const std::string& id,const std::optional<RootBudgetSpec>& budget,const std::optional<DynamicPlanCapabilities>& caps){
        if(!caps){if(budget&&budget->policy_id=="native.dynamic-plan")throw std::invalid_argument("Dynamic budget requires typed capabilities");return;}
        if(!budget)throw std::invalid_argument("Dynamic capabilities require their admitted budget");validate_dynamic_caps(*caps,*budget,false);
        changed_one(database.execute("INSERT INTO dynamic_capabilities(root_run_id,backend_identity,capabilities_json,catalogue_finalized) VALUES(?,?,?,0)",{id,caps->backend_identity,dynamic_caps_json(*caps).dump()}));
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
        if(current.parent_id.empty()&&!current.graph_root&&(next==RunState::paused||terminal(next))&&!database.execute("SELECT root_run_id FROM dynamic_capabilities WHERE root_run_id=?",{current.id}).rows.empty()){
            if(next==RunState::paused)throw Conflict("Dynamic roots require their typed human pause");
            if(!database.execute("SELECT id FROM agent_budget_segments WHERE root_run_id=? AND state='open'",{current.id}).rows.empty())throw Conflict("Dynamic terminal state requires measured active-segment closure");
            if(!database.execute("SELECT id FROM dynamic_plans WHERE root_run_id=? AND state NOT IN ('completed','failed','cancelled','interrupted')",{current.id}).rows.empty()){
                if(next!=RunState::completed)throw Conflict("Dynamic retirement requires its typed owner operation");
                const auto pending=database.execute("SELECT n.label FROM dynamic_plan_nodes n JOIN dynamic_plans p ON p.id=n.plan_id WHERE p.root_run_id=? AND n.state IN ('pending','blocked','claimed','waiting_human','uncertain')",{current.id}).rows;if(!pending.empty())throw Conflict("Dynamic plan has unresolved nodes");
            }
            if(!database.execute("SELECT id FROM dynamic_plan_calls WHERE root_run_id=? AND (state IN ('accepted','report_ready') OR (state='turn_committed' AND continuation_attempt_id IS NULL))",{current.id}).rows.empty())throw Conflict("Dynamic root has uncommitted signed planning work");
        }
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
            const auto dynamic=database.execute("SELECT p.state,n.state FROM dynamic_plan_nodes n JOIN dynamic_plans p ON p.id=n.plan_id WHERE n.child_run_id=? AND p.root_run_id=? AND n.backend_node_id=?",{current.id,current.parent_id,current.node_id}).rows;
            if(!dynamic.empty()&&(text(dynamic[0][0])!="active"||text(dynamic[0][1])!="claimed"))throw Conflict("Dynamic child admission is already retired");
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
    DynamicBudgetSegment close_dynamic_segment(const std::string& root,const std::string& id,std::int64_t elapsed){
        if(elapsed<0||elapsed>9007199254740991)throw std::invalid_argument("Invalid measured active elapsed time");
        const auto rows=database.execute("SELECT id,ordinal,state,remaining_active_ms,opened_event_seq FROM agent_budget_segments WHERE root_run_id=? ORDER BY ordinal DESC LIMIT 1",{root}).rows;if(rows.empty())throw NotFound("Active dynamic segment not found");const auto& r=rows[0];
        if(text(r[0])!=id||text(r[2])!="open")throw Conflict("Active dynamic segment changed");
        const auto remaining=std::max<std::int64_t>(0,integer(r[3])-elapsed);const auto e=event(root,"budget.segment.closed",Json{{"segment_id",id},{"active_elapsed_ms",elapsed},{"remaining_active_ms",remaining}}.dump());
        changed_one(database.execute("UPDATE agent_budget_segments SET state='closed',active_elapsed_ms=?,remaining_active_ms=?,closed_event_seq=? WHERE root_run_id=? AND id=? AND state='open'",{elapsed,remaining,e.sequence,root,id}));return {root,id,"closed",integer(r[1]),elapsed,remaining,integer(r[4]),e.sequence};
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
    } else if(version<1 || version>11) throw DatabaseError("Unsupported target repository version");
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
    if(version<11) {
        db.execute("ALTER TABLE agent_model_call_reservations ADD COLUMN actual_assistant_json TEXT CHECK(actual_assistant_json IS NULL OR (json_valid(actual_assistant_json) AND json_type(actual_assistant_json)='object'))");
        db.execute("CREATE TRIGGER actual_model_response_once BEFORE UPDATE OF actual_assistant_json ON agent_model_call_reservations WHEN OLD.actual_assistant_json IS NOT NULL OR OLD.state!='started' OR NEW.state!='finished' BEGIN SELECT RAISE(ABORT,'actual model response requires once-only started completion'); END");
        db.execute("ALTER TABLE agent_execution_budgets ADD COLUMN planned_children_reserved INTEGER NOT NULL DEFAULT 0 CHECK(planned_children_reserved>=0 AND planned_children_reserved+children_admitted<=max_children)");
        db.execute("CREATE TABLE dynamic_capabilities(root_run_id TEXT PRIMARY KEY NOT NULL REFERENCES agent_execution_budgets(root_run_id),backend_identity TEXT NOT NULL CHECK(length(backend_identity)=64),capabilities_json TEXT NOT NULL CHECK(json_valid(capabilities_json) AND json_type(capabilities_json)='object'),catalogue_finalized INTEGER NOT NULL CHECK(catalogue_finalized IN (0,1)))");
        db.execute("CREATE TABLE dynamic_plans(id TEXT PRIMARY KEY NOT NULL,root_run_id TEXT NOT NULL UNIQUE REFERENCES dynamic_capabilities(root_run_id),revision INTEGER NOT NULL CHECK(revision>0),state_sequence INTEGER NOT NULL CHECK(state_sequence>0),state TEXT NOT NULL CHECK(state IN ('active','waiting_human','completed','failed','cancelled','uncertain','interrupted')),retired_labels_json TEXT NOT NULL CHECK(json_valid(retired_labels_json) AND json_type(retired_labels_json)='array'),planned_children_reserved INTEGER NOT NULL CHECK(planned_children_reserved>=0),planned_humans_reserved INTEGER NOT NULL CHECK(planned_humans_reserved>=0),humans_published INTEGER NOT NULL DEFAULT 0 CHECK(humans_published>=0))");
        db.execute("CREATE TABLE dynamic_plan_calls(id TEXT PRIMARY KEY NOT NULL,plan_id TEXT NOT NULL REFERENCES dynamic_plans(id),root_run_id TEXT NOT NULL REFERENCES dynamic_capabilities(root_run_id),provider_tool_call_id TEXT NOT NULL,origin_attempt_id TEXT NOT NULL,arguments_json TEXT NOT NULL CHECK(json_valid(arguments_json) AND json_type(arguments_json)='object'),parent_assistant_json TEXT NOT NULL CHECK(json_valid(parent_assistant_json) AND json_type(parent_assistant_json)='object'),state TEXT NOT NULL CHECK(state IN ('accepted','report_ready','turn_committed','interrupted','cancelled')),accepted_revision INTEGER NOT NULL,accepted_event_seq INTEGER NOT NULL REFERENCES events(seq),result_json TEXT CHECK(result_json IS NULL OR (json_valid(result_json) AND json_type(result_json)='object')),result_event_seq INTEGER REFERENCES events(seq),conversation_commit_seq INTEGER REFERENCES events(seq),continuation_attempt_id TEXT,UNIQUE(root_run_id,provider_tool_call_id),UNIQUE(root_run_id,continuation_attempt_id),FOREIGN KEY(root_run_id,origin_attempt_id) REFERENCES agent_model_call_reservations(root_run_id,attempt_id),FOREIGN KEY(root_run_id,continuation_attempt_id) REFERENCES agent_model_call_reservations(root_run_id,attempt_id),CHECK((result_json IS NULL)=(result_event_seq IS NULL)),CHECK(conversation_commit_seq IS NULL OR result_event_seq IS NOT NULL))");
        db.execute("CREATE UNIQUE INDEX dynamic_one_pending_call ON dynamic_plan_calls(root_run_id) WHERE state IN ('accepted','report_ready')");
        db.execute("CREATE TABLE dynamic_plan_revisions(plan_id TEXT NOT NULL REFERENCES dynamic_plans(id),revision INTEGER NOT NULL,canonical_spec_json TEXT NOT NULL CHECK(json_valid(canonical_spec_json) AND json_type(canonical_spec_json)='object'),plan_call_id TEXT NOT NULL UNIQUE REFERENCES dynamic_plan_calls(id),accepted_event_seq INTEGER NOT NULL REFERENCES events(seq),PRIMARY KEY(plan_id,revision))");
        db.execute("CREATE TABLE dynamic_plan_nodes(plan_id TEXT NOT NULL REFERENCES dynamic_plans(id),label TEXT NOT NULL,backend_node_id TEXT NOT NULL UNIQUE,definition_json TEXT NOT NULL CHECK(json_valid(definition_json) AND json_type(definition_json)='object'),definition_revision INTEGER NOT NULL CHECK(definition_revision>0),state TEXT NOT NULL CHECK(state IN ('pending','blocked','claimed','waiting_human','settled','skipped','cancelled','uncertain')),claim_revision INTEGER NOT NULL DEFAULT 0,claim_id TEXT UNIQUE,child_run_id TEXT UNIQUE REFERENCES runs(id) DEFERRABLE INITIALLY DEFERRED,human_request_id TEXT UNIQUE,child_state TEXT,outcome_json TEXT CHECK(outcome_json IS NULL OR (json_valid(outcome_json) AND json_type(outcome_json)='object')),settled_event_seq INTEGER REFERENCES events(seq),resolved_input_json TEXT CHECK(resolved_input_json IS NULL OR (json_valid(resolved_input_json) AND json_type(resolved_input_json)='object')),effect_state TEXT NOT NULL DEFAULT 'none',protected_definition INTEGER NOT NULL DEFAULT 0 CHECK(protected_definition IN (0,1)),PRIMARY KEY(plan_id,label),CHECK((outcome_json IS NULL)=(settled_event_seq IS NULL)),CHECK(child_run_id IS NULL OR human_request_id IS NULL),CHECK(claim_revision=0 OR claim_revision=definition_revision))");
        db.execute("CREATE TABLE dynamic_revision_nodes(plan_id TEXT NOT NULL,revision INTEGER NOT NULL,label TEXT NOT NULL,backend_node_id TEXT NOT NULL,definition_json TEXT NOT NULL CHECK(json_valid(definition_json)),definition_revision INTEGER NOT NULL,PRIMARY KEY(plan_id,revision,label),FOREIGN KEY(plan_id,revision) REFERENCES dynamic_plan_revisions(plan_id,revision))");
        db.execute("CREATE TABLE dynamic_plan_edges(plan_id TEXT NOT NULL,revision INTEGER NOT NULL,label TEXT NOT NULL,dependency_label TEXT NOT NULL,requirement TEXT NOT NULL CHECK(requirement IN ('success','observed')),PRIMARY KEY(plan_id,revision,label,dependency_label),FOREIGN KEY(plan_id,revision,label) REFERENCES dynamic_revision_nodes(plan_id,revision,label),FOREIGN KEY(plan_id,revision,dependency_label) REFERENCES dynamic_revision_nodes(plan_id,revision,label))");
        db.execute("CREATE TABLE dynamic_human_requests(id TEXT PRIMARY KEY NOT NULL,plan_id TEXT NOT NULL REFERENCES dynamic_plans(id),root_run_id TEXT NOT NULL REFERENCES dynamic_capabilities(root_run_id),label TEXT NOT NULL,backend_node_id TEXT NOT NULL,definition_revision INTEGER NOT NULL,plan_call_id TEXT NOT NULL REFERENCES dynamic_plan_calls(id),question TEXT NOT NULL,state TEXT NOT NULL CHECK(state IN ('waiting','answered','expired','cancelled')),expires_ms INTEGER NOT NULL,published_event_seq INTEGER NOT NULL REFERENCES events(seq),input_json TEXT CHECK(input_json IS NULL OR json_valid(input_json)),actor TEXT,input_event_seq INTEGER REFERENCES events(seq),UNIQUE(plan_id,label),FOREIGN KEY(plan_id,label) REFERENCES dynamic_plan_nodes(plan_id,label),CHECK((input_event_seq IS NULL)=(state='waiting')),CHECK(state!='answered' OR (input_json IS NOT NULL AND actor IS NOT NULL)))");
        db.execute("CREATE TABLE agent_budget_segments(root_run_id TEXT NOT NULL REFERENCES dynamic_capabilities(root_run_id),id TEXT NOT NULL UNIQUE,ordinal INTEGER NOT NULL CHECK(ordinal>0),state TEXT NOT NULL CHECK(state IN ('open','closed','interrupted')),active_elapsed_ms INTEGER NOT NULL DEFAULT 0 CHECK(active_elapsed_ms>=0),remaining_active_ms INTEGER NOT NULL CHECK(remaining_active_ms>=0),opened_event_seq INTEGER NOT NULL REFERENCES events(seq),closed_event_seq INTEGER REFERENCES events(seq),PRIMARY KEY(root_run_id,ordinal),CHECK((state='open')=(closed_event_seq IS NULL)))");
        db.execute("CREATE UNIQUE INDEX one_open_budget_segment ON agent_budget_segments(root_run_id) WHERE state='open'");
        db.execute("CREATE TABLE dynamic_owner_pauses(root_run_id TEXT PRIMARY KEY NOT NULL REFERENCES dynamic_capabilities(root_run_id),plan_id TEXT NOT NULL REFERENCES dynamic_plans(id),plan_call_id TEXT NOT NULL REFERENCES dynamic_plan_calls(id),segment_id TEXT NOT NULL REFERENCES agent_budget_segments(id),paused_event_seq INTEGER NOT NULL REFERENCES events(seq),state TEXT NOT NULL CHECK(state IN ('paused','resumed','retired')))");
        db.execute("DROP TRIGGER owned_child_boundary");
        db.execute("CREATE TRIGGER owned_child_boundary BEFORE INSERT ON runs WHEN NEW.parent_run_id IS NOT NULL BEGIN SELECT CASE WHEN NEW.node_id IS NULL OR NOT (EXISTS(SELECT 1 FROM runs p JOIN graph_roots g ON g.run_id=p.id WHERE p.id=NEW.parent_run_id AND p.parent_run_id IS NULL AND p.session_id=NEW.session_id AND p.state='running') OR EXISTS(SELECT 1 FROM delegation_tasks t JOIN delegation_batches b ON b.id=t.batch_id JOIN agent_execution_budgets e ON e.root_run_id=b.root_run_id JOIN runs p ON p.id=b.parent_run_id WHERE t.child_run_id=NEW.id AND t.node_id=NEW.node_id AND b.parent_run_id=NEW.parent_run_id AND b.root_run_id=b.parent_run_id AND b.state IN ('accepted','working') AND p.parent_run_id IS NULL AND p.state='running' AND p.session_id=NEW.session_id AND NEW.state='queued' AND NOT EXISTS(SELECT 1 FROM graph_roots g WHERE g.run_id=p.id)) OR EXISTS(SELECT 1 FROM dynamic_plan_nodes n JOIN dynamic_plans d ON d.id=n.plan_id JOIN dynamic_capabilities a ON a.root_run_id=d.root_run_id JOIN runs p ON p.id=d.root_run_id WHERE n.child_run_id=NEW.id AND n.backend_node_id=NEW.node_id AND n.state='claimed' AND n.claim_revision=n.definition_revision AND n.claim_id IS NOT NULL AND json_extract(n.definition_json,'$.type')='agent' AND d.root_run_id=NEW.parent_run_id AND d.state='active' AND a.catalogue_finalized=1 AND p.parent_run_id IS NULL AND p.state='running' AND p.session_id=NEW.session_id AND NEW.state='queued' AND NOT EXISTS(SELECT 1 FROM graph_roots g WHERE g.run_id=p.id))) THEN RAISE(ABORT,'invalid owned child boundary') END; END");
        db.execute("CREATE TRIGGER dynamic_capability_identity BEFORE UPDATE ON dynamic_capabilities BEGIN SELECT CASE WHEN OLD.catalogue_finalized!=0 OR NEW.catalogue_finalized!=1 OR NEW.root_run_id!=OLD.root_run_id OR NEW.backend_identity!=OLD.backend_identity OR EXISTS(SELECT 1 FROM agent_model_call_reservations r WHERE r.root_run_id=OLD.root_run_id) OR json_extract(NEW.capabilities_json,'$.backend_identity')!=json_extract(OLD.capabilities_json,'$.backend_identity') OR json_extract(NEW.capabilities_json,'$.workspace_identity')!=json_extract(OLD.capabilities_json,'$.workspace_identity') OR json_extract(NEW.capabilities_json,'$.provider_identity_json')!=json_extract(OLD.capabilities_json,'$.provider_identity_json') OR json_extract(NEW.capabilities_json,'$.max_nodes')!=json_extract(OLD.capabilities_json,'$.max_nodes') OR json_extract(NEW.capabilities_json,'$.max_revisions')!=json_extract(OLD.capabilities_json,'$.max_revisions') OR json_extract(NEW.capabilities_json,'$.max_humans')!=json_extract(OLD.capabilities_json,'$.max_humans') OR json_extract(NEW.capabilities_json,'$.change_bytes')!=json_extract(OLD.capabilities_json,'$.change_bytes') OR json_extract(NEW.capabilities_json,'$.human_expiry_ms')!=json_extract(OLD.capabilities_json,'$.human_expiry_ms') OR json_extract(NEW.capabilities_json,'$.max_parent_turns')!=json_extract(OLD.capabilities_json,'$.max_parent_turns') THEN RAISE(ABORT,'dynamic capability is immutable') END; END");
        db.execute("CREATE TRIGGER dynamic_revision_immutable BEFORE UPDATE ON dynamic_plan_revisions BEGIN SELECT RAISE(ABORT,'dynamic revision is immutable'); END");
        db.execute("CREATE TRIGGER dynamic_revision_node_immutable BEFORE UPDATE ON dynamic_revision_nodes BEGIN SELECT RAISE(ABORT,'dynamic definition history is immutable'); END");
        db.execute("CREATE TRIGGER dynamic_edge_immutable BEFORE UPDATE ON dynamic_plan_edges BEGIN SELECT RAISE(ABORT,'dynamic dependency history is immutable'); END");
        db.execute("CREATE TRIGGER dynamic_definition_protected BEFORE UPDATE OF plan_id,label,backend_node_id,definition_json,definition_revision ON dynamic_plan_nodes WHEN OLD.plan_id!=NEW.plan_id OR OLD.label!=NEW.label OR OLD.backend_node_id!=NEW.backend_node_id OR OLD.protected_definition=1 OR OLD.state NOT IN ('pending','blocked') BEGIN SELECT RAISE(ABORT,'dynamic claimed definition is immutable'); END");
        db.execute("CREATE TRIGGER dynamic_claim_immutable BEFORE UPDATE OF claim_revision,claim_id,child_run_id,human_request_id,resolved_input_json ON dynamic_plan_nodes WHEN OLD.claim_revision!=0 BEGIN SELECT RAISE(ABORT,'dynamic claim identity is immutable'); END");
        db.execute("CREATE TRIGGER dynamic_child_settlement BEFORE UPDATE OF outcome_json,settled_event_seq ON dynamic_plan_nodes WHEN OLD.child_run_id IS NOT NULL BEGIN SELECT CASE WHEN OLD.outcome_json IS NOT NULL OR NOT EXISTS(SELECT 1 FROM events e JOIN runs c ON c.id=OLD.child_run_id JOIN dynamic_plans p ON p.id=OLD.plan_id WHERE e.seq=NEW.settled_event_seq AND e.run_id=c.id AND e.kind='plan.child.settled' AND e.payload=NEW.outcome_json AND c.parent_run_id=p.root_run_id AND c.node_id=OLD.backend_node_id AND c.state IN ('completed','failed','cancelled') AND json_extract(NEW.outcome_json,'$.child_run_id')=c.id AND json_extract(NEW.outcome_json,'$.child_state')=c.state) THEN RAISE(ABORT,'invalid dynamic child settlement') END; END");
        db.execute("CREATE TRIGGER dynamic_call_identity BEFORE UPDATE OF id,plan_id,root_run_id,provider_tool_call_id,origin_attempt_id,arguments_json,parent_assistant_json,accepted_revision,accepted_event_seq ON dynamic_plan_calls BEGIN SELECT RAISE(ABORT,'signed dynamic call is immutable'); END");
        db.execute("CREATE TRIGGER dynamic_call_result BEFORE UPDATE OF result_json,result_event_seq ON dynamic_plan_calls BEGIN SELECT CASE WHEN OLD.result_json IS NOT NULL OR NOT EXISTS(SELECT 1 FROM events e WHERE e.seq=NEW.result_event_seq AND e.run_id=OLD.root_run_id AND e.kind='plan.call.reported' AND e.payload=NEW.result_json) THEN RAISE(ABORT,'dynamic call result must be owned and write once') END; END");
        db.execute("CREATE TRIGGER dynamic_call_commit BEFORE UPDATE OF conversation_commit_seq ON dynamic_plan_calls BEGIN SELECT CASE WHEN OLD.conversation_commit_seq IS NOT NULL OR OLD.result_json IS NULL OR NOT EXISTS(SELECT 1 FROM events e WHERE e.seq=NEW.conversation_commit_seq AND e.run_id=OLD.root_run_id AND e.kind='conversation.tool_turn' AND json_extract(e.payload,'$.plan_call_id')=OLD.id) THEN RAISE(ABORT,'dynamic conversation commit must be owned and write once') END; END");
        db.execute("CREATE TRIGGER dynamic_call_tool_completion BEFORE UPDATE OF conversation_commit_seq ON dynamic_plan_calls BEGIN SELECT CASE WHEN (SELECT count(*) FROM events t WHERE t.run_id=OLD.root_run_id AND t.kind IN ('tool.completed','tool.failed') AND json_extract(t.payload,'$.activity_id')=OLD.root_run_id||':plan:'||OLD.provider_tool_call_id)!=1 OR NOT EXISTS(SELECT 1 FROM events t WHERE t.run_id=OLD.root_run_id AND t.kind='tool.completed' AND t.seq<NEW.conversation_commit_seq AND t.seq>OLD.result_event_seq AND json_extract(t.payload,'$.activity_id')=OLD.root_run_id||':plan:'||OLD.provider_tool_call_id AND json_extract(t.payload,'$.call_id')=OLD.provider_tool_call_id AND json_extract(t.payload,'$.name')=json_extract(OLD.parent_assistant_json,'$.tool_calls[0].name') AND json_extract(t.payload,'$.data')=OLD.result_json) THEN RAISE(ABORT,'planning conversation requires once-only actual tool completion') END; END");
        db.execute("CREATE TRIGGER dynamic_call_continuation BEFORE UPDATE OF continuation_attempt_id ON dynamic_plan_calls BEGIN SELECT CASE WHEN OLD.continuation_attempt_id IS NOT NULL OR OLD.conversation_commit_seq IS NULL OR NOT EXISTS(SELECT 1 FROM agent_model_call_reservations r WHERE r.root_run_id=OLD.root_run_id AND r.attempt_id=NEW.continuation_attempt_id AND r.owner_run_id=OLD.root_run_id AND r.role='parent' AND r.state='reserved') THEN RAISE(ABORT,'dynamic continuation must be owned and write once') END; END");
        db.execute("CREATE TRIGGER dynamic_human_identity BEFORE UPDATE OF id,plan_id,root_run_id,label,backend_node_id,definition_revision,plan_call_id,question,expires_ms,published_event_seq ON dynamic_human_requests BEGIN SELECT RAISE(ABORT,'published human binding is immutable'); END");
        db.execute("CREATE TRIGGER dynamic_human_input BEFORE UPDATE OF state,input_json,actor,input_event_seq ON dynamic_human_requests BEGIN SELECT CASE WHEN OLD.state!='waiting' OR NEW.state='waiting' OR NOT EXISTS(SELECT 1 FROM events e WHERE e.seq=NEW.input_event_seq AND e.run_id=OLD.root_run_id AND e.kind='plan.human.'||NEW.state AND json_extract(e.payload,'$.request_id')=OLD.id) THEN RAISE(ABORT,'human input must be owned and write once') END; END");
        db.execute("CREATE TRIGGER dynamic_human_settlement BEFORE UPDATE OF outcome_json,settled_event_seq ON dynamic_plan_nodes WHEN OLD.human_request_id IS NOT NULL BEGIN SELECT CASE WHEN OLD.outcome_json IS NOT NULL OR NOT EXISTS(SELECT 1 FROM dynamic_human_requests h JOIN events e ON e.seq=NEW.settled_event_seq WHERE h.id=OLD.human_request_id AND h.plan_id=OLD.plan_id AND h.label=OLD.label AND h.backend_node_id=OLD.backend_node_id AND h.definition_revision=OLD.definition_revision AND h.state IN ('answered','expired','cancelled') AND e.run_id=h.root_run_id AND e.kind='plan.human.settled' AND e.payload=NEW.outcome_json AND json_extract(NEW.outcome_json,'$.human_state')=h.state AND json_extract(NEW.outcome_json,'$.request_id')=h.id AND (h.state!='answered' OR json_extract(NEW.outcome_json,'$.input_json')=h.input_json)) THEN RAISE(ABORT,'invalid human settlement boundary') END; END");
        db.execute("CREATE TRIGGER dynamic_plan_identity BEFORE UPDATE OF id,root_run_id ON dynamic_plans BEGIN SELECT RAISE(ABORT,'dynamic plan ownership is immutable'); END");
        db.execute("CREATE TRIGGER dynamic_initial_node BEFORE INSERT ON dynamic_plan_nodes WHEN NEW.state NOT IN ('pending','blocked','skipped') OR NEW.claim_revision!=0 OR NEW.claim_id IS NOT NULL OR NEW.child_run_id IS NOT NULL OR NEW.human_request_id IS NOT NULL OR NEW.outcome_json IS NOT NULL OR NEW.settled_event_seq IS NOT NULL BEGIN SELECT RAISE(ABORT,'new dynamic node cannot fabricate execution'); END");
        db.execute("CREATE TRIGGER dynamic_claim_boundary BEFORE UPDATE OF claim_revision,claim_id,child_run_id,human_request_id ON dynamic_plan_nodes WHEN OLD.claim_revision=0 BEGIN SELECT CASE WHEN NEW.claim_revision!=OLD.definition_revision OR NEW.claim_id IS NULL OR OLD.state NOT IN ('pending','blocked') OR NOT EXISTS(SELECT 1 FROM dynamic_plans p JOIN runs r ON r.id=p.root_run_id JOIN dynamic_plan_calls c ON c.plan_id=p.id WHERE p.id=OLD.plan_id AND p.state='active' AND r.state='running' AND c.state='accepted') OR (json_extract(OLD.definition_json,'$.type')='agent' AND (NEW.state!='claimed' OR NEW.child_run_id IS NULL OR NEW.human_request_id IS NOT NULL)) OR (json_extract(OLD.definition_json,'$.type')='human' AND (NEW.state!='waiting_human' OR NEW.child_run_id IS NOT NULL OR NOT EXISTS(SELECT 1 FROM dynamic_human_requests h WHERE h.id=NEW.human_request_id AND h.plan_id=OLD.plan_id AND h.label=OLD.label AND h.definition_revision=OLD.definition_revision AND h.state='waiting'))) THEN RAISE(ABORT,'invalid dynamic claim boundary') END; END");
        db.execute("CREATE TRIGGER dynamic_capability_admission BEFORE INSERT ON dynamic_capabilities BEGIN SELECT CASE WHEN NEW.catalogue_finalized!=0 OR json_extract(NEW.capabilities_json,'$.catalogue_finalized')!=0 OR json_extract(NEW.capabilities_json,'$.backend_identity')!=NEW.backend_identity OR NOT EXISTS(SELECT 1 FROM runs r JOIN agent_execution_budgets b ON b.root_run_id=r.id WHERE r.id=NEW.root_run_id AND r.parent_run_id IS NULL AND r.state='queued' AND b.policy_id='native.dynamic-plan' AND b.policy_revision=1 AND json_extract(NEW.capabilities_json,'$.workspace_identity')=b.workspace_identity AND json_extract(NEW.capabilities_json,'$.provider_identity_json')=b.provider_identity_json AND NOT EXISTS(SELECT 1 FROM graph_roots g WHERE g.run_id=r.id)) THEN RAISE(ABORT,'invalid dynamic capability admission') END; END");
        db.execute("CREATE TRIGGER dynamic_signed_call_boundary BEFORE INSERT ON dynamic_plan_calls BEGIN SELECT CASE WHEN NEW.state!='accepted' OR NEW.result_json IS NOT NULL OR NEW.result_event_seq IS NOT NULL OR NEW.conversation_commit_seq IS NOT NULL OR NEW.continuation_attempt_id IS NOT NULL OR json_array_length(NEW.parent_assistant_json,'$.tool_calls')!=1 OR json_extract(NEW.parent_assistant_json,'$.tool_calls[0].id')!=NEW.provider_tool_call_id OR json_extract(NEW.parent_assistant_json,'$.tool_calls[0].arguments')!=NEW.arguments_json OR json_extract(NEW.parent_assistant_json,'$.tool_calls[0].name')!=CASE WHEN NEW.accepted_revision=1 THEN 'plan_tasks' ELSE 'revise_plan' END OR NOT EXISTS(SELECT 1 FROM dynamic_plans p JOIN runs r ON r.id=p.root_run_id JOIN dynamic_capabilities a ON a.root_run_id=r.id JOIN agent_model_call_reservations m ON m.root_run_id=r.id JOIN events e ON e.seq=NEW.accepted_event_seq WHERE p.id=NEW.plan_id AND p.root_run_id=NEW.root_run_id AND p.revision=NEW.accepted_revision AND r.state='running' AND a.catalogue_finalized=1 AND m.attempt_id=NEW.origin_attempt_id AND m.owner_run_id=r.id AND m.role='parent' AND m.state='finished' AND m.actual_assistant_json=NEW.parent_assistant_json AND e.run_id=r.id AND e.kind='plan.call.accepted' AND json_extract(e.payload,'$.plan_call_id')=NEW.id AND EXISTS(SELECT 1 FROM events t WHERE t.run_id=r.id AND t.kind='budget.model_call.finished' AND json_extract(t.payload,'$.attempt_id')=m.attempt_id AND json_extract(t.payload,'$.was_started')=1)) THEN RAISE(ABORT,'invalid signed dynamic call boundary') END; END");
        for(const auto* table:{"dynamic_capabilities","dynamic_plans","dynamic_plan_calls","dynamic_plan_revisions","dynamic_plan_nodes","dynamic_revision_nodes","dynamic_plan_edges","dynamic_human_requests","agent_budget_segments","dynamic_owner_pauses"})db.execute(std::string("CREATE TRIGGER retain_")+table+" BEFORE DELETE ON "+table+" BEGIN SELECT RAISE(ABORT,'dynamic durable evidence cannot be deleted'); END");
        db.execute("PRAGMA user_version=11");
    }
    transaction.commit(); db.execute("PRAGMA journal_mode=WAL"); db.execute("PRAGMA synchronous=FULL");
}
Repository::~Repository()=default;
DynamicPlanCapabilities Repository::dynamic_capabilities(const std::string& root){
    const auto rows=impl_->database.execute("SELECT capabilities_json,catalogue_finalized FROM dynamic_capabilities WHERE root_run_id=?",{root}).rows;if(rows.empty())throw NotFound("Dynamic capabilities not found");
    auto c=dynamic_caps(text(rows[0][0]));if(c.catalogue_finalized!=(integer(rows[0][1])!=0))throw DatabaseError("Dynamic capability seal differs");return c;
}
DynamicPlanCapabilities Repository::finalize_dynamic_capabilities(const std::string& root,const std::string& identity,const std::string& catalogue,const std::vector<DynamicPresetCapability>& presets){
    auto& db=impl_->database;Transaction tx(db);auto c=dynamic_capabilities(root);const auto budget=root_budget(root);if(c.backend_identity!=identity)throw DynamicPlanUnavailable("Dynamic settings changed before catalogue finalization");
    if(run(root).state!=RunState::running)throw Conflict("Catalogue finalization requires its running owner");
    const auto exact=object_json(catalogue,262144);
    if(c.catalogue_finalized){if(c.tool_catalog_json!=exact||dynamic_caps_json(c).at("presets")!=dynamic_caps_json([&]{auto copy=c;copy.presets=presets;return copy;}()).at("presets"))throw Conflict("Dynamic catalogue was already sealed differently");tx.commit();return c;}
    if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=?",{root}).rows.empty())throw Conflict("Catalogue cannot change after model admission");
    if(presets.size()!=c.presets.size())throw Conflict("Registered preset set changed");
    for(std::size_t i=0;i<presets.size();++i){const auto& a=c.presets[i];const auto& b=presets[i];if(a.id!=b.id||a.revision!=b.revision||a.readonly!=b.readonly||a.turn_limit!=b.turn_limit||(!a.backend_identity.empty()&&a.backend_identity!=b.backend_identity))throw Conflict("Registered preset authority changed");}
    c.presets=presets;c.tool_catalog_json=exact;c.catalogue_finalized=true;validate_dynamic_caps(c,budget.spec,true);
    // The native discovery adapter supplies actual schema objects; every child
    // name must be present in the sealed root catalogue, with no recursion.
    const auto catalog=Json::parse(exact);if(!catalog.contains("tools")||!catalog["tools"].is_array())throw std::invalid_argument("Actual dynamic catalogue requires tools");
    std::set<std::string> names;for(const auto& tool:catalog["tools"]){if(!tool.is_object()||!tool.contains("name")||!tool["name"].is_string()||!names.insert(tool["name"].get<std::string>()).second)throw std::invalid_argument("Invalid actual tool catalogue");}
    for(const auto& p:c.presets)for(const auto& name:p.tools)if(!names.contains(name))throw Conflict("Child preset tool is absent from actual catalogue");
    changed_one(db.execute("UPDATE dynamic_capabilities SET capabilities_json=?,catalogue_finalized=1 WHERE root_run_id=? AND catalogue_finalized=0",{dynamic_caps_json(c).dump(),root}));
    impl_->event(root,"plan.capabilities.finalized",Json{{"preset_count",c.presets.size()},{"tool_count",names.size()}}.dump());tx.commit();return c;
}
DynamicPlanRecord Repository::dynamic_plan(const std::string& id){
    const auto rows=impl_->database.execute("SELECT root_run_id,revision,state_sequence,state,retired_labels_json,planned_children_reserved,planned_humans_reserved,humans_published FROM dynamic_plans WHERE id=?",{id}).rows;if(rows.empty())throw NotFound("Dynamic plan not found");const auto& r=rows[0];
    DynamicPlanRecord p;p.id=id;p.root_run_id=text(r[0]);p.revision=integer(r[1]);p.state_sequence=integer(r[2]);p.state=text(r[3]);p.capabilities=dynamic_capabilities(p.root_run_id);p.retired_labels=Json::parse(text(r[4])).get<std::vector<std::string>>();p.planned_children_reserved=integer(r[5]);p.planned_humans_reserved=integer(r[6]);p.humans_published=integer(r[7]);
    for(const auto& n:impl_->database.execute("SELECT definition_json,backend_node_id,state,definition_revision,claim_revision,COALESCE(claim_id,''),COALESCE(child_run_id,''),COALESCE(human_request_id,''),COALESCE(child_state,''),COALESCE(outcome_json,''),settled_event_seq,effect_state,protected_definition FROM dynamic_plan_nodes WHERE plan_id=? ORDER BY rowid",{id}).rows){
        DynamicNodeRecord v;v.definition=dynamic_definition(text(n[0]));v.backend_node_id=text(n[1]);v.state=dynamic_state(text(n[2]));v.definition_revision=integer(n[3]);v.claim_revision=integer(n[4]);v.claim_id=text(n[5]);v.child_run_id=text(n[6]);v.human_request_id=text(n[7]);v.child_state=text(n[8]);v.outcome_json=text(n[9]);if(!std::holds_alternative<std::nullptr_t>(n[10]))v.settled_event_seq=integer(n[10]);v.effect_state=text(n[11]);v.protected_definition=integer(n[12])!=0;p.nodes.push_back(std::move(v));
    }return p;
}
std::optional<DynamicPlanRecord> Repository::dynamic_plan_for_root(const std::string& root){run(root);const auto rows=impl_->database.execute("SELECT id FROM dynamic_plans WHERE root_run_id=?",{root}).rows;if(rows.empty())return {};return dynamic_plan(text(rows[0][0]));}
DynamicPlanCallRecord Repository::dynamic_plan_call(const std::string& id){
    const auto rows=impl_->database.execute("SELECT plan_id,root_run_id,provider_tool_call_id,origin_attempt_id,arguments_json,parent_assistant_json,state,COALESCE(result_json,''),accepted_revision,accepted_event_seq,result_event_seq,conversation_commit_seq,COALESCE(continuation_attempt_id,'') FROM dynamic_plan_calls WHERE id=?",{id}).rows;if(rows.empty())throw NotFound("Dynamic plan call not found");const auto& r=rows[0];
    DynamicPlanCallRecord c;c.id=id;c.plan_id=text(r[0]);c.root_run_id=text(r[1]);c.provider_tool_call_id=text(r[2]);c.origin_attempt_id=text(r[3]);c.arguments_json=text(r[4]);c.parent_assistant_json=text(r[5]);c.state=text(r[6]);c.result_json=text(r[7]);c.accepted_revision=integer(r[8]);c.accepted_event_seq=integer(r[9]);if(!std::holds_alternative<std::nullptr_t>(r[10]))c.result_event_seq=integer(r[10]);if(!std::holds_alternative<std::nullptr_t>(r[11]))c.conversation_commit_seq=integer(r[11]);c.continuation_attempt_id=text(r[12]);return c;
}
std::vector<DynamicPlanCallRecord> Repository::dynamic_plan_calls(const std::string& root){
    const auto owner=run(root);if(!owner.parent_id.empty()||owner.graph_root)throw std::invalid_argument("Planning calls require their ordinary owning root");std::vector<DynamicPlanCallRecord> result;
    for(const auto& row:impl_->database.execute("SELECT id FROM dynamic_plan_calls WHERE root_run_id=? ORDER BY accepted_revision,accepted_event_seq",{root}).rows){auto call=dynamic_plan_call(text(row[0]));if(call.root_run_id!=root)throw DatabaseError("Planning call read ownership differs");result.push_back(std::move(call));}return result;
}
std::vector<DynamicPlanRevisionRecord> Repository::dynamic_plan_revisions(const std::string& plan){
    const auto owner=dynamic_plan(plan);std::vector<DynamicPlanRevisionRecord> result;
    for(const auto& row:impl_->database.execute("SELECT revision,accepted_event_seq,plan_call_id,canonical_spec_json FROM dynamic_plan_revisions WHERE plan_id=? ORDER BY revision",{plan}).rows){
        DynamicPlanRevisionRecord revision{plan,integer(row[0]),integer(row[1]),text(row[2]),text(row[3])};const auto call=dynamic_plan_call(revision.call_id);
        if(call.plan_id!=plan||call.root_run_id!=owner.root_run_id||call.accepted_revision!=revision.revision||call.accepted_event_seq!=revision.accepted_event_seq)throw DatabaseError("Accepted topology revision differs from its actual call and event");
        // Do not promote arbitrary stored JSON to public topology. The same
        // strict native definition parser excludes outcomes/private authority.
        try{const auto spec=Json::parse(object_json(revision.canonical_spec_json,262144));if(spec.size()!=1||!spec.contains("nodes")||!spec["nodes"].is_array()||spec["nodes"].empty()||spec["nodes"].size()>32)throw std::invalid_argument("Invalid accepted topology");
            // Validate individual definitions without adding a wrapper to the
            // complete 256 KiB canonical frame and rejecting its legal boundary.
            std::set<std::string> labels;for(const auto& node:spec["nodes"]){const auto parsed=parse_dynamic_plan_change(Json{{"expected_revision",0},{"expected_state_sequence",0},{"add",Json::array({node})}}.dump(),true);if(!labels.insert(parsed.add.at(0).label).second)throw std::invalid_argument("Duplicate accepted topology label");}
        }catch(const std::exception&){throw DatabaseError("Invalid accepted topology definition history");}
        result.push_back(std::move(revision));
    }return result;
}
DynamicPlanRevisionRecord Repository::dynamic_plan_revision(const std::string& plan,std::int64_t revision){
    if(revision<1||revision>16)throw std::invalid_argument("Invalid accepted plan revision");const auto values=dynamic_plan_revisions(plan);const auto found=std::find_if(values.begin(),values.end(),[&](const auto& value){return value.revision==revision;});if(found==values.end())throw NotFound("Accepted plan revision not found");return *found;
}
DynamicPlanCallRecord Repository::accept_dynamic_plan_change(const DynamicPlanChangeSpec& spec){
    for(const auto* id:{&spec.id,&spec.plan_id,&spec.root_run_id,&spec.origin_attempt_id}){bounded_identity(*id,64);if(id->find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::invalid_argument("Invalid native plan identity");}bounded_identity(spec.provider_tool_call_id,256);private_identity(spec.backend_identity);
    const auto args=object_json(spec.arguments_json,262144),assistant_bytes=object_json(spec.parent_assistant_json,8*1024*1024);const auto assistant=Json::parse(assistant_bytes);
    const auto preliminary=Json::parse(args);if(!preliminary.contains("expected_revision")||!preliminary["expected_revision"].is_number_integer())throw std::invalid_argument("Planning requires an exact revision precondition");const bool initial=preliminary.at("expected_revision")==0;const auto change=parse_dynamic_plan_change(args,initial);
    if(!assistant.contains("tool_calls")||!assistant["tool_calls"].is_array()||assistant["tool_calls"].size()!=1)throw std::invalid_argument("Planning must own one complete assistant call");
    const auto& signed_call=assistant["tool_calls"][0];if(!signed_call.is_object()||!signed_call.contains("id")||!signed_call["id"].is_string()||signed_call["id"]!=spec.provider_tool_call_id||!signed_call.contains("name")||!signed_call["name"].is_string()||signed_call["name"]!=(initial?"plan_tasks":"revise_plan")||!signed_call.contains("arguments")||!signed_call["arguments"].is_string()||signed_call["arguments"].get<std::string>()!=args)throw std::invalid_argument("Planning call differs from its actual assistant DTO");
    auto& db=impl_->database;Transaction tx(db);const auto root=run(spec.root_run_id);const auto budget=root_budget(root.id);const auto caps=dynamic_capabilities(root.id);
    if(root.state!=RunState::running||!caps.catalogue_finalized||caps.backend_identity!=spec.backend_identity)throw DynamicPlanUnavailable("Planning owner or frozen authority is unavailable");
    const auto origin=db.execute("SELECT state,COALESCE(actual_assistant_json,'') FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND role='parent'",{root.id,spec.origin_attempt_id,root.id}).rows;
    if(origin.empty()||text(origin[0][0])!="finished"||text(origin[0][1])!=assistant_bytes||db.execute("SELECT seq FROM events WHERE run_id=? AND kind='budget.model_call.finished' AND json_extract(payload,'$.attempt_id')=? AND json_extract(payload,'$.was_started')=1",{root.id,spec.origin_attempt_id}).rows.empty())throw Conflict("Planning origin is not its exact finished actual parent response");
    const auto replay=db.execute("SELECT id FROM dynamic_plan_calls WHERE root_run_id=? AND provider_tool_call_id=?",{root.id,spec.provider_tool_call_id}).rows;
    if(!replay.empty()){auto old=dynamic_plan_call(text(replay[0][0]));if(old.plan_id!=spec.plan_id||old.origin_attempt_id!=spec.origin_attempt_id||old.arguments_json!=args||old.parent_assistant_json!=assistant_bytes)throw Conflict("Planning call identity changed");if(old.state=="interrupted"||old.state=="cancelled")throw Conflict("Retired planning call cannot replay");tx.commit();return old;}
    if(!db.execute("SELECT id FROM dynamic_plan_calls WHERE root_run_id=? AND (state IN ('accepted','report_ready') OR (state='turn_committed' AND continuation_attempt_id IS NULL))",{root.id}).rows.empty())throw Conflict("Previous planning call has no complete continuation");
    auto existing=dynamic_plan_for_root(root.id);DynamicPlanRecord p;if(existing)p=*existing;else {p.id=spec.plan_id;p.root_run_id=root.id;p.capabilities=caps;}
    if(p.id!=spec.plan_id)throw Conflict("Root owns another immutable plan identity");dynamic_cas(p,change.expected_revision,change.expected_state_sequence,spec.backend_identity);
    const auto candidate=[&]{try{return reduce_dynamic_plan(p,change,caps);}catch(const Conflict&){throw std::invalid_argument("Requested planning revision is not admissible");}}();
    if(spec.expected_budget_revision!=budget.revision)throw DynamicPlanChanged("Planning budget changed");if(budget.children_admitted+candidate.planned_children_reserved>budget.spec.max_children||budget.model_calls_reserved+budget.parent_calls_held>=budget.spec.max_model_calls||budget.parent_model_calls_reserved+budget.parent_calls_held>=caps.max_parent_turns)throw RootBudgetExhausted("Plan cannot reserve child and continuation capacity");dynamic_safe_sequence(p.state_sequence);
    const auto revision=p.revision+1,sequence=p.state_sequence+1;
    if(!existing)changed_one(db.execute("INSERT INTO dynamic_plans(id,root_run_id,revision,state_sequence,state,retired_labels_json,planned_children_reserved,planned_humans_reserved) VALUES(?,?,?,?,'active',?,?,?)",{p.id,root.id,revision,sequence,Json(candidate.retired_labels).dump(),candidate.planned_children_reserved,candidate.planned_humans_reserved}));
    else changed_one(db.execute("UPDATE dynamic_plans SET revision=?,state_sequence=?,state='active',retired_labels_json=?,planned_children_reserved=?,planned_humans_reserved=? WHERE id=? AND revision=? AND state_sequence=?",{revision,sequence,Json(candidate.retired_labels).dump(),candidate.planned_children_reserved,candidate.planned_humans_reserved,p.id,p.revision,p.state_sequence}));
    changed_one(db.execute("UPDATE agent_execution_budgets SET planned_children_reserved=?,parent_calls_held=parent_calls_held+1,revision=revision+1 WHERE root_run_id=? AND revision=?",{candidate.planned_children_reserved,root.id,budget.revision}));
    const auto accepted=impl_->event(root.id,"plan.call.accepted",Json{{"plan_id",p.id},{"plan_call_id",spec.id},{"revision",revision},{"state_sequence",sequence},{"provider_tool_call_id",spec.provider_tool_call_id},{"origin_attempt_id",spec.origin_attempt_id}}.dump());
    changed_one(db.execute("INSERT INTO dynamic_plan_calls(id,plan_id,root_run_id,provider_tool_call_id,origin_attempt_id,arguments_json,parent_assistant_json,state,accepted_revision,accepted_event_seq) VALUES(?,?,?,?,?,?,?,'accepted',?,?)",{spec.id,p.id,root.id,spec.provider_tool_call_id,spec.origin_attempt_id,args,assistant_bytes,revision,accepted.sequence}));
    changed_one(db.execute("INSERT INTO dynamic_plan_revisions(plan_id,revision,canonical_spec_json,plan_call_id,accepted_event_seq) VALUES(?,?,?,?,?)",{p.id,revision,candidate.canonical_spec_json,spec.id,accepted.sequence}));
    std::size_t position=0;for(auto n:candidate.nodes){if(n.backend_node_id.empty())n.backend_node_id=p.id+"_n"+std::to_string(position);++position;const auto definition=dynamic_definition_json(n.definition).dump();
        const auto old=db.execute("SELECT definition_json,definition_revision,state FROM dynamic_plan_nodes WHERE plan_id=? AND label=?",{p.id,n.definition.label}).rows;
        if(old.empty())changed_one(db.execute("INSERT INTO dynamic_plan_nodes(plan_id,label,backend_node_id,definition_json,definition_revision,state) VALUES(?,?,?,?,?,?)",{p.id,n.definition.label,n.backend_node_id,definition,n.definition_revision,dynamic_state_name(n.state)}));
        else if(text(old[0][0])!=definition||integer(old[0][1])!=n.definition_revision)changed_one(db.execute("UPDATE dynamic_plan_nodes SET definition_json=?,definition_revision=?,state=? WHERE plan_id=? AND label=? AND state IN ('pending','blocked') AND protected_definition=0",{definition,n.definition_revision,dynamic_state_name(n.state),p.id,n.definition.label}));
        else if(text(old[0][2])!=dynamic_state_name(n.state))changed_one(db.execute("UPDATE dynamic_plan_nodes SET state=? WHERE plan_id=? AND label=? AND state IN ('pending','blocked') AND protected_definition=0",{dynamic_state_name(n.state),p.id,n.definition.label}));
        changed_one(db.execute("INSERT INTO dynamic_revision_nodes(plan_id,revision,label,backend_node_id,definition_json,definition_revision) VALUES(?,?,?,?,?,?)",{p.id,revision,n.definition.label,n.backend_node_id,definition,n.definition_revision}));
    }
    for(const auto& n:candidate.nodes)for(const auto& e:n.definition.dependencies)changed_one(db.execute("INSERT INTO dynamic_plan_edges(plan_id,revision,label,dependency_label,requirement) VALUES(?,?,?,?,?)",{p.id,revision,n.definition.label,e.task,e.require==DynamicDependencyRequirement::success?"success":"observed"}));
    auto result=dynamic_plan_call(spec.id);result.created=true;tx.commit();return result;
}
Session Repository::create_session(const std::string& id,const std::string& title) {
    identifier(id); Transaction transaction(impl_->database);
    if(!impl_->database.execute("SELECT id FROM sessions WHERE id=?",{id}).rows.empty()) throw Conflict("Session already exists");
    changed_one(impl_->database.execute("INSERT INTO sessions(id,title) VALUES(?,?)",{id,title}));
    transaction.commit(); return {id,title};
}
DynamicFrontierClaim Repository::admit_dynamic_frontier(const DynamicFrontierSpec& spec){
    if(spec.tasks.empty()||spec.tasks.size()>2)throw std::invalid_argument("Dynamic frontier must contain one or two native agents");auto& db=impl_->database;Transaction tx(db);auto p=dynamic_plan(spec.plan_id);dynamic_cas(p,spec.expected_revision,spec.expected_state_sequence,spec.backend_identity);const auto root=run(p.root_run_id);const auto budget=root_budget(root.id);const auto call=dynamic_plan_call(spec.plan_call_id);
    if(root.state!=RunState::running||p.state!="active"||call.plan_id!=p.id||call.state!="accepted")throw Conflict("Dynamic frontier lacks its running signed-call owner");if(budget.revision!=spec.expected_budget_revision)throw DynamicPlanChanged("Dynamic frontier budget changed");dynamic_safe_sequence(p.state_sequence);
    const auto active=integer(db.execute("SELECT count(*) FROM runs WHERE parent_run_id=? AND state IN ('queued','running','paused')",{root.id}).rows.at(0).at(0));if(active+static_cast<std::int64_t>(spec.tasks.size())>budget.spec.max_parallel)throw Conflict("Dynamic frontier has no physical child capacity");
    std::set<std::string> labels,claims,children;std::vector<DynamicPreparedNode> prepared;
    for(const auto& task:spec.tasks){bounded_identity(task.claim_id,128);bounded_identity(task.child_run_id,128);if(!labels.insert(task.label).second||!claims.insert(task.claim_id).second||!children.insert(task.child_run_id).second)throw std::invalid_argument("Duplicate dynamic admission identities");
        auto n=prepare_dynamic_node(p,task.label);if(n.node.definition.kind!=DynamicNodeKind::agent||n.node.definition_revision!=task.definition_revision||task.resolved_input_json!=n.dependency_outputs_json)throw Conflict("Dynamic child differs from its actual prepared definition");
        const auto prompt=dynamic_prompt(n.node.definition,n.dependency_outputs_json,p.capabilities).dump();if(!task.prompt_json.empty()&&task.prompt_json!=prompt)throw Conflict("Dynamic child prompt is not derived from actual observations");prepared.push_back(std::move(n));}
    const auto count=static_cast<std::int64_t>(prepared.size());if(budget.planned_children_reserved<count||p.planned_children_reserved<count||budget.children_admitted+budget.planned_children_reserved>budget.spec.max_children)throw RootBudgetExhausted("Dynamic planned child capacity changed");
    changed_one(db.execute("UPDATE agent_execution_budgets SET planned_children_reserved=planned_children_reserved-?,children_admitted=children_admitted+?,revision=revision+1 WHERE root_run_id=? AND revision=?",{count,count,root.id,budget.revision}));
    changed_one(db.execute("UPDATE dynamic_plans SET planned_children_reserved=planned_children_reserved-?,state_sequence=state_sequence+1 WHERE id=? AND revision=? AND state_sequence=?",{count,p.id,p.revision,p.state_sequence}));
    for(std::size_t i=0;i<prepared.size();++i){const auto& task=spec.tasks[i];const auto& n=prepared[i].node;
        changed_one(db.execute("UPDATE dynamic_plan_nodes SET state='claimed',claim_revision=definition_revision,claim_id=?,child_run_id=?,resolved_input_json=?,protected_definition=1 WHERE plan_id=? AND label=? AND definition_revision=? AND state IN ('pending','blocked') AND claim_revision=0",{task.claim_id,task.child_run_id,prepared[i].dependency_outputs_json,p.id,task.label,task.definition_revision}));
        changed_one(db.execute("INSERT INTO runs(id,session_id,state,parent_run_id,node_id) VALUES(?,?,'queued',?,?)",{task.child_run_id,root.session_id,root.id,n.backend_node_id}));changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{task.child_run_id}));
        const Run child{task.child_run_id,root.session_id,RunState::queued,root.id,n.backend_node_id,false,root.provider_context_json};impl_->message(child,"user",dynamic_prompt(n.definition,prepared[i].dependency_outputs_json,p.capabilities).dump());impl_->event(child.id,"run.queued","{}");
        impl_->event(child.id,"plan.child.admitted",Json{{"plan_id",p.id},{"plan_call_id",call.id},{"label",task.label},{"claim_id",task.claim_id},{"child_run_id",child.id},{"definition_revision",n.definition_revision},{"preset_id",n.definition.preset_id},{"preset_revision",n.definition.preset_revision}}.dump());
    }
    DynamicFrontierClaim result;result.plan=dynamic_plan(p.id);result.budget=root_budget(root.id);for(const auto& task:spec.tasks)result.nodes.push_back(*std::find_if(result.plan.nodes.begin(),result.plan.nodes.end(),[&](const auto& n){return n.definition.label==task.label;}));result.created=true;tx.commit();return result;
}
DynamicNodeRecord Repository::settle_dynamic_child(const std::string& child_id) try {
    auto& db=impl_->database;Transaction tx(db);const auto rows=db.execute("SELECT plan_id,label FROM dynamic_plan_nodes WHERE child_run_id=?",{child_id}).rows;if(rows.empty())throw NotFound("Dynamic child not found");
    auto p=dynamic_plan(text(rows[0][0]));auto found=std::find_if(p.nodes.begin(),p.nodes.end(),[&](const auto& n){return n.child_run_id==child_id;});if(found==p.nodes.end())throw DatabaseError("Dynamic child ownership differs");auto n=*found;const auto child=run(child_id);const auto admitted=child_admission(child_id);
    if(admitted.kind!=ChildAdmissionKind::dynamic_agent||admitted.plan_id!=p.id||child.parent_id!=p.root_run_id||child.node_id!=n.backend_node_id)throw Conflict("Dynamic child is not its exact claimed execution");
    if(n.settled_event_seq){tx.commit();return n;}if(n.state!=DynamicNodeState::claimed||!terminal(child.state))throw Conflict("Dynamic child has not physically retired");
    if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE owner_run_id=? AND state IN ('reserved','started')",{child_id}).rows.empty())throw Conflict("Dynamic child still owns model transport");
    const auto effect=dynamic_effect_state(operations(child_id));
    Json outcome={{"child_run_id",child_id},{"label",n.definition.label},{"child_state",state_name(child.state)},{"preset_id",n.definition.preset_id},{"preset_revision",n.definition.preset_revision},{"effect_state",effect}};
    if(child.state==RunState::completed){const auto h=run_history(child_id);if(h.empty()||h.back().role!="assistant")throw DatabaseError("Completed dynamic child lacks actual output");const auto answer=Json::parse(h.back().json);if(!answer.contains("content")||!answer["content"].is_string())throw DatabaseError("Invalid dynamic child output");
        for(const auto* key:{"content","model","usage","elapsed_ms","first_token_ms"})if(answer.contains(key))outcome[key]=answer[key];
    }else {Json error=Json::object();const auto failure=db.execute("SELECT payload FROM events WHERE run_id=? AND kind=? ORDER BY seq DESC LIMIT 1",{child_id,"run."+state_name(child.state)}).rows;if(!failure.empty()){const auto source=Json::parse(text(failure[0][0]));for(const auto* key:{"reason","code","protocol_error_code"})if(source.contains(key)&&source[key].is_string()&&source[key].get<std::string>().size()<=256)error[key]=source[key];}outcome["error"]=error;}
    if(outcome.dump().size()>32768)outcome={{"child_run_id",child_id},{"label",n.definition.label},{"child_state",state_name(child.state)},{"preset_id",n.definition.preset_id},{"preset_revision",n.definition.preset_revision},{"effect_state",effect},{"error",{{"code","result_limit_exceeded"}}},{"history_ref","/v1/runs/"+p.root_run_id+"/children/"+child_id+"/history"}};
    dynamic_safe_sequence(p.state_sequence);const auto event=impl_->event(child_id,"plan.child.settled",outcome.dump());
    changed_one(db.execute("UPDATE dynamic_plan_nodes SET state=?,child_state=?,outcome_json=?,settled_event_seq=?,effect_state=? WHERE plan_id=? AND label=? AND state='claimed' AND outcome_json IS NULL",{effect=="uncertain"?"uncertain":"settled",state_name(child.state),outcome.dump(),event.sequence,effect,p.id,n.definition.label}));
    changed_one(db.execute("UPDATE dynamic_plans SET state_sequence=state_sequence+1,state=? WHERE id=? AND state_sequence=?",{effect=="uncertain"?"uncertain":p.state,p.id,p.state_sequence}));
    auto current=dynamic_plan(p.id);auto result=*std::find_if(current.nodes.begin(),current.nodes.end(),[&](const auto& v){return v.child_run_id==child_id;});tx.commit();return result;
}catch(const DatabaseError&){throw DynamicOutcomeUnrecorded("Dynamic child outcome was not durably recorded");}
DynamicPlanStepResult Repository::settle_dynamic_plan_step(const std::string& call_id) try {
    auto& db=impl_->database;Transaction tx(db);auto c=dynamic_plan_call(call_id);auto p=dynamic_plan(c.plan_id);if(c.state=="interrupted"||c.state=="cancelled")throw Conflict("Retired plan call cannot acquire a report");if(!c.result_json.empty()){tx.commit();return {p,c,true};}
    if(c.state!="accepted"||run(c.root_run_id).state!=RunState::running)throw Conflict("Plan report requires its running owner");const auto decision=inspect_dynamic_plan(p);if(decision.halted)throw WorkspaceEffectUncertain("Dynamic plan has an uncertain outcome");if(!decision.report_ready){tx.commit();return {p,c,false};}
    const auto output=dynamic_plan_report(p);if(output.size()>65536)throw DatabaseError("Dynamic report exceeds its native bound");const auto event=impl_->event(c.root_run_id,"plan.call.reported",output);
    changed_one(db.execute("UPDATE dynamic_plan_calls SET state='report_ready',result_json=?,result_event_seq=? WHERE id=? AND state='accepted' AND result_json IS NULL",{output,event.sequence,c.id}));c=dynamic_plan_call(c.id);tx.commit();return {p,c,true};
}catch(const DatabaseError&){throw DynamicOutcomeUnrecorded("Dynamic plan report was not durably recorded");}
void Repository::commit_dynamic_tool_turn(const std::string& root,const std::string& call_id) try {
    auto& db=impl_->database;Transaction tx(db);auto c=dynamic_plan_call(call_id);if(c.root_run_id!=root)throw Conflict("Planning conversation owner differs");
    const auto signed_turn=Json::parse(c.parent_assistant_json);const auto name=signed_turn.at("tool_calls").at(0).at("name").get<std::string>();
    const auto activity=root+":plan:"+c.provider_tool_call_id;
    const auto starts=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='tool.started' AND json_extract(payload,'$.activity_id')=?",{root,activity}).rows;
    const auto origin=db.execute("SELECT seq FROM events WHERE run_id=? AND kind='budget.model_call.finished' AND json_extract(payload,'$.attempt_id')=? AND json_extract(payload,'$.was_started')=1",{root,c.origin_attempt_id}).rows;
    if(starts.size()!=1||origin.size()!=1||integer(starts[0][0])<=integer(origin[0][0])||integer(starts[0][0])>=c.accepted_event_seq)throw Conflict("Planning commit lacks one prior actual tool start");
    const auto started=Json::parse(text(starts[0][1]));
    if(!started.contains("call_id")||started["call_id"]!=c.provider_tool_call_id||!started.contains("name")||started["name"]!=name)throw Conflict("Planning start differs from its signed call");
    if(started.contains("arguments_json")){if(!started["arguments_json"].is_string()||started["arguments_json"].get<std::string>()!=c.arguments_json)throw Conflict("Planning start arguments differ");}
    else if(!started.contains("arguments")||started["arguments"]!=Json::parse(c.arguments_json))throw Conflict("Planning start arguments differ");
    const auto completions=db.execute("SELECT seq,kind,payload FROM events WHERE run_id=? AND kind IN ('tool.completed','tool.failed') AND json_extract(payload,'$.activity_id')=?",{root,activity}).rows;
    if(c.conversation_commit_seq){
        if(completions.size()!=1||text(completions[0][1])!="tool.completed"||integer(completions[0][0])>=*c.conversation_commit_seq)throw DatabaseError("Committed planning turn lacks its once-only actual completion");
        const auto completed=Json::parse(text(completions[0][2]));if(completed.value("call_id",std::string{})!=c.provider_tool_call_id||completed.value("name",std::string{})!=name||!completed.contains("data")||completed["data"]!=Json::parse(c.result_json))throw DatabaseError("Committed planning tool completion differs from its actual report");
        tx.commit();return;
    }
    const auto owner=run(root);if(owner.state!=RunState::running||c.state!="report_ready"||c.result_json.empty()||!completions.empty())throw Conflict("Planning turn lacks its actual uncommitted report");
    impl_->event(root,"tool.completed",Json{{"activity_id",activity},{"call_id",c.provider_tool_call_id},{"name",name},{"data",Json::parse(c.result_json)}}.dump());
    impl_->message(owner,"assistant",c.parent_assistant_json);impl_->message(owner,"tool",Json{{"content",c.result_json},{"tool_call_id",c.provider_tool_call_id}}.dump());const auto event=impl_->event(root,"conversation.tool_turn",Json{{"plan_call_id",call_id}}.dump());
    changed_one(db.execute("UPDATE dynamic_plan_calls SET state='turn_committed',conversation_commit_seq=? WHERE id=? AND state='report_ready' AND conversation_commit_seq IS NULL",{event.sequence,call_id}));tx.commit();
}catch(const DatabaseError&){throw DynamicOutcomeUnrecorded("Dynamic signed conversation turn was not durably committed");}
void Repository::record_rejected_dynamic_tool_turn(const std::string& root,const std::string& origin_id,const std::string& actual_assistant,const std::string& code) try {
    (void)DynamicPlanRejected(code);bounded_identity(origin_id);
    const auto exact=object_json(actual_assistant,8*1024*1024);const auto reply=Json::parse(exact);
    if(!reply.contains("tool_calls")||!reply["tool_calls"].is_array()||reply["tool_calls"].size()!=1)throw std::invalid_argument("Rejected planning turn must own one actual tool call");
    const auto& call=reply["tool_calls"][0];if(!call.is_object()||!call.contains("id")||!call["id"].is_string()||!call.contains("name")||!call["name"].is_string()||!call.contains("arguments")||!call["arguments"].is_string())throw std::invalid_argument("Rejected planning turn has invalid actual call fields");
    const auto provider_call=call["id"].get<std::string>(),name=call["name"].get<std::string>(),arguments=call["arguments"].get<std::string>();bounded_identity(provider_call,256);
    if(name!="plan_tasks"&&name!="revise_plan"&&name!="inspect_plan")throw std::invalid_argument("Only preaccept planning tools use typed rejection");
    auto& db=impl_->database;Transaction tx(db);const auto owner=run(root);const auto caps=dynamic_capabilities(root);if(!owner.parent_id.empty()||owner.graph_root||!caps.catalogue_finalized)throw Conflict("Rejected planning turn lacks its owning admitted root");
    const auto origin=db.execute("SELECT actual_assistant_json FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND role='parent' AND state='finished'",{root,origin_id,root}).rows;
    const auto finished=db.execute("SELECT seq FROM events WHERE run_id=? AND kind='budget.model_call.finished' AND json_extract(payload,'$.attempt_id')=? AND json_extract(payload,'$.was_started')=1",{root,origin_id}).rows;
    if(origin.size()!=1||std::holds_alternative<std::nullptr_t>(origin[0][0])||text(origin[0][0])!=exact||finished.size()!=1)throw Conflict("Rejected planning turn differs from its actual finished response");
    if(!db.execute("SELECT id FROM dynamic_plan_calls WHERE root_run_id=? AND (provider_tool_call_id=? OR state IN ('accepted','report_ready') OR (state='turn_committed' AND continuation_attempt_id IS NULL))",{root,provider_call}).rows.empty())throw Conflict("Accepted planning work cannot acquire a generic rejected conversation");
    const auto activity=root+":plan:"+provider_call;const auto starts=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='tool.started' AND json_extract(payload,'$.activity_id')=?",{root,activity}).rows;
    if(starts.size()!=1||integer(starts[0][0])<=integer(finished[0][0]))throw Conflict("Rejected planning turn lacks one actual tool start");
    const auto started=Json::parse(text(starts[0][1]));if(!started.contains("call_id")||started["call_id"]!=provider_call||!started.contains("name")||started["name"]!=name)throw Conflict("Rejected planning start differs from actual call");
    if(started.contains("arguments_json")){if(!started["arguments_json"].is_string()||started["arguments_json"].get<std::string>()!=arguments)throw Conflict("Rejected planning start arguments differ");}
    else {if(!started.contains("arguments"))throw Conflict("Rejected planning start arguments are absent");try{if(started["arguments"]!=Json::parse(arguments))throw Conflict("Rejected planning start arguments differ");}catch(const Json::exception&){throw Conflict("Invalid planning JSON requires exact raw start arguments");}}
    const auto output=Json{{"source","native_dynamic_plan"},{"accepted",false},{"error",{{"code",code}}}};
    const auto markers=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='conversation.tool_turn' AND json_extract(payload,'$.rejected_plan_call_id')=?",{root,provider_call}).rows;
    const auto completions=db.execute("SELECT seq,kind,payload FROM events WHERE run_id=? AND kind IN ('tool.completed','tool.failed') AND json_extract(payload,'$.activity_id')=?",{root,activity}).rows;
    if(!markers.empty()){
        const auto marker=Json::parse(text(markers[0][1]));if(markers.size()!=1||marker.value("origin_attempt_id",std::string{})!=origin_id||marker.value("error_code",std::string{})!=code)throw Conflict("Rejected planning commit binding changed");
        if(completions.size()!=1||text(completions[0][1])!="tool.failed"||integer(completions[0][0])>=integer(markers[0][0]))throw DatabaseError("Rejected planning commit lacks once-only actual failure");const auto failure=Json::parse(text(completions[0][2]));if(failure.value("call_id",std::string{})!=provider_call||failure.value("name",std::string{})!=name||!failure.contains("data")||failure["data"]!=output)throw DatabaseError("Rejected planning tool failure differs from fixed safe result");tx.commit();return;
    }
    if(owner.state!=RunState::running||!completions.empty())throw Conflict("Rejected planning owner or activity is already retired");
    impl_->event(root,"tool.failed",Json{{"activity_id",activity},{"call_id",provider_call},{"name",name},{"data",output}}.dump());impl_->message(owner,"assistant",exact);impl_->message(owner,"tool",Json{{"content",output.dump()},{"tool_call_id",provider_call}}.dump());impl_->event(root,"conversation.tool_turn",Json{{"rejected_plan_call_id",provider_call},{"origin_attempt_id",origin_id},{"error_code",code}}.dump());tx.commit();
}catch(const DatabaseError&){throw DynamicOutcomeUnrecorded("Rejected actual planning turn was not durably committed");}
ModelCallReservation Repository::reserve_dynamic_continuation(const std::string& call_id,const std::string& attempt){
    bounded_identity(attempt);auto& db=impl_->database;Transaction tx(db);const auto c=dynamic_plan_call(call_id);const auto budget=root_budget(c.root_run_id);const auto caps=dynamic_capabilities(c.root_run_id);if(run(c.root_run_id).state!=RunState::running||c.state!="turn_committed"||!c.conversation_commit_seq)throw Conflict("Planning continuation lacks its committed actual turn");
    if(!c.continuation_attempt_id.empty()){if(c.continuation_attempt_id!=attempt)throw Conflict("Planning continuation identity changed");const auto old=db.execute("SELECT state FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{c.root_run_id,attempt}).rows;if(old.empty()||text(old[0][0])!="reserved")throw Conflict("Planning continuation already dispatched or retired");tx.commit();return {c.root_run_id,attempt,c.root_run_id,ModelCallRole::parent,"reserved"};}
    if(budget.parent_calls_held<1||budget.model_calls_reserved>=budget.spec.max_model_calls||budget.parent_model_calls_reserved>=caps.max_parent_turns)throw RootBudgetExhausted("Planning continuation allowance exhausted");
    changed_one(db.execute("UPDATE agent_execution_budgets SET model_calls_reserved=model_calls_reserved+1,parent_calls_held=parent_calls_held-1,revision=revision+1 WHERE root_run_id=? AND revision=?",{c.root_run_id,budget.revision}));
    changed_one(db.execute("INSERT INTO agent_model_call_reservations(root_run_id,attempt_id,owner_run_id,role,state) VALUES(?,?,?,'parent','reserved')",{c.root_run_id,attempt,c.root_run_id}));
    changed_one(db.execute("UPDATE dynamic_plan_calls SET continuation_attempt_id=? WHERE id=? AND continuation_attempt_id IS NULL",{attempt,call_id}));impl_->event(c.root_run_id,"budget.model_call.reserved",Json{{"root_run_id",c.root_run_id},{"attempt_id",attempt},{"role","parent"},{"plan_call_id",call_id}}.dump());tx.commit();return {c.root_run_id,attempt,c.root_run_id,ModelCallRole::parent,"reserved"};
}
DynamicHumanRequest Repository::dynamic_human_request(const std::string& plan,const std::string& id){
    const auto rows=impl_->database.execute("SELECT root_run_id,label,backend_node_id,question,state,COALESCE(input_json,''),COALESCE(actor,''),definition_revision,published_event_seq,expires_ms,input_event_seq FROM dynamic_human_requests WHERE id=? AND plan_id=?",{id,plan}).rows;if(rows.empty())throw NotFound("Human request is not owned by this plan");const auto& r=rows[0];
    DynamicHumanRequest h;h.id=id;h.plan_id=plan;h.root_run_id=text(r[0]);h.label=text(r[1]);h.backend_node_id=text(r[2]);h.question=text(r[3]);h.state=text(r[4]);h.input_json=text(r[5]);h.actor=text(r[6]);h.definition_revision=integer(r[7]);h.published_event_seq=integer(r[8]);h.expires_unix_ms=integer(r[9]);if(!std::holds_alternative<std::nullptr_t>(r[10]))h.input_event_seq=integer(r[10]);return h;
}
std::vector<DynamicHumanRequest> Repository::dynamic_human_requests(const std::string& plan){dynamic_plan(plan);std::vector<DynamicHumanRequest> out;for(const auto& r:impl_->database.execute("SELECT id FROM dynamic_human_requests WHERE plan_id=? ORDER BY rowid",{plan}).rows)out.push_back(dynamic_human_request(plan,text(r[0])));return out;}
DynamicHumanRequest Repository::publish_dynamic_human(const DynamicHumanRequestSpec& spec){
    bounded_identity(spec.id,128);auto& db=impl_->database;Transaction tx(db);auto p=dynamic_plan(spec.plan_id);dynamic_cas(p,spec.expected_revision,spec.expected_state_sequence,spec.backend_identity);const auto call=dynamic_plan_call(spec.plan_call_id);
    if(run(p.root_run_id).state!=RunState::running||p.state!="active"||call.plan_id!=p.id||call.state!="accepted")throw Conflict("Human question has no actual running planning call");const auto prepared=prepare_dynamic_node(p,spec.label);const auto& n=prepared.node;if(n.definition.kind!=DynamicNodeKind::human)throw std::invalid_argument("Only declared human tasks publish questions");
    const auto now=now_ms();if(spec.expires_unix_ms<=now||spec.expires_unix_ms>now+p.capabilities.human_expiry_ms)throw std::invalid_argument("Human question expiry exceeds its admitted policy");if(p.planned_humans_reserved<1||p.humans_published>=p.capabilities.max_humans)throw RootBudgetExhausted("Human question allowance exhausted");dynamic_safe_sequence(p.state_sequence);
    const auto event=impl_->event(p.root_run_id,"plan.human.published",Json{{"plan_id",p.id},{"plan_call_id",call.id},{"request_id",spec.id},{"label",spec.label},{"definition_revision",n.definition_revision},{"question",n.definition.question},{"expires_unix_ms",spec.expires_unix_ms}}.dump());
    changed_one(db.execute("INSERT INTO dynamic_human_requests(id,plan_id,root_run_id,label,backend_node_id,definition_revision,plan_call_id,question,state,expires_ms,published_event_seq) VALUES(?,?,?,?,?,?,?,?,'waiting',?,?)",{spec.id,p.id,p.root_run_id,spec.label,n.backend_node_id,n.definition_revision,call.id,n.definition.question,spec.expires_unix_ms,event.sequence}));
    changed_one(db.execute("UPDATE dynamic_plan_nodes SET state='waiting_human',claim_revision=definition_revision,claim_id=?,human_request_id=?,resolved_input_json=?,protected_definition=1 WHERE plan_id=? AND label=? AND state IN ('pending','blocked') AND claim_revision=0",{spec.id,spec.id,prepared.dependency_outputs_json,p.id,spec.label}));
    changed_one(db.execute("UPDATE dynamic_plans SET planned_humans_reserved=planned_humans_reserved-1,humans_published=humans_published+1,state_sequence=state_sequence+1 WHERE id=? AND state_sequence=?",{p.id,p.state_sequence}));auto result=dynamic_human_request(p.id,spec.id);tx.commit();return result;
}
DynamicHumanRequest Repository::input_dynamic_human(const DynamicHumanInputSpec& spec){
    const auto input=object_json(spec.input_json,16384);identifier(spec.authenticated_actor);if(spec.authenticated_actor.size()>4096)throw std::invalid_argument("Human actor exceeds its limit");
    auto& db=impl_->database;Transaction tx(db);auto p=dynamic_plan(spec.plan_id);if(p.capabilities.backend_identity!=spec.backend_identity)throw DynamicPlanUnavailable("Human request frozen settings are stale");auto h=dynamic_human_request(p.id,spec.request_id);const auto owner=run(p.root_run_id);
    if(owner.state!=RunState::paused&&owner.state!=RunState::running)throw Conflict("Human owner is terminal");if(h.root_run_id!=owner.id)throw Conflict("Human request owner differs");if(h.state=="answered"){if(h.input_json!=input||h.actor!=spec.authenticated_actor)throw Conflict("Human answer changed");tx.commit();return h;}
    dynamic_cas(p,spec.expected_revision,spec.expected_state_sequence,spec.backend_identity);if(h.state!="waiting"||h.expires_unix_ms<=now_ms())throw Conflict("Human request is no longer waiting");dynamic_safe_sequence(p.state_sequence);
    const auto outcome=Json{{"human_state","answered"},{"request_id",h.id},{"input_json",input}}.dump();
    if(outcome.size()>32768)throw std::invalid_argument("Encoded human observation exceeds its native outcome bound");
    const auto event=impl_->event(owner.id,"plan.human.answered",Json{{"request_id",h.id},{"plan_id",p.id},{"label",h.label},{"input_json",input},{"actor",spec.authenticated_actor}}.dump());
    changed_one(db.execute("UPDATE dynamic_human_requests SET state='answered',input_json=?,actor=?,input_event_seq=? WHERE id=? AND plan_id=? AND state='waiting'",{input,spec.authenticated_actor,event.sequence,h.id,p.id}));
    const auto settled=impl_->event(owner.id,"plan.human.settled",outcome);
    changed_one(db.execute("UPDATE dynamic_plan_nodes SET state='settled',outcome_json=?,settled_event_seq=? WHERE plan_id=? AND label=? AND human_request_id=? AND definition_revision=? AND state='waiting_human'",{outcome,settled.sequence,p.id,h.label,h.id,h.definition_revision}));
    changed_one(db.execute("UPDATE dynamic_plans SET state_sequence=state_sequence+1 WHERE id=? AND state_sequence=?",{p.id,p.state_sequence}));auto result=dynamic_human_request(p.id,h.id);tx.commit();return result;
}
DynamicHumanRequest Repository::expire_dynamic_human(const std::string& plan,const std::string& id){
    auto& db=impl_->database;Transaction tx(db);auto p=dynamic_plan(plan);auto h=dynamic_human_request(plan,id);if(h.state=="expired"){tx.commit();return h;}if(h.state!="waiting"||h.expires_unix_ms>now_ms())throw Conflict("Human question has not expired");const auto root=run(p.root_run_id);if(root.state!=RunState::running&&root.state!=RunState::paused)throw Conflict("Human owner is terminal");dynamic_safe_sequence(p.state_sequence);
    const auto event=impl_->event(root.id,"plan.human.expired",Json{{"request_id",h.id},{"plan_id",p.id},{"label",h.label}}.dump());changed_one(db.execute("UPDATE dynamic_human_requests SET state='expired',input_event_seq=? WHERE id=? AND state='waiting'",{event.sequence,h.id}));
    const auto outcome=Json{{"human_state","expired"},{"request_id",h.id}}.dump();const auto settled=impl_->event(root.id,"plan.human.settled",outcome);changed_one(db.execute("UPDATE dynamic_plan_nodes SET state='settled',outcome_json=?,settled_event_seq=? WHERE plan_id=? AND label=? AND human_request_id=? AND state='waiting_human'",{outcome,settled.sequence,p.id,h.label,h.id}));
    changed_one(db.execute("UPDATE dynamic_plans SET state_sequence=state_sequence+1 WHERE id=? AND state_sequence=?",{p.id,p.state_sequence}));auto result=dynamic_human_request(plan,id);tx.commit();return result;
}
DynamicBudgetSegment Repository::dynamic_budget_segment(const std::string& root){
    const auto rows=impl_->database.execute("SELECT id,ordinal,state,active_elapsed_ms,remaining_active_ms,opened_event_seq,closed_event_seq FROM agent_budget_segments WHERE root_run_id=? ORDER BY ordinal DESC LIMIT 1",{root}).rows;if(rows.empty())throw NotFound("Dynamic budget segment not found");const auto& r=rows[0];
    DynamicBudgetSegment s;s.root_run_id=root;s.id=text(r[0]);s.ordinal=integer(r[1]);s.state=text(r[2]);s.active_elapsed_ms=integer(r[3]);s.remaining_active_ms=integer(r[4]);s.opened_event_seq=integer(r[5]);if(!std::holds_alternative<std::nullptr_t>(r[6]))s.closed_event_seq=integer(r[6]);return s;
}
DynamicBudgetSegment Repository::open_dynamic_budget_segment(const DynamicSegmentSpec& spec){
    bounded_identity(spec.id,128);auto& db=impl_->database;Transaction tx(db);const auto caps=dynamic_capabilities(spec.root_run_id);const auto budget=root_budget(spec.root_run_id);if(caps.backend_identity!=spec.backend_identity)throw DynamicPlanUnavailable("Dynamic active segment settings changed");if(run(spec.root_run_id).state!=RunState::running||budget.revision!=spec.expected_budget_revision)throw Conflict("Dynamic segment owner or budget changed");
    const auto old=db.execute("SELECT id,ordinal,state,remaining_active_ms FROM agent_budget_segments WHERE root_run_id=? ORDER BY ordinal DESC LIMIT 1",{spec.root_run_id}).rows;std::int64_t ordinal=1,remaining=budget.spec.wall_limit_ms;
    if(!old.empty()){if(text(old[0][0])==spec.id&&text(old[0][2])=="open"){auto s=dynamic_budget_segment(spec.root_run_id);tx.commit();return s;}throw Conflict("Subsequent dynamic active segments require typed resume");}
    if(remaining<1)throw RootBudgetExhausted("Dynamic active time exhausted");const auto e=impl_->event(spec.root_run_id,"budget.segment.opened",Json{{"segment_id",spec.id},{"ordinal",ordinal},{"remaining_active_ms",remaining}}.dump());changed_one(db.execute("INSERT INTO agent_budget_segments(root_run_id,id,ordinal,state,remaining_active_ms,opened_event_seq) VALUES(?,?,?,'open',?,?)",{spec.root_run_id,spec.id,ordinal,remaining,e.sequence}));
    changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=? AND revision=?",{spec.root_run_id,budget.revision}));auto s=dynamic_budget_segment(spec.root_run_id);tx.commit();return s;
}
Run Repository::suspend_dynamic_owner(const DynamicPauseSpec& spec){
    auto& db=impl_->database;Transaction tx(db);auto p=dynamic_plan(spec.plan_id);dynamic_cas(p,spec.expected_revision,spec.expected_state_sequence,spec.backend_identity);const auto root=run(p.root_run_id);const auto budget=root_budget(root.id);const auto call=dynamic_plan_call(spec.plan_call_id);
    if(root.state!=RunState::running||p.state!="active"||call.plan_id!=p.id||call.state!="accepted")throw Conflict("Dynamic pause binding changed");if(budget.revision!=spec.expected_budget_revision)throw DynamicPlanChanged("Dynamic pause budget changed");const auto d=inspect_dynamic_plan(p);
    if(d.halted||!d.ready.empty()||!d.claimed.empty()||d.waiting_human.empty())throw Conflict("Dynamic pause requires an actual human-only waiting frontier");
    if(!db.execute("SELECT id FROM runs WHERE parent_run_id=? AND state IN ('queued','running','paused')",{root.id}).rows.empty()||!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{root.id}).rows.empty()||!db.execute("SELECT o.id FROM operations o JOIN runs r ON r.id=o.run_id WHERE (r.id=? OR r.parent_run_id=?) AND o.state IN ('awaiting_approval','ready','executing','uncertain')",{root.id,root.id}).rows.empty())throw Conflict("Dynamic pause still owns physical work");
    const auto segment=dynamic_budget_segment(root.id);if(segment.id!=spec.segment_id||segment.state!="open"||spec.measured_active_elapsed_ms>=segment.remaining_active_ms)throw RootBudgetExhausted("Dynamic pause has no active time remaining");impl_->close_dynamic_segment(root.id,spec.segment_id,spec.measured_active_elapsed_ms);dynamic_safe_sequence(p.state_sequence);
    changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=? AND revision=?",{root.id,budget.revision}));changed_one(db.execute("UPDATE dynamic_plans SET state='waiting_human',state_sequence=state_sequence+1 WHERE id=? AND revision=? AND state_sequence=?",{p.id,p.revision,p.state_sequence}));
    changed_one(db.execute("UPDATE runs SET state='paused' WHERE id=? AND state='running'",{root.id}));const auto e=impl_->event(root.id,"run.paused",Json{{"reason","dynamic_human_wait"},{"plan_id",p.id},{"plan_call_id",call.id},{"segment_id",spec.segment_id}}.dump());
    changed_one(db.execute("INSERT INTO dynamic_owner_pauses(root_run_id,plan_id,plan_call_id,segment_id,paused_event_seq,state) VALUES(?,?,?,?,?,'paused') ON CONFLICT(root_run_id) DO UPDATE SET plan_id=excluded.plan_id,plan_call_id=excluded.plan_call_id,segment_id=excluded.segment_id,paused_event_seq=excluded.paused_event_seq,state='paused'",{root.id,p.id,call.id,spec.segment_id,e.sequence}));tx.commit();auto result=root;result.state=RunState::paused;return result;
}
DynamicResumeRecord Repository::resume_dynamic_owner(const DynamicResumeSpec& spec){
    bounded_identity(spec.segment_id,128);auto& db=impl_->database;Transaction tx(db);auto p=dynamic_plan(spec.plan_id);dynamic_cas(p,spec.expected_revision,spec.expected_state_sequence,spec.backend_identity);auto root=run(p.root_run_id);const auto budget=root_budget(root.id);
    if(root.state!=RunState::paused||p.state!="waiting_human")throw Conflict("Dynamic paused owner changed");if(budget.revision!=spec.expected_budget_revision)throw DynamicPlanChanged("Dynamic paused budget changed");const auto pause=db.execute("SELECT plan_call_id,segment_id FROM dynamic_owner_pauses WHERE root_run_id=? AND plan_id=? AND state='paused'",{root.id,p.id}).rows;if(pause.empty())throw Conflict("Dynamic paused binding is absent");auto call=dynamic_plan_call(text(pause[0][0]));if(call.state!="accepted"||call.plan_id!=p.id)throw Conflict("Dynamic signed pause call is not resumable");
    const auto old=dynamic_budget_segment(root.id);if(old.state!="closed"||old.id!=text(pause[0][1])||old.remaining_active_ms<1)throw RootBudgetExhausted("Dynamic paused time is unavailable");const auto d=inspect_dynamic_plan(p);if(d.halted||(!d.report_ready&&d.ready.empty()))throw Conflict("Human answers have not made a frontier or report ready");
    if(!db.execute("SELECT id FROM runs WHERE parent_run_id=? AND state IN ('queued','running','paused')",{root.id}).rows.empty()||!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{root.id}).rows.empty()||!db.execute("SELECT o.id FROM operations o JOIN runs r ON r.id=o.run_id WHERE (r.id=? OR r.parent_run_id=?) AND o.state IN ('awaiting_approval','ready','executing','uncertain')",{root.id,root.id}).rows.empty())throw Conflict("Dynamic resume has unfinished physical work");
    dynamic_safe_sequence(p.state_sequence);changed_one(db.execute("UPDATE runs SET state='running' WHERE id=? AND state='paused'",{root.id}));impl_->event(root.id,"run.running",Json{{"reason","dynamic_human_resume"},{"plan_id",p.id},{"plan_call_id",call.id}}.dump());changed_one(db.execute("UPDATE dynamic_plans SET state='active',state_sequence=state_sequence+1 WHERE id=? AND state_sequence=?",{p.id,p.state_sequence}));
    const auto e=impl_->event(root.id,"budget.segment.opened",Json{{"segment_id",spec.segment_id},{"ordinal",old.ordinal+1},{"remaining_active_ms",old.remaining_active_ms}}.dump());changed_one(db.execute("INSERT INTO agent_budget_segments(root_run_id,id,ordinal,state,remaining_active_ms,opened_event_seq) VALUES(?,?,?,'open',?,?)",{root.id,spec.segment_id,old.ordinal+1,old.remaining_active_ms,e.sequence}));changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=? AND revision=?",{root.id,budget.revision}));changed_one(db.execute("UPDATE dynamic_owner_pauses SET state='resumed' WHERE root_run_id=? AND state='paused'",{root.id}));
    root.state=RunState::running;DynamicResumeRecord result{root,dynamic_plan(p.id),dynamic_budget_segment(root.id),call};tx.commit();return result;
}
Run Repository::complete_dynamic_owner(const std::string& root_id,const std::string& assistant,const std::string& segment_id,std::int64_t elapsed){
    (void)object_json(assistant,8*1024*1024);auto& db=impl_->database;Transaction tx(db);auto root=run(root_id);(void)dynamic_capabilities(root.id);if(root.state!=RunState::running)throw Conflict("Dynamic completion owner is not running");
    if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{root.id}).rows.empty())throw Conflict("Dynamic completion still owns transport");impl_->close_dynamic_segment(root.id,segment_id,elapsed);impl_->root_boundary(root,RunState::completed);
    if(impl_->has_operations(root.id,"'awaiting_approval','ready','executing','uncertain'"))throw Conflict("Dynamic root has unresolved effects");impl_->message(root,"assistant",assistant);changed_one(db.execute("UPDATE runs SET state='completed' WHERE id=? AND state='running'",{root.id}));impl_->event(root.id,"conversation.assistant",assistant);impl_->event(root.id,"run.completed","{}");
    db.execute("UPDATE dynamic_plans SET state='completed',state_sequence=state_sequence+1 WHERE root_run_id=? AND state='active'",{root.id});db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=?",{root.id});tx.commit();root.state=RunState::completed;return root;
}
Run Repository::retire_dynamic_owner(const std::string& root_id,RunState next,const std::string& reason,const std::string& segment_id,std::int64_t elapsed){
    if(next!=RunState::failed&&next!=RunState::cancelled)throw std::invalid_argument("Dynamic retirement requires failure or cancellation");(void)object_json(reason);
    // Settlements remain separate typed, once-only transactions. A journal
    // failure aborts retirement; an actual result is never fabricated.
    (void)dynamic_capabilities(root_id);for(const auto& c:owned_children(root_id)){if(!terminal(c.run.state))throw Conflict("Dynamic owner still has a physical child");if(c.kind=="dynamic_agent")settle_dynamic_child(c.run.id);else if(c.kind=="delegated_leaf")settle_delegation_child(c.run.id);}
    for(const auto& b:delegation_batches(root_id))if(b.state=="accepted"||b.state=="working")settle_delegation_batch(b.id);
    auto& db=impl_->database;Transaction tx(db);auto root=run(root_id);if(terminal(root.state)){if(root.state!=next)throw Conflict("Dynamic terminal state differs");tx.commit();return root;}
    if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{root.id}).rows.empty()||!db.execute("SELECT o.id FROM operations o JOIN runs r ON r.id=o.run_id WHERE (r.id=? OR r.parent_run_id=?) AND o.state='executing'",{root.id,root.id}).rows.empty())throw Conflict("Dynamic owner still has a physical transport or effect");
    const auto segments=db.execute("SELECT id FROM agent_budget_segments WHERE root_run_id=? AND state='open'",{root.id}).rows;if(!segments.empty()){if(segment_id.empty())throw Conflict("Dynamic retirement requires measured active closure");impl_->close_dynamic_segment(root.id,segment_id,elapsed);}else if(!segment_id.empty())throw Conflict("Dynamic retirement segment is no longer open");
    auto p=dynamic_plan_for_root(root.id);if(p){for(const auto& h:dynamic_human_requests(p->id))if(h.state=="waiting"){const auto e=impl_->event(root.id,"plan.human.cancelled",Json{{"request_id",h.id},{"plan_id",p->id},{"label",h.label}}.dump());changed_one(db.execute("UPDATE dynamic_human_requests SET state='cancelled',input_event_seq=? WHERE id=? AND state='waiting'",{e.sequence,h.id}));const auto outcome=Json{{"human_state","cancelled"},{"request_id",h.id}}.dump();const auto settled=impl_->event(root.id,"plan.human.settled",outcome);changed_one(db.execute("UPDATE dynamic_plan_nodes SET state='settled',outcome_json=?,settled_event_seq=? WHERE plan_id=? AND label=? AND state='waiting_human'",{outcome,settled.sequence,p->id,h.label}));}
        db.execute("UPDATE dynamic_plan_nodes SET state='cancelled',protected_definition=1 WHERE plan_id=? AND state IN ('pending','blocked')",{p->id});changed_one(db.execute("UPDATE dynamic_plans SET state=?,state_sequence=state_sequence+1,planned_children_reserved=0,planned_humans_reserved=0 WHERE id=?",{state_name(next),p->id}));
    }
    const auto holds=integer(db.execute("SELECT count(*) FROM dynamic_plan_calls WHERE root_run_id=? AND continuation_attempt_id IS NULL AND state IN ('accepted','report_ready','turn_committed')",{root.id}).rows.at(0).at(0));db.execute("UPDATE dynamic_plan_calls SET state='cancelled' WHERE root_run_id=? AND state IN ('accepted','report_ready','turn_committed') AND continuation_attempt_id IS NULL",{root.id});
    changed_one(db.execute("UPDATE agent_execution_budgets SET planned_children_reserved=0,parent_calls_held=parent_calls_held-?,revision=revision+1 WHERE root_run_id=? AND parent_calls_held>=?",{holds,root.id,holds}));db.execute("UPDATE dynamic_owner_pauses SET state='retired' WHERE root_run_id=?",{root.id});impl_->cancel_waiting(root.id);
    changed_one(db.execute("UPDATE runs SET state=? WHERE id=? AND state=?",{state_name(next),root.id,state_name(root.state)}));impl_->event(root.id,"run."+state_name(next),reason);tx.commit();root.state=next;return root;
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
Run Repository::start_incoming_message(const std::string& id,const std::string& context,const std::string& message,const std::string& prompt,const std::string& identity,std::optional<RootBudgetSpec> budget,std::optional<DynamicPlanCapabilities> dynamic){
    identifier(id);object_json(prompt);const auto provider=prompt_context(prompt);const auto data=Json::parse(prompt);if(!data.contains("content")||!data["content"].is_string())throw std::invalid_argument("Incoming prompt must contain text");const auto content=data["content"].get<std::string>();if(content.empty()||content.size()>65536||content.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid incoming text");Transaction transaction(impl_->database);if(const auto replay=incoming_message(message,context,identity,content)){transaction.commit();return *replay;}
    const auto session_id=context.empty()?"ctx_"+id:context;identifier(session_id);auto& db=impl_->database;
    if(context.empty())changed_one(db.execute("INSERT INTO sessions(id,title) VALUES(?,'Inbound agent conversation')",{session_id}));else session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty())throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,prompt}));
    changed_one(db.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{id}));
    changed_one(db.execute("INSERT INTO incoming_messages(message_id,run_id,context_id,identity,content) VALUES(?,?,?,?,?)",{message,id,session_id,Json::parse(identity).dump(),content}));
    if(budget)impl_->initialize_budget(id,prompt,*budget);impl_->initialize_dynamic(id,budget,dynamic);
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
Run Repository::start_prompt_run(const std::string& id,const std::string& session_id,const std::string& prompt_json,std::optional<RootBudgetSpec> budget,std::optional<DynamicPlanCapabilities> dynamic) {
    identifier(id);const auto provider=prompt_context(prompt_json);auto& db=impl_->database;Transaction transaction(db);session(session_id);
    if(!db.execute("SELECT id FROM runs WHERE id=?",{id}).rows.empty()) throw Conflict("Run already exists");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty()) throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,prompt_json}));
    changed_one(db.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));if(budget)impl_->initialize_budget(id,prompt_json,*budget);impl_->initialize_dynamic(id,budget,dynamic);impl_->event(id,"run.queued","{}");transaction.commit();return {id,session_id,RunState::queued,{},{},false,provider};
}
RootBudgetRecord Repository::root_budget(const std::string& id){
    const auto current=run(id);if(!current.parent_id.empty()||current.graph_root)throw std::invalid_argument("Native delegation budget requires an ordinary root");
    const auto rows=impl_->database.execute("SELECT policy_id,policy_revision,workspace_identity,provider_identity_json,max_children,max_parallel,max_model_calls,wall_limit_ms,children_admitted,model_calls_reserved,parent_calls_held,revision FROM agent_execution_budgets WHERE root_run_id=?",{id}).rows;
    if(rows.empty())throw NotFound("Execution budget not found");const auto& r=rows[0];
    RootBudgetRecord result{id,{text(r[0]),integer(r[1]),text(r[2]),text(r[3]),integer(r[4]),integer(r[5]),integer(r[6]),integer(r[7])},integer(r[8]),integer(r[9]),integer(r[10]),integer(r[11])};
    result.planned_children_reserved=integer(impl_->database.execute("SELECT planned_children_reserved FROM agent_execution_budgets WHERE root_run_id=?",{id}).rows.at(0).at(0));
    result.parent_model_calls_reserved=integer(impl_->database.execute("SELECT count(*) FROM agent_model_call_reservations WHERE root_run_id=? AND role='parent'",{id}).rows.at(0).at(0));
    (void)budget_provider_identity(result.spec);return result;
}
ModelCallReservation Repository::reserve_model_call(const std::string& root,const std::string& owner,const std::string& attempt,ModelCallRole role){
    bounded_identity(attempt);const auto role_name=std::string(call_role(role));auto& db=impl_->database;Transaction transaction(db);
    const auto budget=root_budget(root);const auto parent=run(root),actor=run(owner);
    if(parent.state!=RunState::running || actor.state!=RunState::running)throw Conflict("Model attempt requires its running execution owners");
    if(role==ModelCallRole::parent){if(owner!=root)throw Conflict("Parent model attempt owner differs");}
    else {const auto admitted=child_admission(owner);if((admitted.kind!=ChildAdmissionKind::delegated_leaf&&admitted.kind!=ChildAdmissionKind::dynamic_agent)||admitted.root_run_id!=root)throw Conflict("Leaf model attempt is not owned by this root");
        if(admitted.kind==ChildAdmissionKind::dynamic_agent){const auto p=dynamic_plan(admitted.plan_id);const auto n=std::find_if(p.nodes.begin(),p.nodes.end(),[&](const auto& v){return v.child_run_id==owner;});const auto& preset=dynamic_preset(p.capabilities,n->definition);const auto calls=integer(db.execute("SELECT count(*) FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=?",{root,owner}).rows.at(0).at(0));if(calls>=preset.turn_limit&&db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{root,attempt}).rows.empty())throw RootBudgetExhausted("Durable dynamic child-turn allowance exhausted");}}
    const auto previous=db.execute("SELECT owner_run_id,role,state FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{root,attempt}).rows;
    if(!previous.empty()){if(text(previous[0][0])!=owner||text(previous[0][1])!=role_name)throw Conflict("Model attempt identity changed");transaction.commit();return {root,attempt,owner,role,text(previous[0][2])};}
    const auto dynamic=budget.spec.policy_id=="native.dynamic-plan";
    if(dynamic){const auto caps=dynamic_capabilities(root);if(!caps.catalogue_finalized)throw DynamicPlanUnavailable("Dynamic catalogue is not finalized");
        if(role==ModelCallRole::parent&&!db.execute("SELECT id FROM dynamic_plan_calls WHERE root_run_id=? AND state IN ('accepted','report_ready') OR (root_run_id=? AND state='turn_committed' AND continuation_attempt_id IS NULL)",{root,root}).rows.empty())throw Conflict("Parent must use its exact dynamic continuation");}
    const auto signed_holds=integer(db.execute("SELECT count(*) FROM dynamic_plan_calls WHERE root_run_id=? AND state IN ('accepted','report_ready','turn_committed') AND continuation_attempt_id IS NULL",{root}).rows.at(0).at(0));
    const auto held=role==ModelCallRole::parent&&budget.parent_calls_held>signed_holds?1:0;
    if(dynamic&&role==ModelCallRole::parent&&budget.parent_model_calls_reserved+budget.parent_calls_held-held>=dynamic_capabilities(root).max_parent_turns)throw RootBudgetExhausted("Durable parent-turn allowance exhausted");
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
    if(role==ModelCallRole::parent){if(root!=owner)throw Conflict("Invalid parent attempt owner");}else {const auto admitted=child_admission(owner);if((admitted.kind!=ChildAdmissionKind::delegated_leaf&&admitted.kind!=ChildAdmissionKind::dynamic_agent)||admitted.root_run_id!=root)throw Conflict("Invalid leaf attempt owner");}
    changed_one(db.execute("UPDATE agent_model_call_reservations SET state='started' WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND state='reserved'",{root,attempt,owner}));
    impl_->event(owner,"budget.model_call.started",Json{{"root_run_id",root},{"attempt_id",attempt},{"role",call_role(role)}}.dump());transaction.commit();return {root,attempt,owner,role,"started"};
}
ModelCallReservation Repository::finish_model_call(const std::string& root,const std::string& owner,const std::string& attempt,std::optional<std::string> actual_assistant_json){
    if(actual_assistant_json)(void)object_json(*actual_assistant_json,8*1024*1024);
    auto& db=impl_->database;Transaction transaction(db);const auto rows=db.execute("SELECT role,state FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=? AND owner_run_id=?",{root,attempt,owner}).rows;
    if(rows.empty())throw NotFound("Owned model attempt not found");const auto role=text(rows[0][0])=="parent"?ModelCallRole::parent:ModelCallRole::leaf;const auto state=text(rows[0][1]);
    if(state=="finished"){const auto previous=db.execute("SELECT COALESCE(actual_assistant_json,'') FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{root,attempt}).rows;if(actual_assistant_json&&text(previous[0][0])!=*actual_assistant_json)throw Conflict("Actual completed model response differs");transaction.commit();return {root,attempt,owner,role,state};}
    if(state!="reserved"&&state!="started")throw Conflict("Interrupted attempt cannot be adopted");
    if(actual_assistant_json){if(state!="started")throw Conflict("Undispatched reservation cannot acquire a response");changed_one(db.execute("UPDATE agent_model_call_reservations SET state='finished',actual_assistant_json=? WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND state='started'",{*actual_assistant_json,root,attempt,owner}));}
    else changed_one(db.execute("UPDATE agent_model_call_reservations SET state='finished' WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND state=?",{root,attempt,owner,state}));
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
    if(budget.spec.policy_id=="native.dynamic-plan"&&!db.execute("SELECT id FROM dynamic_plan_calls WHERE root_run_id=? AND (state IN ('accepted','report_ready') OR (state='turn_committed' AND continuation_attempt_id IS NULL))",{parent.id}).rows.empty())throw Conflict("Delegation cannot bypass outstanding signed planning work");
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
    if(budget.children_admitted+budget.planned_children_reserved+static_cast<std::int64_t>(spec.tasks.size())>budget.spec.max_children || budget.model_calls_reserved+budget.parent_calls_held>=budget.spec.max_model_calls)throw RootBudgetExhausted("Delegation cannot preserve its child and parent allowances");
    if(budget.spec.policy_id=="native.dynamic-plan"&&budget.parent_model_calls_reserved+budget.parent_calls_held>=dynamic_capabilities(parent.id).max_parent_turns)throw RootBudgetExhausted("Delegation cannot preserve its parent-turn allowance");
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
    const auto dynamic=impl_->database.execute("SELECT n.plan_id,n.label,n.claim_id,n.definition_revision,n.claim_revision,n.backend_node_id FROM dynamic_plan_nodes n JOIN dynamic_plans p ON p.id=n.plan_id WHERE n.child_run_id=? AND p.root_run_id=?",{id,parent.id}).rows;
    if(!dynamic.empty()){const auto& r=dynamic[0];auto p=dynamic_plan(text(r[0]));const auto n=std::find_if(p.nodes.begin(),p.nodes.end(),[&](const auto& v){return v.child_run_id==id;});if(n==p.nodes.end()||n->definition.kind!=DynamicNodeKind::agent||n->backend_node_id!=child.node_id||n->claim_revision!=n->definition_revision||n->claim_id.empty()||text(r[5])!=child.node_id)throw Conflict("Dynamic child differs from its accepted claim");
        const auto& preset=dynamic_preset(p.capabilities,n->definition);private_identity(preset.backend_identity);(void)root_budget(parent.id);ChildAdmissionRecord a;a.kind=ChildAdmissionKind::dynamic_agent;a.root_run_id=parent.id;a.preset_id=preset.id;a.preset_revision=preset.revision;a.plan_id=p.id;a.node_label=n->definition.label;a.claim_id=n->claim_id;a.definition_revision=n->definition_revision;a.claim_revision=n->claim_revision;a.backend_identity=preset.backend_identity;return a;}
    const auto rows=impl_->database.execute("SELECT b.id,b.root_run_id,b.preset_id,b.preset_revision,b.state,t.node_id FROM delegation_tasks t JOIN delegation_batches b ON b.id=t.batch_id WHERE t.child_run_id=? AND b.parent_run_id=?",{id,parent.id}).rows;
    if(rows.empty())throw Conflict("Child has no accepted native delegation");const auto& r=rows[0];
    if(text(r[1])!=parent.id||text(r[5])!=child.node_id||text(r[2])!="workspace.inspect"||integer(r[3])!=1)throw Conflict("Delegated child ownership or preset differs");
    (void)root_budget(parent.id);return {ChildAdmissionKind::delegated_leaf,parent.id,text(r[0]),text(r[2]),integer(r[3])};
}
std::vector<OwnedChildRecord> Repository::owned_children(const std::string& id){
    const auto parent=run(id);if(!parent.parent_id.empty())throw std::invalid_argument("Owned child observation requires a root parent");std::vector<OwnedChildRecord> result;
    for(const auto& row:impl_->database.execute("SELECT id FROM runs WHERE parent_run_id=? ORDER BY rowid",{id}).rows){const auto child=run(text(row[0]));const auto admission=child_admission(child.id);std::string label;
        if(admission.kind==ChildAdmissionKind::delegated_leaf){const auto task=impl_->database.execute("SELECT task_id FROM delegation_tasks WHERE child_run_id=?",{child.id}).rows;label=text(task.at(0).at(0));}
        result.push_back({child,admission.kind==ChildAdmissionKind::dynamic_agent?"dynamic_agent":admission.kind==ChildAdmissionKind::delegated_leaf?"delegated_leaf":admission.kind==ChildAdmissionKind::graph_agent?"graph_agent":"graph_tool",admission.batch_id,label,admission.preset_id,admission.preset_revision,admission.plan_id,admission.node_label,admission.claim_id,admission.definition_revision,admission.claim_revision});
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
    if(!db.execute("SELECT id FROM dynamic_plan_calls WHERE root_run_id=? AND state IN ('accepted','report_ready')",{id}).rows.empty())throw Conflict("Signed planning turns require their typed commit");
    const auto actual=Json::parse(object_json(assistant_json,8*1024*1024));if(actual.contains("tool_calls"))for(const auto& call:actual["tool_calls"])if(call.value("name",std::string{})=="plan_tasks"||call.value("name",std::string{})=="revise_plan")throw Conflict("Planning conversation turns require their accepted signed ledger");
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
    identifier(kind); if(kind.rfind("run.",0)==0 || kind.rfind("operation.",0)==0 || kind.rfind("delegation.",0)==0 || kind.rfind("budget.",0)==0 || kind.rfind("plan.",0)==0) throw std::invalid_argument("Lifecycle events require their owning repository operation");
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
    const auto owner=run(spec.run_id);if(!owner.parent_id.empty()){const auto admission=child_admission(owner.id);if(admission.kind==ChildAdmissionKind::dynamic_agent){const auto p=dynamic_plan(admission.plan_id);const auto n=std::find_if(p.nodes.begin(),p.nodes.end(),[&](const auto& v){return v.child_run_id==owner.id;});if(n==p.nodes.end()||p.root_run_id!=admission.root_run_id||p.root_run_id!=owner.parent_id||run(p.root_run_id).state!=RunState::running||n->backend_node_id!=owner.node_id||n->definition.label!=admission.node_label||n->claim_id!=admission.claim_id||n->definition_revision!=admission.definition_revision||n->claim_revision!=admission.claim_revision)throw Conflict("Dynamic child effect differs from its actual native ownership");dynamic_effect_authority(spec,p,*n,dynamic_preset(p.capabilities,n->definition));}}
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
    auto roots=db.execute("SELECT id FROM runs WHERE state IN ('queued','running')").rows;
    // Only a fully closed, typed human pause survives. A paused run without
    // this complete proof is interrupted rather than silently adopted.
    for(const auto& row:db.execute("SELECT r.id FROM runs r JOIN dynamic_capabilities a ON a.root_run_id=r.id WHERE r.state='paused'").rows){
        const auto& id=text(row[0]);const auto clean=db.execute("SELECT p.id FROM dynamic_owner_pauses h JOIN dynamic_plans p ON p.id=h.plan_id JOIN dynamic_plan_calls c ON c.id=h.plan_call_id JOIN agent_budget_segments s ON s.id=h.segment_id JOIN dynamic_capabilities a ON a.root_run_id=h.root_run_id WHERE h.root_run_id=? AND h.state='paused' AND p.root_run_id=h.root_run_id AND p.state='waiting_human' AND c.root_run_id=h.root_run_id AND c.plan_id=p.id AND c.state='accepted' AND c.result_json IS NULL AND c.conversation_commit_seq IS NULL AND c.continuation_attempt_id IS NULL AND s.root_run_id=h.root_run_id AND s.state='closed' AND s.remaining_active_ms>0 AND a.catalogue_finalized=1 AND NOT EXISTS(SELECT 1 FROM runs j WHERE j.parent_run_id=h.root_run_id AND j.state IN ('queued','running','paused')) AND NOT EXISTS(SELECT 1 FROM dynamic_plan_nodes n WHERE n.plan_id=p.id AND n.state IN ('claimed','uncertain')) AND NOT EXISTS(SELECT 1 FROM agent_budget_segments b WHERE b.root_run_id=h.root_run_id AND b.ordinal>s.ordinal) AND NOT EXISTS(SELECT 1 FROM agent_model_call_reservations m WHERE m.root_run_id=h.root_run_id AND m.state IN ('reserved','started')) AND NOT EXISTS(SELECT 1 FROM operations o JOIN runs j ON j.id=o.run_id WHERE (j.id=h.root_run_id OR j.parent_run_id=h.root_run_id) AND o.state IN ('awaiting_approval','ready','executing','uncertain')) AND NOT EXISTS(SELECT 1 FROM delegation_batches b WHERE b.parent_run_id=h.root_run_id AND b.state IN ('accepted','working'))",{id}).rows;
        if(clean.empty())roots.push_back(row);
    }
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
    for(const auto& row:roots)changed_one(db.execute("UPDATE runs SET state='failed' WHERE id=? AND state IN ('queued','running','paused')",{text(row[0])}));
    for(const auto& row:roots) impl_->event(text(row[0]),"run.failed",R"({"reason":"server_restart"})");
    for(const auto& row:db.execute("SELECT id,parent_run_id FROM delegation_batches WHERE state IN ('accepted','working')").rows){
        changed_one(db.execute("UPDATE delegation_batches SET state='interrupted' WHERE id=? AND state IN ('accepted','working')",{text(row[0])}));
        impl_->event(text(row[1]),"delegation.batch.interrupted",Json{{"batch_id",text(row[0])},{"reason","server_restart"},{"children_replayed",false}}.dump());
    }
    for(const auto& row:db.execute("SELECT root_run_id,attempt_id,owner_run_id,role,state FROM agent_model_call_reservations WHERE state IN ('reserved','started')").rows){
        changed_one(db.execute("UPDATE agent_model_call_reservations SET state='interrupted' WHERE root_run_id=? AND attempt_id=? AND state=?",{text(row[0]),text(row[1]),text(row[4])}));impl_->event(text(row[2]),"budget.model_call.interrupted",Json{{"root_run_id",text(row[0])},{"attempt_id",text(row[1])},{"role",text(row[3])},{"was_started",text(row[4])=="started"},{"reason","server_restart"}}.dump());
    }
    for(const auto& row:db.execute("SELECT s.root_run_id,s.id FROM agent_budget_segments s JOIN runs r ON r.id=s.root_run_id WHERE s.state='open'").rows){const auto e=impl_->event(text(row[0]),"budget.segment.interrupted",Json{{"segment_id",text(row[1])},{"reason","server_restart"},{"elapsed_known",false}}.dump());changed_one(db.execute("UPDATE agent_budget_segments SET state='interrupted',closed_event_seq=? WHERE id=? AND state='open'",{e.sequence,text(row[1])}));}
    for(const auto& row:db.execute("SELECT p.id,p.root_run_id FROM dynamic_plans p JOIN runs r ON r.id=p.root_run_id WHERE r.state='failed' AND (p.state IN ('active','waiting_human') OR (p.state='uncertain' AND (p.planned_children_reserved>0 OR p.planned_humans_reserved>0 OR EXISTS(SELECT 1 FROM dynamic_plan_nodes n WHERE n.plan_id=p.id AND n.state IN ('pending','blocked','claimed','waiting_human')) OR EXISTS(SELECT 1 FROM dynamic_plan_calls c WHERE c.plan_id=p.id AND c.state IN ('accepted','report_ready','turn_committed') AND c.continuation_attempt_id IS NULL))))").rows){
        const auto plan=text(row[0]),root=text(row[1]);bool uncertain=!db.execute("SELECT label FROM dynamic_plan_nodes WHERE plan_id=? AND (state='uncertain' OR effect_state='uncertain')",{plan}).rows.empty();
        for(const auto& n:db.execute("SELECT label,child_run_id FROM dynamic_plan_nodes WHERE plan_id=? AND state='claimed'",{plan}).rows){
            const auto child=text(n[1]);const auto actual=run(child);if(!terminal(actual.state))throw DatabaseError("Recovered dynamic child remains active");const auto effect=dynamic_effect_state(operations(child));uncertain|=effect=="uncertain";
            Json observed={{"child_run_id",child},{"label",text(n[0])},{"child_state",state_name(actual.state)},{"effect_state",effect},{"error",{{"code","server_restart"}}}};
            if(actual.state==RunState::completed){const auto history=run_history(child);if(history.empty()||history.back().role!="assistant")throw DatabaseError("Recovered completed dynamic child lacks actual history");const auto answer=Json::parse(history.back().json);for(const auto* key:{"content","model","usage","elapsed_ms","first_token_ms"})if(answer.contains(key))observed[key]=answer[key];}
            if(observed.dump().size()>32768)observed={{"child_run_id",child},{"label",text(n[0])},{"child_state",state_name(actual.state)},{"effect_state",effect},{"error",{{"code","result_limit_exceeded"}}},{"history_ref","/v1/runs/"+root+"/children/"+child+"/history"}};
            const auto outcome=observed.dump();const auto e=impl_->event(child,"plan.child.settled",outcome);changed_one(db.execute("UPDATE dynamic_plan_nodes SET state=?,child_state=?,effect_state=?,outcome_json=?,settled_event_seq=? WHERE plan_id=? AND label=? AND state='claimed'",{std::string(effect)=="uncertain"?"uncertain":"settled",state_name(actual.state),effect,outcome,e.sequence,plan,text(n[0])}));
        }
        for(const auto& h:db.execute("SELECT id,label FROM dynamic_human_requests WHERE plan_id=? AND state='waiting'",{plan}).rows){const auto event=impl_->event(root,"plan.human.cancelled",Json{{"request_id",text(h[0])},{"reason","server_restart"}}.dump());changed_one(db.execute("UPDATE dynamic_human_requests SET state='cancelled',input_event_seq=? WHERE id=? AND state='waiting'",{event.sequence,text(h[0])}));const auto outcome=Json{{"human_state","cancelled"},{"request_id",text(h[0])}}.dump();const auto settled=impl_->event(root,"plan.human.settled",outcome);changed_one(db.execute("UPDATE dynamic_plan_nodes SET state='settled',outcome_json=?,settled_event_seq=? WHERE plan_id=? AND label=? AND state='waiting_human'",{outcome,settled.sequence,plan,text(h[1])}));}
        db.execute("UPDATE dynamic_plan_nodes SET state='cancelled',protected_definition=1 WHERE plan_id=? AND state IN ('pending','blocked')",{plan});changed_one(db.execute("UPDATE dynamic_plans SET state=?,state_sequence=state_sequence+1,planned_children_reserved=0,planned_humans_reserved=0 WHERE id=?",{uncertain?"uncertain":"interrupted",plan}));
        const auto holds=integer(db.execute("SELECT count(*) FROM dynamic_plan_calls WHERE root_run_id=? AND continuation_attempt_id IS NULL AND state IN ('accepted','report_ready','turn_committed')",{root}).rows.at(0).at(0));db.execute("UPDATE dynamic_plan_calls SET state='interrupted' WHERE root_run_id=? AND state IN ('accepted','report_ready','turn_committed') AND continuation_attempt_id IS NULL",{root});changed_one(db.execute("UPDATE agent_execution_budgets SET planned_children_reserved=0,parent_calls_held=parent_calls_held-?,revision=revision+1 WHERE root_run_id=? AND parent_calls_held>=?",{holds,root,holds}));db.execute("UPDATE dynamic_owner_pauses SET state='retired' WHERE root_run_id=?",{root});impl_->event(root,"plan.interrupted",Json{{"plan_id",plan},{"reason","server_restart"},{"replayed",false}}.dump());
    }
    for(const auto& row:db.execute("SELECT run_id FROM graph_roots").rows){auto root=graph_run(text(row[0]));const GraphCoordinator recovered(GraphPlan(root.specification_json),root.checkpoint_json);if(recovered.checkpoint()!=root.checkpoint_json){impl_->graph_checkpoint(root,recovered);impl_->event(root.run.id,"graph.recovered",Json{{"checkpoint_revision",root.checkpoint_revision},{"reason","interrupted_nodes_not_replayed"}}.dump());}}
    transaction.commit(); return roots.size();
}
}
