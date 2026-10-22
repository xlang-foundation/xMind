#include "agentflow/repository.hpp"
#include "agentflow/json_schema.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/graph.hpp"
#include "agentflow/dynamic_plan.hpp"
#include "agentflow/context_selection.hpp"
#include "agentflow/responses_context.hpp"
#include "agentflow/legacy_owner.hpp"
#include "agentflow/agent_definition_records.hpp"
#include "agentflow/agent_model_values.hpp"
#include <algorithm>
#include <limits>
#include <chrono>
#include <map>
#include <set>
#include <type_traits>
#include "nlohmann/json.hpp"

namespace agentflow {
namespace {
using Json=nlohmann::json;
bool ordinary_worker_policy(const std::string& policy){return policy=="native.direct-agent"||policy=="native.context"||policy=="native.delegation";}
Json worker_provider_identity(Repository&,const std::string&,const std::string&);
bool worker_delegated_owner(Repository&,const AgentWorkerLeaseSpec&);
bool worker_graph_owner(Repository&,const AgentWorkerLeaseSpec&);
bool worker_uncounted_graph_owner(Repository&,const AgentWorkerLeaseSpec&);
bool worker_effect_owner(Repository&,const AgentWorkerLeaseSpec&);
bool worker_agent_owner(Repository&,const AgentWorkerLeaseSpec&);
void worker_current_scope(Repository&,XlangSqlite&,const AgentWorkerLeaseSpec&,bool allow_queued_root=false);
std::optional<std::string> worker_attempt_generation(XlangSqlite&,const std::string& root,const std::string& owner,const std::string& attempt);
std::string worker_assistant_receipt(Repository&,XlangSqlite&,const AgentWorkerLeaseSpec&,const std::string& attempt,bool final,bool planning_root=false);
void worker_require_tool_turns(XlangSqlite&,const AgentWorkerLeaseSpec&);
void worker_planning_continuation_authority(Repository&,XlangSqlite&,const DynamicPlanCallRecord&,const AgentWorkerLeaseSpec*);
bool worker_planning_attempt(Repository&,XlangSqlite&,const AgentWorkerLeaseSpec&,const std::string&);
void settle_worker_planning(Repository&,XlangSqlite&,const std::string& root,const std::string& owner,const std::string& attempt,const std::string& assistant,const std::string& accepted,const std::string& rejected);
BackendOwnerState decode_owner(const std::string&);
std::optional<std::string> owner_record(XlangSqlite&);
void require_bootstrap(XlangSqlite&,const BackendLease&,const BackendOwnerState&,const BackendOwnerBootstrap&);
BackendOwnerState stage_backend_owner(XlangSqlite&,const BackendLease&,const std::string&,const BackendOwnerBootstrap*);
BackendOwnerState stage_legacy_owner(XlangSqlite&,const BackendLease&,const std::string&,const LegacyOwnerBootstrap&);
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
std::string skill_selections_json(const SkillSelections& value){
    validate_skill_selections(value);const std::set<std::string> ids(value.ids.begin(),value.ids.end());
    Json result{{"version",1},{"workspace_id",value.workspace_id},{"ids",std::vector<std::string>(ids.begin(),ids.end())}};
    if(!value.manual_ids.empty()){const std::set<std::string> manual(value.manual_ids.begin(),value.manual_ids.end());result["manual_ids"]=std::vector<std::string>(manual.begin(),manual.end());}return result.dump();
}
SkillSelections skill_selections(const std::string& source){
    const auto value=Json::parse(object_json(source,8192));
    if((value.size()!=3&&value.size()!=4)||(value.size()==4&&!value.contains("manual_ids"))||!value.contains("version")||!value["version"].is_number_integer()||value["version"]!=1||!value.contains("workspace_id")||!value["workspace_id"].is_string()||!value.contains("ids")||!value["ids"].is_array()||value["ids"].size()>8)throw DatabaseError("Invalid saved native skill selection");
    SkillSelections result{value["workspace_id"].get<std::string>(),{}};for(const auto& id:value["ids"]){if(!id.is_string())throw DatabaseError("Invalid saved native skill id");result.ids.push_back(id.get<std::string>());}
    if(value.contains("manual_ids")){if(!value["manual_ids"].is_array()||value["manual_ids"].size()>8)throw DatabaseError("Invalid saved manual skills");for(const auto& id:value["manual_ids"]){if(!id.is_string())throw DatabaseError("Invalid saved manual skill id");result.manual_ids.push_back(id.get<std::string>());}}
    validate_skill_selections(result);return result;
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
    const bool context=spec.policy_id=="native.context",graph=spec.policy_id=="native.graph-context",direct=spec.policy_id=="native.direct-agent";
    if((!context&&!graph&&!direct&&spec.policy_id!="native.delegation" && spec.policy_id!="native.dynamic-plan") || spec.policy_revision!=1 || spec.workspace_identity.empty() || spec.workspace_identity.size()>4096 || spec.workspace_identity.find('\0')!=std::string::npos ||
        ((context||direct)?(spec.max_children!=0||spec.max_parallel!=0):(spec.max_children<1 || spec.max_children>8 || spec.max_parallel<1 || spec.max_parallel>2)) || spec.max_model_calls<1 || spec.max_model_calls>32 || spec.wall_limit_ms<1 || spec.wall_limit_ms>3600000 || spec.provider_identity_json.size()>4096)
        throw std::invalid_argument("Invalid native execution budget policy");
    const auto identity=Json::parse(object_json(spec.provider_identity_json));
    if(identity.size()==6){(void)provider_context(identity);return identity;}
    const bool dialect=identity.size()==3&&identity.contains("chat_dialect");
    if((identity.size()!=2&&!dialect) || !identity.contains("wire") || !identity["wire"].is_string() || !identity.contains("model_id") || !identity["model_id"].is_string())throw std::invalid_argument("Invalid public execution provider identity");
    if(dialect&&(!identity["chat_dialect"].is_string()||(identity["chat_dialect"]!="deepseek"&&identity["chat_dialect"]!="mistral")||identity["wire"]!="chat-completions"))throw std::invalid_argument("Invalid public execution chat dialect");
    const auto model=identity.at("model_id").get<std::string>();if(model.empty()||model.size()>256||model.starts_with("sk-")||model.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)throw std::invalid_argument("Invalid execution model identity");
    const auto wire=identity.at("wire").get<std::string>();if(wire!="chat-completions"&&wire!="responses"&&wire!="anthropic-messages"&&wire!="gemini-generate-content")throw std::invalid_argument("Invalid execution provider wire");
    return identity;
}
const char* call_role(ModelCallRole role){switch(role){case ModelCallRole::parent:return "parent";case ModelCallRole::leaf:return "leaf";}throw std::invalid_argument("Invalid model attempt role");}
void changed_one(const SqlResult& result);
const char* context_scope_name(ContextScopeKind kind){switch(kind){case ContextScopeKind::session:return "session";case ContextScopeKind::execution:return "execution";}throw std::invalid_argument("Invalid context scope");}
const char* group_name(ConversationGroupKind kind){switch(kind){case ConversationGroupKind::user:return "user";case ConversationGroupKind::assistant:return "assistant";case ConversationGroupKind::tool_turn:return "tool_turn";}throw std::invalid_argument("Invalid conversation group");}
const char* context_failure(ContextFailureCode code){switch(code){case ContextFailureCode::cancelled:return "cancelled";case ContextFailureCode::deadline:return "deadline";case ContextFailureCode::provider_failure:return "provider_failure";case ContextFailureCode::invalid_result:return "invalid_result";case ContextFailureCode::capacity_exceeded:return "capacity_exceeded";case ContextFailureCode::source_changed:return "source_changed";case ContextFailureCode::binding_changed:return "binding_changed";case ContextFailureCode::interrupted:return "interrupted";case ContextFailureCode::native_failure:return "native_failure";}throw std::invalid_argument("Invalid context outcome");}
const char* context_purpose(ModelCallPurpose purpose){switch(purpose){case ModelCallPurpose::inference:return "inference";case ModelCallPurpose::context_compaction:return "context_compaction";}throw std::invalid_argument("Invalid model purpose");}
bool context_usage_has_output(const std::string& raw){const auto usage=Json::parse(object_json(raw));bool output=false;const auto counter=[&](const Json& object,const char* field){if(!object.contains(field))return;if(!object[field].is_number_integer()||object[field]<0||object[field]>9007199254740991LL)throw std::invalid_argument("Invalid actual output usage counter");output|=object[field]>0;};for(const auto* field:{"output_tokens","completion_tokens","reasoning_tokens","candidatesTokenCount","thoughtsTokenCount"})counter(usage,field);for(const auto* field:{"output_tokens_details","completion_tokens_details"})if(usage.contains(field)){if(!usage[field].is_object())throw std::invalid_argument("Invalid actual output usage details");for(const auto* key:{"reasoning_tokens","audio_tokens","accepted_prediction_tokens","rejected_prediction_tokens"})counter(usage[field],key);}return output;}
Json context_binding_json(const ContextBinding& b){
    if(b.authority_identity.size()!=64||b.authority_identity.find_first_not_of("0123456789abcdef")!=std::string::npos||b.policy_id!="native.context"||b.policy_revision!=1||b.strategy_id!="responses.compact"||b.strategy_revision!=1)throw std::invalid_argument("Invalid registered context binding");
    RootBudgetSpec probe;probe.policy_id="native.context";probe.max_children=probe.max_parallel=0;probe.workspace_identity="workspace:none";probe.provider_identity_json=b.provider_identity_json;const auto p=budget_provider_identity(probe);if(p["wire"]!="responses")throw ContextUnavailable("Context strategy is not registered for this wire");
    return {{"provider_identity_json",b.provider_identity_json},{"authority_identity",b.authority_identity},{"policy_id",b.policy_id},{"policy_revision",b.policy_revision},{"strategy_id",b.strategy_id},{"strategy_revision",b.strategy_revision}};
}
ContextBinding decode_context_binding(const std::string& source){const auto j=Json::parse(source);ContextBinding b{j.at("provider_identity_json").get<std::string>(),j.at("authority_identity").get<std::string>(),j.at("policy_id").get<std::string>(),j.at("policy_revision").get<std::int64_t>(),j.at("strategy_id").get<std::string>(),j.at("strategy_revision").get<std::int64_t>()};if(context_binding_json(b)!=j)throw DatabaseError("Saved context binding differs");return b;}
std::string context_binding_key(const ContextBinding& binding){return context_digest(context_binding_json(binding).dump());}
void context_read_bound(const ContextReadBound& b){if(b.max_groups<1||b.max_groups>4096||b.max_metadata_bytes<1||b.max_metadata_bytes>1024*1024||b.max_payload_bytes<1||b.max_payload_bytes>8*1024*1024)throw std::invalid_argument("Invalid context read bounds");}
Json context_policy_json(const ContextPolicy& p){if(p.id!="native.context"||p.revision!=1||p.strategy_id!="responses.compact"||p.strategy_revision!=1||p.max_groups<1||p.max_groups>4096||p.max_messages<1||p.max_messages>8192||p.max_request_bytes<1||p.max_request_bytes>8*1024*1024||p.max_checkpoint_bytes<1||p.max_checkpoint_bytes>4*1024*1024||p.max_maintenance_calls<1||p.max_maintenance_calls>4||p.max_count_requests<1||p.max_count_requests>32||p.maintenance_deadline_ms<1||p.maintenance_deadline_ms>120000||p.retain_recent_groups<1||p.retain_recent_groups>p.max_groups)throw std::invalid_argument("Invalid registered context policy");return {{"id",p.id},{"revision",p.revision},{"strategy_id",p.strategy_id},{"strategy_revision",p.strategy_revision},{"automatic",p.automatic},{"max_groups",p.max_groups},{"max_messages",p.max_messages},{"max_request_bytes",p.max_request_bytes},{"max_checkpoint_bytes",p.max_checkpoint_bytes},{"max_maintenance_calls",p.max_maintenance_calls},{"max_count_requests",p.max_count_requests},{"maintenance_deadline_ms",p.maintenance_deadline_ms},{"retain_recent_groups",p.retain_recent_groups}};}
Json context_group_json(const ConversationGroup& g){return {{"ordinal",g.ordinal},{"first",g.first_message_seq},{"last",g.last_message_seq},{"owner",g.execution_owner?Json(*g.execution_owner):Json(nullptr)},{"event",g.committed_event_seq?Json(*g.committed_event_seq):Json(nullptr)},{"kind",group_name(g.kind)},{"messages",g.message_count},{"bytes",g.payload_bytes},{"source",g.source_binding},{"legacy",g.legacy_unattributed}};}
Json context_snapshot_json(const ContextSnapshot& s){Json groups=Json::array(),users=Json::array();for(const auto& g:s.groups)groups.push_back(context_group_json(g));for(const auto& g:s.protected_users)users.push_back(context_group_json(g));return {{"scope_kind",context_scope_name(s.head.scope.kind)},{"scope_id",s.head.scope.id},{"binding",context_binding_json(s.head.binding)},{"revision",s.head.revision},{"covered",s.head.covered_through_ordinal},{"checkpoint",s.head.checkpoint_id?Json(*s.head.checkpoint_id):Json(nullptr)},{"watermark",s.source_watermark},{"latest_user",s.latest_user_ordinal},{"groups",groups},{"users",users}};}
ContextAdmission context_admission(const std::string& source){const auto j=Json::parse(source);const auto kind=j.at("kind").get<std::string>();if(kind=="idle")return IdleContextAdmission{j.at("owner").get<std::string>(),j.at("max_calls").get<std::int64_t>(),j.at("wall_ms").get<std::int64_t>()};if(kind=="planning")return PlanningContextAdmission{j.at("root").get<std::string>(),j.at("owner").get<std::string>(),j.at("step").get<std::string>(),j.at("attempt").get<std::string>(),j.at("call").get<std::string>(),j.at("budget_revision").get<std::int64_t>()};if(kind=="active")return ActiveContextAdmission{j.at("root").get<std::string>(),j.at("owner").get<std::string>(),j.at("step").get<std::string>(),j.at("attempt").get<std::string>(),j.at("role")=="parent"?ModelCallRole::parent:ModelCallRole::leaf,j.at("budget_revision").get<std::int64_t>()};if(kind=="rebuild")return RebuildContextAdmission{j.at("root").get<std::string>(),j.at("owner").get<std::string>(),j.at("step").get<std::string>(),j.at("failed").get<std::string>(),j.at("attempt").get<std::string>(),j.at("role")=="parent"?ModelCallRole::parent:ModelCallRole::leaf,j["call"].is_null()?std::optional<std::string>{}:std::optional<std::string>{j["call"].get<std::string>()},j.at("budget_revision").get<std::int64_t>()};throw DatabaseError("Invalid saved context admission");}
Json context_admission_json(const ContextAdmission& a){return std::visit([](const auto& v)->Json{using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,IdleContextAdmission>)return {{"kind","idle"},{"owner",v.owner_id},{"max_calls",v.max_model_calls},{"wall_ms",v.wall_limit_ms}};else {Json j={{"kind",std::is_same_v<T,PlanningContextAdmission>?"planning":std::is_same_v<T,RebuildContextAdmission>?"rebuild":"active"},{"root",v.root_run_id},{"owner",v.owner_run_id},{"step",v.inference_step_id},{"budget_revision",v.expected_budget_revision}};if constexpr(std::is_same_v<T,PlanningContextAdmission>){j["attempt"]=v.held_attempt_id;j["call"]=v.plan_call_id;}else if constexpr(std::is_same_v<T,RebuildContextAdmission>){j["attempt"]=v.retry_attempt_id;j["failed"]=v.failed_attempt_id;j["role"]=call_role(v.role);j["call"]=v.planning_call_id?Json(*v.planning_call_id):Json(nullptr);}else{j["attempt"]=v.inference_attempt_id;j["role"]=call_role(v.role);}return j;}},a);}
void ensure_context_scope(XlangSqlite& db,const ContextScope& s){bounded_identity(s.id,256);const auto kind=context_scope_name(s.kind);const auto rows=s.kind==ContextScopeKind::session?db.execute("SELECT id FROM sessions WHERE id=? AND NOT EXISTS(SELECT 1 FROM deleted_sessions d WHERE d.id=sessions.id)",{s.id}).rows:db.execute("SELECT id FROM runs WHERE id=? AND parent_run_id IS NOT NULL",{s.id}).rows;if(rows.empty())throw NotFound("Context scope does not exist");db.execute("INSERT INTO context_scopes(kind,id) VALUES(?,?) ON CONFLICT(kind,id) DO NOTHING",{std::string(kind),s.id});}
std::vector<Message> context_originals(XlangSqlite& db,const ContextScope& s,std::int64_t after,std::size_t limit){std::vector<Message> result;const auto sql=s.kind==ContextScopeKind::session?"SELECT seq,role,payload FROM messages WHERE session_id=? AND execution_run_id IS NULL AND seq>? ORDER BY seq LIMIT ?":"SELECT seq,role,payload FROM messages WHERE execution_run_id=? AND seq>? ORDER BY seq LIMIT ?";for(const auto& r:db.execute(sql,{s.id,after,static_cast<std::int64_t>(limit)}).rows)result.push_back({integer(r[0]),text(r[1]),text(r[2])});return result;}
std::string context_messages_digest(const std::vector<Message>& messages){std::string framed="xMind.context.group.v1\n";for(const auto& m:messages){for(const auto& value:{std::to_string(m.sequence),m.role,m.json}){framed+=std::to_string(value.size());framed+=':';framed+=value;}}return context_digest(framed);}
ConversationGroupKind validate_context_group(const std::vector<Message>& messages){
    if(messages.empty()||messages.size()>65)throw ContextUnavailable("Conversation group is not bounded");
    for(const auto& m:messages)(void)object_json(m.json,8*1024*1024);
    if(messages[0].role=="user"){if(messages.size()!=1)throw ContextUnavailable("User group has extra results");return ConversationGroupKind::user;}
    if(messages[0].role!="assistant")throw ContextUnavailable("Conversation group has no actual assistant origin");const auto assistant=Json::parse(messages[0].json);
    if(!assistant.contains("tool_calls")||assistant["tool_calls"].empty()){if(messages.size()!=1)throw ContextUnavailable("Assistant group contains dangling results");return ConversationGroupKind::assistant;}
    const auto& calls=assistant["tool_calls"];if(!calls.is_array()||calls.size()>64||messages.size()!=calls.size()+1)throw ContextUnavailable("Tool group is incomplete");std::set<std::string> pending;
    for(const auto& c:calls){if(!c.is_object()||!c.contains("id")||!c["id"].is_string()||!pending.insert(c["id"].get<std::string>()).second)throw ContextUnavailable("Tool group call identity is invalid");}
    for(std::size_t i=1;i<messages.size();++i){const auto t=Json::parse(messages[i].json);if(messages[i].role!="tool"||!t.contains("tool_call_id")||!t["tool_call_id"].is_string()||!pending.erase(t["tool_call_id"].get<std::string>()))throw ContextUnavailable("Tool group result identity differs");}
    if(!pending.empty())throw ContextUnavailable("Tool group is incomplete");return ConversationGroupKind::tool_turn;
}
void insert_context_group(XlangSqlite& db,const ContextScope& s,const std::vector<Message>& messages,std::optional<std::string> owner={},std::optional<std::int64_t> event={},bool legacy=true){
    const auto kind=validate_context_group(messages);ensure_context_scope(db,s);const auto ordinal=integer(db.execute("SELECT next_ordinal FROM context_scopes WHERE kind=? AND id=?",{std::string(context_scope_name(s.kind)),s.id}).rows.at(0).at(0));if(ordinal>=9007199254740991)throw ContextCapacityExceeded("Conversation watermark is exhausted");std::int64_t bytes=0;for(const auto& m:messages){if(m.json.size()>8*1024*1024||bytes>8*1024*1024-static_cast<std::int64_t>(m.json.size()))throw ContextCapacityExceeded("Complete group exceeds payload bound");bytes+=static_cast<std::int64_t>(m.json.size());}
    db.execute("INSERT INTO conversation_groups(scope_kind,scope_id,ordinal,first_message_seq,last_message_seq,execution_owner,committed_event_seq,kind,message_count,payload_bytes,source_binding,legacy_unattributed) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",{std::string(context_scope_name(s.kind)),s.id,ordinal,messages.front().sequence,messages.back().sequence,owner?SqlValue(*owner):SqlValue(nullptr),event?SqlValue(*event):SqlValue(nullptr),std::string(group_name(kind)),static_cast<std::int64_t>(messages.size()),bytes,context_messages_digest(messages),static_cast<std::int64_t>(legacy)});
    for(std::size_t i=0;i<messages.size();++i)db.execute("INSERT INTO context_group_members VALUES(?,?,?,?,?)",{std::string(context_scope_name(s.kind)),s.id,ordinal,static_cast<std::int64_t>(i),messages[i].sequence});
    changed_one(db.execute("UPDATE context_scopes SET next_ordinal=next_ordinal+1,indexed_message_seq=? WHERE kind=? AND id=? AND next_ordinal=?",{messages.back().sequence,std::string(context_scope_name(s.kind)),s.id,ordinal}));
}
void backfill_context_groups(XlangSqlite& db,const ContextScope& s,const ContextReadBound& bound){
    ensure_context_scope(db,s);const auto after=integer(db.execute("SELECT indexed_message_seq FROM context_scopes WHERE kind=? AND id=?",{std::string(context_scope_name(s.kind)),s.id}).rows.at(0).at(0));const auto values=context_originals(db,s,after,8193);if(values.size()>8192)throw ContextCapacityExceeded("Legacy conversation needs a bounded native indexing pass");std::size_t bytes=0;for(const auto& m:values){if(m.json.size()>bound.max_payload_bytes-bytes)throw ContextCapacityExceeded("Legacy conversation payload exceeds indexing bound");bytes+=m.json.size();}
    for(std::size_t i=0;i<values.size();){std::size_t count=1;if(values[i].role=="assistant"){const auto j=Json::parse(object_json(values[i].json,8*1024*1024));if(j.contains("tool_calls")&&!j["tool_calls"].empty()){if(!j["tool_calls"].is_array()||j["tool_calls"].size()>64)throw ContextUnavailable("Legacy tool group is invalid");count+=j["tool_calls"].size();}}if(count>values.size()-i)throw ContextUnavailable("Legacy tool group is unfinished");std::vector<Message> group(values.begin()+i,values.begin()+i+count);insert_context_group(db,s,group);i+=count;}
}
ConversationGroup decode_context_group(const ContextScope& scope,const std::vector<SqlValue>& r){ConversationGroup g;g.scope=scope;g.ordinal=integer(r[0]);g.first_message_seq=integer(r[1]);g.last_message_seq=integer(r[2]);if(!std::holds_alternative<std::nullptr_t>(r[3]))g.execution_owner=text(r[3]);if(!std::holds_alternative<std::nullptr_t>(r[4]))g.committed_event_seq=integer(r[4]);const auto k=text(r[5]);g.kind=k=="user"?ConversationGroupKind::user:k=="assistant"?ConversationGroupKind::assistant:ConversationGroupKind::tool_turn;g.message_count=integer(r[6]);g.payload_bytes=integer(r[7]);g.source_binding=text(r[8]);g.legacy_unattributed=integer(r[9])!=0;return g;}
const char* context_group_columns="ordinal,first_message_seq,last_message_seq,execution_owner,committed_event_seq,kind,message_count,payload_bytes,source_binding,legacy_unattributed";
bool same_responses_history_profile(const std::string& source,const std::string& target){
    auto a=Json::parse(source),b=Json::parse(target);
    // An unprofiled legacy identity is insufficient proof of the provider route.
    // Each full identity was validated at its own context admission boundary.
    if(!a.is_object()||!b.is_object()||!a.contains("profile_id")||!b.contains("profile_id")||
       a.value("wire",std::string{})!="responses"||b.value("wire",std::string{})!="responses")return false;
    a.erase("model_id");b.erase("model_id");return a==b;
}
struct ProvedHistoryReceipt {
    bool has_items=false;
    std::string provider_identity,workspace;
    std::optional<ContextBinding> binding;
};
ProvedHistoryReceipt prove_history_receipt(XlangSqlite& db,const ConversationGroup& group,const Message& original){
    ProvedHistoryReceipt result;if(original.role!="assistant")return result;
    const auto dto=Json::parse(original.json);
    if(dto.contains("provider_items")){if(!dto["provider_items"].is_array())throw ContextBindingChanged("Stored provider receipt shape is incompatible");result.has_items=!dto["provider_items"].empty();}
    if(dto.contains("provider_items_json")&&(!dto["provider_items_json"].is_string()||dto["provider_items_json"].get<std::string>()!="[]"))throw ContextBindingChanged("Legacy opaque receipt has no registered native continuation representation");
    if(!result.has_items)return result;
    if(group.legacy_unattributed||!group.execution_owner||!group.committed_event_seq)throw ContextBindingChanged("Opaque original has no proved producing context owner");
    const auto receipt=db.execute("SELECT r.inference_step_id,m.binding_json,s.spec_json,e.seq,b.workspace_identity FROM agent_model_call_reservations r LEFT JOIN inference_attempt_members m ON m.root_run_id=r.root_run_id AND m.attempt_id=r.attempt_id LEFT JOIN inference_steps s ON s.id=m.step_id JOIN agent_execution_budgets b ON b.root_run_id=r.root_run_id JOIN events e ON e.run_id=r.owner_run_id AND e.kind='budget.model_call.finished' AND json_extract(e.payload,'$.attempt_id')=r.attempt_id AND json_extract(e.payload,'$.was_started')=1 WHERE r.owner_run_id=? AND r.state='finished' AND r.purpose='inference' AND r.actual_assistant_json=? AND e.seq<?",{*group.execution_owner,original.json,*group.committed_event_seq}).rows;
    if(receipt.size()!=1)throw ContextBindingChanged("Opaque original has no unique completed producing model call");
    result.workspace=text(receipt[0][4]);
    if(!std::holds_alternative<std::nullptr_t>(receipt[0][0])){
        if(std::holds_alternative<std::nullptr_t>(receipt[0][1])||std::holds_alternative<std::nullptr_t>(receipt[0][2]))throw DatabaseError("Opaque original inference membership is incomplete");
        const auto binding=decode_context_binding(text(receipt[0][1]));const auto spec=Json::parse(text(receipt[0][2]));
        if(!spec.is_object()||!spec.contains("binding")||decode_context_binding(spec["binding"].dump())!=binding||!spec.contains("scope_kind")||!spec.contains("scope_id")||spec.at("scope_kind")!=context_scope_name(group.scope.kind)||spec.at("scope_id")!=group.scope.id)
            throw ContextBindingChanged("Opaque original belongs to another execution scope or context binding");
        result.binding=binding;result.provider_identity=binding.provider_identity_json;
    }else{
        const auto context=db.execute("SELECT CASE WHEN json_type(m.payload,'$.provider_context') IS NULL THEN '' ELSE json_quote(json_extract(m.payload,'$.provider_context')) END FROM task_messages t JOIN messages m ON m.seq=t.message_seq WHERE t.run_id=? AND m.role='user' ORDER BY m.seq LIMIT 1",{*group.execution_owner}).rows;
        if(context.size()!=1||text(context[0][0]).empty()||!dto.contains("provider_context"))throw ContextBindingChanged("Opaque original lacks a captured provider-profile identity");
        const auto captured=provider_context(Json::parse(text(context[0][0])));
        if(provider_context(dto.at("provider_context"))!=captured||!dto.contains("model")||!dto["model"].is_string()||dto["model"]!=Json::parse(captured).at("model_id"))throw ContextBindingChanged("Opaque original differs from its captured provider-profile identity");
        result.provider_identity=captured;
    }
    if(dto.contains("provider_context")&&provider_context(dto.at("provider_context"))!=result.provider_identity)throw ContextBindingChanged("Opaque original provider context differs from its actual model call");
    const auto event=db.execute("SELECT run_id,kind FROM events WHERE run_id=? AND seq=?",{*group.execution_owner,*group.committed_event_seq}).rows;
    // The group event is scoped to its actual owner, never an arbitrary equal DTO.
    if(event.size()!=1||text(event[0][0])!=*group.execution_owner||(text(event[0][1])!="conversation.assistant"&&text(event[0][1])!="conversation.tool_turn"))throw DatabaseError("Opaque source group lacks its actual committed conversation event");
    return result;
}
bool validate_context_receipts(XlangSqlite& db,const ConversationGroup& group,const std::vector<Message>& originals,const ContextBinding& selected_binding,bool allow_authority_replay=false,std::string* producing_workspace=nullptr,bool* model_changed=nullptr){
    bool reproject=false;
    for(const auto& original:originals){const auto proof=prove_history_receipt(db,group,original);if(!proof.has_items)continue;
        if(proof.binding&&*proof.binding!=selected_binding){
            auto compatible=*proof.binding;compatible.authority_identity=selected_binding.authority_identity;
            const bool changed_model=Json::parse(proof.provider_identity)!=Json::parse(selected_binding.provider_identity_json);
            if(changed_model&&same_responses_history_profile(proof.provider_identity,selected_binding.provider_identity_json))compatible.provider_identity_json=selected_binding.provider_identity_json;
            if(!allow_authority_replay||compatible!=selected_binding)throw ContextBindingChanged("Opaque original belongs to another selected context authority");
            reproject=true;if(model_changed)*model_changed|=changed_model;
            if(producing_workspace){if(proof.workspace.empty()||(!producing_workspace->empty()&&*producing_workspace!=proof.workspace))throw ContextBindingChanged("Opaque originals have incompatible producing workspaces");*producing_workspace=proof.workspace;}
        }
        if(Json::parse(proof.provider_identity)!=Json::parse(selected_binding.provider_identity_json)&&!(allow_authority_replay&&reproject&&same_responses_history_profile(proof.provider_identity,selected_binding.provider_identity_json)))throw ContextBindingChanged("Opaque original belongs to another selected provider identity");
    }
    return reproject;
}
void index_context_event(XlangSqlite& db,const std::string& owner,const std::string& event_kind,std::int64_t event_seq){
    // Existing transcript methods accept older DTO shapes. Such a transcript
    // remains readable, but cannot become a false complete compaction group.
    // A savepoint keeps indexing all-or-nothing without changing that API.
    const auto owner_rows=db.execute("SELECT session_id,parent_run_id FROM runs WHERE id=?",{owner}).rows;if(owner_rows.empty())return;const auto& r=owner_rows[0];ContextScope scope{std::holds_alternative<std::nullptr_t>(r[1])?ContextScopeKind::session:ContextScopeKind::execution,std::holds_alternative<std::nullptr_t>(r[1])?text(r[0]):owner};
    db.execute("SAVEPOINT context_group_index");try {
        ensure_context_scope(db,scope);const auto after=integer(db.execute("SELECT indexed_message_seq FROM context_scopes WHERE kind=? AND id=?",{std::string(context_scope_name(scope.kind)),scope.id}).rows.at(0).at(0));const auto values=context_originals(db,scope,after,8193);if(values.size()>8192)throw ContextCapacityExceeded("Context append needs a bounded legacy indexing pass");std::size_t total=0;for(const auto& m:values){if(m.json.size()>8*1024*1024-total)throw ContextCapacityExceeded("Context indexing exceeds source payload bound");total+=m.json.size();}
        for(std::size_t i=0;i<values.size();){std::size_t count=1;if(values[i].role=="assistant"){const auto j=Json::parse(object_json(values[i].json,8*1024*1024));if(j.contains("tool_calls")&&!j["tool_calls"].empty()){if(!j["tool_calls"].is_array()||j["tool_calls"].size()>64)throw ContextUnavailable("Context tool group is invalid");count+=j["tool_calls"].size();}}if(count>values.size()-i)throw ContextUnavailable("Context tool turn is not complete");std::vector<Message> group(values.begin()+i,values.begin()+i+count);const auto kind=validate_context_group(group);const bool last=i+count==values.size();const bool attributable=last&&((event_kind=="run.queued"&&kind==ConversationGroupKind::user)||(event_kind=="conversation.assistant"&&kind==ConversationGroupKind::assistant)||(event_kind=="conversation.tool_turn"&&kind==ConversationGroupKind::tool_turn));insert_context_group(db,scope,group,attributable?std::optional<std::string>(owner):std::nullopt,attributable?std::optional<std::int64_t>(event_seq):std::nullopt,!attributable);i+=count;}
        db.execute("RELEASE context_group_index");
    }catch(const ContextUnavailable&){db.execute("ROLLBACK TO context_group_index");db.execute("RELEASE context_group_index");}
     catch(const ContextCapacityExceeded&){db.execute("ROLLBACK TO context_group_index");db.execute("RELEASE context_group_index");}
     catch(const std::invalid_argument&){db.execute("ROLLBACK TO context_group_index");db.execute("RELEASE context_group_index");}
     catch(const Json::exception&){db.execute("ROLLBACK TO context_group_index");db.execute("RELEASE context_group_index");}
}
ContextSnapshot read_context_snapshot(XlangSqlite& db,const ContextScope& scope,const ContextBinding& binding,ContextReadBound bound){
    context_read_bound(bound);backfill_context_groups(db,scope,bound);const auto kind=std::string(context_scope_name(scope.kind)),key=context_binding_key(binding),binding_json=context_binding_json(binding).dump();db.execute("INSERT INTO context_heads(scope_kind,scope_id,binding_key,binding_json) VALUES(?,?,?,?) ON CONFLICT(scope_kind,scope_id,binding_key) DO NOTHING",{kind,scope.id,key,binding_json});
    const auto row=db.execute("SELECT revision,covered_ordinal,checkpoint_id,binding_json FROM context_heads WHERE scope_kind=? AND scope_id=? AND binding_key=?",{kind,scope.id,key}).rows.at(0);if(text(row[3])!=binding_json)throw ContextUnavailable("Context authority binding differs");ContextSnapshot s;s.head.scope=scope;s.head.binding=binding;s.head.revision=integer(row[0]);s.head.covered_through_ordinal=integer(row[1]);if(!std::holds_alternative<std::nullptr_t>(row[2]))s.head.checkpoint_id=text(row[2]);s.source_watermark=integer(db.execute("SELECT next_ordinal-1 FROM context_scopes WHERE kind=? AND id=?",{kind,scope.id}).rows.at(0).at(0));const auto last_user=db.execute("SELECT max(ordinal) FROM conversation_groups WHERE scope_kind=? AND scope_id=? AND kind='user'",{kind,scope.id}).rows.at(0).at(0);if(!std::holds_alternative<std::nullptr_t>(last_user))s.latest_user_ordinal=integer(last_user);
    std::size_t bytes=0;for(const auto& r:db.execute(std::string("SELECT ")+context_group_columns+" FROM conversation_groups WHERE scope_kind=? AND scope_id=? AND ordinal>? ORDER BY ordinal LIMIT ?",{kind,scope.id,s.head.covered_through_ordinal,static_cast<std::int64_t>(bound.max_groups+1)}).rows){if(s.groups.size()>=bound.max_groups)throw ContextCapacityExceeded("Context group metadata exceeds its bound");auto g=decode_context_group(scope,r);bytes+=context_group_json(g).dump().size();if(bytes>bound.max_metadata_bytes)throw ContextCapacityExceeded("Context metadata exceeds its byte bound");s.groups.push_back(std::move(g));}
    std::set<std::int64_t> pinned;if(s.latest_user_ordinal)pinned.insert(s.latest_user_ordinal);if(s.head.checkpoint_id){const auto r=db.execute("SELECT c.protected_users_json FROM context_compactions c JOIN context_checkpoints k ON k.compaction_id=c.id WHERE k.id=? AND c.state='committed'",{*s.head.checkpoint_id}).rows;if(r.empty())throw DatabaseError("Context head lacks its committed checkpoint");for(const auto& n:Json::parse(text(r[0][0])))pinned.insert(n.get<std::int64_t>());}
    for(const auto ordinal:pinned){const auto r=db.execute(std::string("SELECT ")+context_group_columns+" FROM conversation_groups WHERE scope_kind=? AND scope_id=? AND ordinal=? AND kind='user'",{kind,scope.id,ordinal}).rows;if(r.size()!=1)throw DatabaseError("Protected original user group is missing");auto g=decode_context_group(scope,r[0]);bytes+=context_group_json(g).dump().size();if(bytes>bound.max_metadata_bytes||s.protected_users.size()>=bound.max_groups)throw ContextCapacityExceeded("Protected user index exceeds its bound");s.protected_users.push_back(std::move(g));}
    const auto pending=db.execute("SELECT id,actor,expected_head_revision,state,owner_id,compaction_id FROM context_manual_requests WHERE scope_kind=? AND scope_id=? AND binding_key=? AND state='pending' ORDER BY rowid LIMIT 1",{kind,scope.id,key}).rows;if(!pending.empty()){const auto& p=pending[0];ContextManualRequest m;m.id=text(p[0]);m.actor=text(p[1]);m.scope=scope;m.binding=binding;m.expected_head_revision=integer(p[2]);m.state=text(p[3]);if(!std::holds_alternative<std::nullptr_t>(p[4]))m.owner_id=text(p[4]);if(!std::holds_alternative<std::nullptr_t>(p[5]))m.compaction_id=text(p[5]);s.pending_manual_request=std::move(m);}return s;
}
Json context_pause_json(const ContextPausePin& pin){return {{"scope_kind",context_scope_name(pin.head.scope.kind)},{"scope_id",pin.head.scope.id},{"binding",context_binding_json(pin.head.binding)},{"revision",pin.head.revision},{"covered",pin.head.covered_through_ordinal},{"checkpoint",pin.head.checkpoint_id?Json(*pin.head.checkpoint_id):Json(nullptr)},{"watermark",pin.source_watermark},{"source",pin.source_binding},{"users",pin.protected_user_binding}};}
ContextPausePin decode_context_pause(const std::string& raw){const auto j=Json::parse(object_json(raw,16384));ContextPausePin p;p.head.scope={j["scope_kind"]=="session"?ContextScopeKind::session:ContextScopeKind::execution,j.at("scope_id").get<std::string>()};p.head.binding=decode_context_binding(j.at("binding").dump());p.head.revision=j.at("revision");p.head.covered_through_ordinal=j.at("covered");if(!j["checkpoint"].is_null())p.head.checkpoint_id=j["checkpoint"].get<std::string>();p.source_watermark=j.at("watermark");p.source_binding=j.at("source");p.protected_user_binding=j.at("users");if(context_pause_json(p)!=j)throw DatabaseError("Saved closed context pin shape differs");return p;}
void validate_context_pause(XlangSqlite& db,const Run& root,const DynamicPlanCapabilities& caps,const ContextPausePin& pin){
    if(pin.head.scope.kind!=ContextScopeKind::session||pin.head.scope.id!=root.session_id||pin.head.binding.authority_identity!=caps.backend_identity||Json::parse(pin.head.binding.provider_identity_json)!=Json::parse(caps.provider_identity_json))throw ContextBindingChanged("Closed context pin authority changed");const auto s=read_context_snapshot(db,pin.head.scope,pin.head.binding,{});std::vector<std::int64_t> users;for(const auto& u:s.protected_users)users.push_back(u.ordinal);if(s.head.revision!=pin.head.revision||s.head.covered_through_ordinal!=pin.head.covered_through_ordinal||s.head.checkpoint_id!=pin.head.checkpoint_id||s.source_watermark!=pin.source_watermark||context_source_binding(s,s.source_watermark)!=pin.source_binding||context_user_binding(s,users)!=pin.protected_user_binding)throw ContextSourceChanged("Closed context pin source or projection changed");
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
constexpr std::string_view refresh_category="native-mcp-oauth-refresh";
void changed_one(const SqlResult&);
void refresh_spec(const McpOAuthRefreshSpec& s){
    constexpr std::string_view purpose_prefix="mcp-http-oauth:";
    auto scheme=s.token_endpoint.substr(0,8);for(auto& c:scheme)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    bounded_identity(s.request_id);bounded_identity(s.server_id);
    identifier(s.credential_id);if(s.scope!="server"||s.credential_id.size()>256||s.configuration_revision<1||s.credential_revision<1||s.credential_revision==std::numeric_limits<std::int64_t>::max()||!s.purpose.starts_with(purpose_prefix)||s.purpose.size()!=purpose_prefix.size()+64||s.purpose.substr(purpose_prefix.size()).find_first_not_of("0123456789abcdef")!=std::string::npos||s.token_endpoint.size()>8192||scheme!="https://"||s.token_endpoint.find_first_of("\r\n\0",0,3)!=std::string::npos)throw std::invalid_argument("Invalid MCP refresh binding");
    const auto j=Json::parse(object_json(s.server_json,65536));
    if(j.dump()!=s.server_json||j.at("id")!=s.server_id||j.at("revision")!=s.configuration_revision||j.at("transport")!="http"||j.at("enabled")!=true||!j.contains("oauth")||j.at("oauth").at("scope")!=s.scope||j.at("oauth").at("id")!=s.credential_id)throw std::invalid_argument("MCP refresh configuration binding differs");
}
Json refresh_binding_json(const McpOAuthRefreshSpec& s){return {{"request",s.request_id},{"server",s.server_id},{"scope",s.scope},{"credential",s.credential_id},{"purpose",s.purpose},{"configuration",Json::parse(s.server_json)},{"configuration_revision",s.configuration_revision},{"credential_revision",s.credential_revision},{"token_endpoint",s.token_endpoint}};}
Json refresh_json(const McpOAuthRefreshRecord& r){return {{"schema",1},{"binding",refresh_binding_json(r.binding)},{"generation",r.generation},{"state",r.state},{"created_ms",r.created_unix_ms},{"dispatched_ms",r.dispatched_unix_ms},{"settled_ms",r.settled_unix_ms},{"published_revision",r.published_revision}};}
McpOAuthRefreshRecord refresh_decode(const std::string& raw){
    try{
        const auto j=Json::parse(object_json(raw,98304));const auto& b=j.at("binding");McpOAuthRefreshRecord r;
        for(const auto* name:{"schema","created_ms","dispatched_ms","settled_ms","published_revision"})if(!j.at(name).is_number_integer())throw DatabaseError("Invalid saved MCP refresh integer");
        for(const auto* name:{"configuration_revision","credential_revision"})if(!b.at(name).is_number_integer())throw DatabaseError("Invalid saved MCP refresh revision");
        r.binding={b.at("request"),b.at("server"),b.at("scope"),b.at("credential"),b.at("purpose"),b.at("configuration").dump(),b.at("token_endpoint"),b.at("configuration_revision"),b.at("credential_revision")};
        r.generation=j.at("generation");r.state=j.at("state");r.created_unix_ms=j.at("created_ms");r.dispatched_unix_ms=j.at("dispatched_ms");r.settled_unix_ms=j.at("settled_ms");r.published_revision=j.at("published_revision");refresh_spec(r.binding);bounded_identity(r.generation);
        if(j.at("schema")!=1||refresh_json(r)!=j||r.created_unix_ms<=0||r.dispatched_unix_ms<0||r.settled_unix_ms<0||r.published_revision<0)throw DatabaseError("Invalid saved MCP refresh receipt");
        const bool prepared=r.state=="prepared",dispatched=r.state=="dispatched",cancelled=r.state=="cancelled",uncertain=r.state=="uncertain",committed=r.state=="committed";
        if(!(prepared||dispatched||cancelled||uncertain||committed)||(prepared&&(r.dispatched_unix_ms||r.settled_unix_ms))||(dispatched&&(!r.dispatched_unix_ms||r.settled_unix_ms))||(cancelled&&(r.dispatched_unix_ms||!r.settled_unix_ms))||((uncertain||committed)&&(!r.dispatched_unix_ms||!r.settled_unix_ms))||(committed?r.published_revision!=r.binding.credential_revision+1:r.published_revision!=0))throw DatabaseError("Invalid saved MCP refresh phase");return r;
    }catch(const DatabaseError&){throw;}catch(...){throw DatabaseError("Invalid saved MCP refresh receipt");}
}
McpOAuthRefreshRecord refresh_read(XlangSqlite& db,const std::string& id){
    bounded_identity(id);const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(refresh_category),id}).rows;if(rows.empty())throw NotFound("MCP refresh request not found");auto r=refresh_decode(text(rows.at(0).at(0)));if(r.binding.request_id!=id)throw DatabaseError("Saved MCP refresh identity differs");return r;
}
std::vector<McpOAuthRefreshRecord> refresh_records(XlangSqlite& db){
    const auto rows=db.execute("SELECT id,payload FROM information WHERE category=? ORDER BY id LIMIT 4097",{std::string(refresh_category)}).rows;if(rows.size()>4096)throw DatabaseError("MCP refresh receipt capacity exceeded");std::vector<McpOAuthRefreshRecord> records;
    for(const auto& row:rows){auto r=refresh_decode(text(row.at(1)));if(r.binding.request_id!=text(row.at(0)))throw DatabaseError("Saved MCP refresh identity differs");records.push_back(std::move(r));}return records;
}
bool refresh_blocks(const McpOAuthRefreshRecord& r){return r.state=="prepared"||r.state=="dispatched"||r.state=="uncertain";}
void refresh_fence(XlangSqlite& db,const std::string& scope,const std::string& id){for(const auto& r:refresh_records(db))if(r.binding.scope==scope&&r.binding.credential_id==id&&refresh_blocks(r))throw Conflict("MCP credential revision has a durable refresh owner");}
void refresh_configuration(XlangSqlite& db,const McpOAuthRefreshSpec& s){
    const auto rows=db.execute("SELECT payload FROM information WHERE category='native-mcp' AND id='servers'").rows;if(rows.empty())throw Conflict("MCP refresh configuration is absent");const auto j=Json::parse(object_json(text(rows.at(0).at(0)),262144));
    std::size_t matches=0;for(const auto& server:j.at("servers"))if(server.at("id")==s.server_id){++matches;if(server.dump()!=s.server_json)throw Conflict("MCP refresh configuration changed");}if(matches!=1)throw Conflict("MCP refresh configuration is absent or ambiguous");
}
void refresh_update(XlangSqlite& db,const McpOAuthRefreshRecord& before,const McpOAuthRefreshRecord& after){changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=? AND payload=?",{refresh_json(after).dump(),std::string(refresh_category),before.binding.request_id,refresh_json(before).dump()}));}
void refresh_recover(XlangSqlite& db,const std::string& generation){for(const auto& r:refresh_records(db))if(r.generation!=generation&&(r.state=="prepared"||r.state=="dispatched")){auto next=r;next.state=r.state=="prepared"?"cancelled":"uncertain";next.settled_unix_ms=now_ms();refresh_update(db,r,next);}}
void refresh_admission(XlangSqlite& db){for(const auto& r:refresh_records(db))if(r.state=="prepared"||r.state=="dispatched")throw Conflict("Native MCP renewal owns idle admission");}
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
    bool persistent_service=false;
    if(spec.tool=="run_process")try{const auto args=Json::parse(spec.arguments_json);persistent_service=args.value("background",false)&&args.value("persistent",false);}catch(...){}
    std::vector<std::string> result;std::set<std::string> seen;
    if(!persistent_service){result.push_back("workspace:"+spec.workspace);seen.insert(result[0]);}
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
McpInputRequest decode_mcp_input_request(const std::vector<SqlValue>& row){
    McpInputRequest result;result.id=text(row[0]);result.run_id=text(row[1]);result.operation_id=text(row[2]);result.server_config_id=text(row[3]);
    result.config_revision=integer(row[4]);result.peer_tool=text(row[5]);result.request_key=text(row[6]);result.request_json=text(row[7]);
    result.state=text(row[8]);if(!std::holds_alternative<std::nullptr_t>(row[9]))result.response_json=text(row[9]);
    result.actor=text(row[10]);result.expires_unix_ms=integer(row[11]);result.published_event_seq=integer(row[12]);
    if(!std::holds_alternative<std::nullptr_t>(row[13]))result.answered_event_seq=integer(row[13]);return result;
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
    const auto type=n.kind==DynamicNodeKind::agent?"agent":n.kind==DynamicNodeKind::tool?"tool":"human";
    Json result={{"id",n.label},{"type",type},{"depends_on",Json::array()}};
    if(n.kind==DynamicNodeKind::agent)result.update(Json{{"objective",n.objective},{"question",n.question},{"preset",n.preset_id},{"preset_revision",n.preset_revision}});
    else if(n.kind==DynamicNodeKind::tool)result.update(Json{{"tool",n.tool},{"arguments_json",n.arguments_json},{"preset_revision",0}});
    else result.update(Json{{"objective",n.objective},{"question",n.question},{"preset",n.preset_id},{"preset_revision",n.preset_revision}});
    for(const auto& d:n.dependencies)result["depends_on"].push_back({{"task",d.task},{"require",d.require==DynamicDependencyRequirement::success?"success":"observed"}});return result;
}
DynamicNodeDefinition dynamic_definition(const std::string& source){
    const auto j=Json::parse(object_json(source));DynamicNodeDefinition n;n.label=j.at("id");const auto type=j.at("type").get<std::string>();
    if(type=="agent"){n.kind=DynamicNodeKind::agent;n.objective=j.at("objective");n.question=j.value("question",std::string{});n.preset_id=j.at("preset");n.preset_revision=j.at("preset_revision");}
    else if(type=="tool"){n.kind=DynamicNodeKind::tool;n.tool=j.at("tool");n.arguments_json=j.at("arguments_json");n.preset_revision=0;}
    else if(type=="human"){n.kind=DynamicNodeKind::human;n.objective=j.value("objective",std::string{});n.question=j.at("question");n.preset_id=j.value("preset",std::string{});n.preset_revision=j.value("preset_revision",std::int64_t{0});}
    else throw DatabaseError("Invalid saved dynamic node type");
    for(const auto& d:j.at("depends_on"))n.dependencies.push_back({d.at("task"),d.at("require")=="success"?DynamicDependencyRequirement::success:DynamicDependencyRequirement::observed});return n;
}
const char* dynamic_state_name(DynamicNodeState s){switch(s){case DynamicNodeState::pending:return "pending";case DynamicNodeState::blocked:return "blocked";case DynamicNodeState::claimed:return "claimed";case DynamicNodeState::waiting_human:return "waiting_human";case DynamicNodeState::settled:return "settled";case DynamicNodeState::skipped:return "skipped";case DynamicNodeState::cancelled:return "cancelled";case DynamicNodeState::uncertain:return "uncertain";}throw DatabaseError("Invalid dynamic state");}
const char* dynamic_kind_name(DynamicNodeKind k){switch(k){case DynamicNodeKind::agent:return "agent";case DynamicNodeKind::tool:return "tool";case DynamicNodeKind::human:return "human";}throw DatabaseError("Invalid dynamic node kind");}
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
        c.max_nodes<1||c.max_nodes>32||c.max_revisions<1||c.max_revisions>16||c.max_humans<1||c.max_humans>8||c.change_bytes<1||c.change_bytes>262144||c.human_expiry_ms<1||c.human_expiry_ms>3600000||c.max_parent_turns<1||c.max_parent_turns>32||c.presets.empty()||c.presets.size()>2)throw std::invalid_argument("Invalid dynamic capability admission");
    (void)object_json(c.tool_catalog_json,262144);std::set<std::string> ids;
    for(const auto& p:c.presets){if(!ids.insert(p.id).second||(p.id!="workspace.inspect"&&p.id!="workspace.coding")||p.revision!=1||p.readonly!=(p.id=="workspace.inspect")||p.turn_limit<1||p.turn_limit>(p.readonly?4:16))throw std::invalid_argument("Invalid dynamic registered preset");
        if(finalized)private_identity(p.backend_identity);else if(!p.backend_identity.empty())private_identity(p.backend_identity);
        if(p.tools.size()>128||(finalized&&p.tools.empty()))throw std::invalid_argument("Invalid dynamic preset tools");std::set<std::string> tools;
        for(const auto& t:p.tools){bounded_identity(t);if(!tools.insert(t).second||t=="delegate_tasks"||t=="plan_tasks"||t=="revise_plan"||t=="inspect_plan"||(p.readonly&&t!="read_file"&&t!="list_files"&&t!="search_files"&&t!="find_files"&&t!="glob_files"&&t!="read_repository_instructions"&&t!="list_skills"&&t!="load_skill"))throw std::invalid_argument("Invalid bounded child tool authority");}
    }
}
const DynamicPresetCapability& dynamic_preset(const DynamicPlanCapabilities& c,const DynamicNodeDefinition& d){const auto p=std::find_if(c.presets.begin(),c.presets.end(),[&](const auto& v){return v.id==d.preset_id&&v.revision==d.preset_revision;});if(p==c.presets.end())throw DynamicPlanUnavailable("Dynamic preset is unavailable");return *p;}
void dynamic_mcp_instruction_snapshot(const Json& value,const std::string& workspace){
    if(!value.is_object()||value.size()!=(value.contains("skills")?4u:3u)||value.dump().size()>24576||
        !value.contains("version")||!value["version"].is_number_integer()||value["version"]!=1||
        !value.contains("directory")||value["directory"]!="."||!value.contains("sources")||
        !value["sources"].is_array()||value["sources"].size()>32||
        (value.contains("skills")&&(!value["skills"].is_array()||value["skills"].size()>8)))
        throw Conflict("Dynamic MCP instruction snapshot has an invalid native shape");
    auto text=[](const Json& source,const char* field,std::size_t maximum){return source.contains(field)&&source[field].is_string()&&!source[field].get_ref<const std::string&>().empty()&&source[field].get_ref<const std::string&>().size()<=maximum&&source[field].get_ref<const std::string&>().find('\0')==std::string::npos;};
    auto validate=[&](const Json& source,bool skill){
        if(!source.is_object()||source.size()!=(skill?6u:5u)||!text(source,"path",4096)||
            !text(source,"workspace_id",32768)||source["workspace_id"]!=workspace||!text(source,"file_id",256)||
            !text(source,"content_sha256",64)||source["content_sha256"].get_ref<const std::string&>().size()!=64||
            source["content_sha256"].get_ref<const std::string&>().find_first_not_of("0123456789abcdef")!=std::string::npos||
            !source.contains("byte_count")||!source["byte_count"].is_number_integer()||source["byte_count"]<0||source["byte_count"]>65536||
            (skill&&!text(source,"id",256)))throw Conflict("Dynamic MCP instruction source differs from its native snapshot shape");
    };
    for(const auto& source:value["sources"])validate(source,false);
    if(value.contains("skills"))for(const auto& source:value["skills"])validate(source,true);
    // This metadata binds guidance only. The captured registry/preset still
    // supplies effect authority, and the executor rechecks source handles and
    // hashes after approval before dispatch.
}
void dynamic_effect_authority(const OperationSpec& spec,const DynamicPlanRecord& plan,const DynamicNodeRecord& node,const DynamicPresetCapability& preset){
    if(plan.state!="active"||!plan.capabilities.catalogue_finalized||node.state!=DynamicNodeState::claimed||node.definition.kind!=DynamicNodeKind::agent||node.claim_revision!=node.definition_revision||node.claim_id.empty()||preset.readonly||spec.workspace!=plan.capabilities.workspace_identity)throw Conflict("Dynamic child effect is outside its immutable workspace and claim");
    // Effect journals use backend adapter identities, not model tool names.
    // Only these native mappings exist; a model/preset cannot define another.
    std::string model_tool;
    if(spec.tool=="replace_file")model_tool="edit_file";
    else if(spec.tool=="patch_file")model_tool="apply_patch";
    else if(spec.tool=="create_file"||spec.tool=="run_process")model_tool=spec.tool;
    else if(spec.tool=="mcp_tool"){
        const auto proposal=Json::parse(spec.arguments_json);
        if(!proposal.contains("alias")||!proposal["alias"].is_string())throw Conflict("Dynamic MCP proposal lacks its captured alias");
        model_tool=proposal["alias"].get<std::string>();
        if(model_tool.size()!=52||!model_tool.starts_with("mcp_")||model_tool.substr(4).find_first_not_of("0123456789abcdef")!=std::string::npos)throw Conflict("Dynamic MCP alias is invalid");
    }else if(spec.tool=="xlang3_tool"){
        const auto proposal=Json::parse(spec.arguments_json);
        if(!proposal.is_object()||proposal.size()!=2||!proposal.contains("alias")||!proposal["alias"].is_string()||!proposal.contains("arguments")||!proposal["arguments"].is_object())throw Conflict("Dynamic callable proposal lacks its captured alias and arguments");
        model_tool=proposal["alias"].get<std::string>();validate_agent_tool_allowlist(std::vector<std::string>{model_tool});
    }else throw Conflict("Dynamic child effect adapter is not registered");
    if(std::find(preset.tools.begin(),preset.tools.end(),model_tool)==preset.tools.end())throw Conflict("Dynamic child effect tool is outside its immutable preset");
    const auto catalogue=Json::parse(plan.capabilities.tool_catalog_json);
    if(!catalogue.contains("tools")||!catalogue["tools"].is_array())throw Conflict("Dynamic child effect catalogue is unavailable");
    const Json* captured=nullptr;
    for(const auto& tool:catalogue["tools"])if(tool.is_object()&&tool.contains("name")&&tool["name"].is_string()&&tool["name"]==model_tool){if(captured)throw Conflict("Dynamic child effect catalogue is ambiguous");captured=&tool;}
    if(!captured)throw Conflict("Dynamic child effect lacks its sealed catalogue entry");
    if(spec.tool=="xlang3_tool"){
        if(!captured->contains("xlang3_binding")||!(*captured)["xlang3_binding"].is_object())throw Conflict("Dynamic callable lacks its sealed registration");
        const auto& binding=(*captured)["xlang3_binding"];
        if(binding.size()!=4||binding.value("kind",std::string{})!="trusted_xlang3_callable"||binding.value("alias",std::string{})!=model_tool||!binding.contains("input_schema_json")||binding["input_schema_json"]!=captured->at("input_schema_json")||(binding.value("execution",std::string{})!="worker"&&binding.value("execution",std::string{})!="in_process"))throw Conflict("Dynamic callable registration differs from its sealed catalogue");
        JsonSchema schema(binding["input_schema_json"].get<std::string>());schema.validate_object(Json::parse(spec.arguments_json).at("arguments").dump());return;
    }
    if(spec.tool!="mcp_tool")return;
    if(!captured->contains("mcp_binding")||!(*captured)["mcp_binding"].is_object())throw Conflict("Dynamic MCP tool lacks its native registry binding");
    const auto& binding=(*captured)["mcp_binding"];const auto proposal=Json::parse(spec.arguments_json);
    const bool readonly_binding=binding.contains("kind");
    if(proposal.contains("instructions"))dynamic_mcp_instruction_snapshot(proposal["instructions"],spec.workspace);
    if(readonly_binding){
        const auto kind=binding["kind"].is_string()?binding["kind"].get<std::string>():std::string{};
        const auto expected_fields=kind=="resource"?12u:kind=="prompt"?11u:0u;
        if(!expected_fields||binding.size()!=expected_fields||proposal.size()!=expected_fields+1+(proposal.contains("instructions")?1u:0u))throw Conflict("Dynamic MCP read binding has unexpected fields");
        auto actual=proposal;actual.erase("arguments_json");actual.erase("instructions");if(actual!=binding)throw Conflict("Dynamic MCP read differs from its captured native binding");
        for(const auto* field:{"server_config_id","kind","peer_name","alias","catalogue_fingerprint","protocol_version","input_schema_json","annotations_json","metadata_json"})if(!binding.contains(field)||!binding[field].is_string()||binding[field].get_ref<const std::string&>().empty()||binding[field].get_ref<const std::string&>().find('\0')!=std::string::npos)throw Conflict("Dynamic MCP read metadata is invalid");
        if(kind=="resource"&&(!binding.contains("resource_uri")||!binding["resource_uri"].is_string()||binding["resource_uri"].get_ref<const std::string&>().empty()))throw Conflict("Dynamic MCP resource URI is invalid");
        if(!binding["config_revision"].is_number_integer()||binding["config_revision"]<1||binding["config_revision"]>9007199254740991||!binding["catalogue_fingerprint"].is_string()||!binding["output_schema_json"].is_null())throw Conflict("Dynamic MCP read identity is invalid");
        const auto metadata=object_json(binding["metadata_json"].get<std::string>(),262144);(void)metadata;(void)object_json(binding["input_schema_json"].get<std::string>(),262144);(void)object_json(binding["annotations_json"].get<std::string>(),262144);
    }else{
        if(binding.size()!=9||proposal.size()!=(proposal.contains("instructions")?11u:10u))throw Conflict("Dynamic MCP registry binding has unexpected fields");
        for(const auto* field:{"server_config_id","config_revision","peer_tool","alias","catalogue_fingerprint","protocol_version","input_schema_json","output_schema_json","annotations_json"})if(!binding.contains(field)||!proposal.contains(field)||binding[field].type()!=proposal[field].type()||binding[field]!=proposal[field])throw Conflict("Dynamic MCP proposal differs from its captured native registry binding");
        for(const auto* field:{"server_config_id","peer_tool","protocol_version","input_schema_json","annotations_json"})if(!binding[field].is_string()||binding[field].get_ref<const std::string&>().empty()||binding[field].get_ref<const std::string&>().find('\0')!=std::string::npos)throw Conflict("Dynamic MCP registry metadata is invalid");
        if(binding["server_config_id"].get_ref<const std::string&>().size()>128||binding["peer_tool"].get_ref<const std::string&>().size()>256||binding["protocol_version"].get_ref<const std::string&>().size()>64||!binding["config_revision"].is_number_integer()||binding["config_revision"]<1||binding["config_revision"]>9007199254740991||!binding["catalogue_fingerprint"].is_string())throw Conflict("Dynamic MCP registry identity is invalid");
        const auto fingerprint=binding["catalogue_fingerprint"].get<std::string>();if(fingerprint.size()!=64||fingerprint.find_first_not_of("0123456789abcdef")!=std::string::npos||model_tool!="mcp_"+fingerprint.substr(0,48)||binding["alias"]!=model_tool)throw Conflict("Dynamic MCP registry fingerprint differs from its alias");
        (void)object_json(binding["annotations_json"].get<std::string>(),262144);
    }
    const auto fingerprint=binding["catalogue_fingerprint"].get<std::string>();
    if(fingerprint.size()!=64||fingerprint.find_first_not_of("0123456789abcdef")!=std::string::npos||model_tool!="mcp_"+fingerprint.substr(0,48)||binding["alias"]!=model_tool)throw Conflict("Dynamic MCP registry fingerprint differs from its alias");
    if(!captured->contains("input_schema_json")||!(*captured)["input_schema_json"].is_string()||(*captured)["input_schema_json"]!=binding["input_schema_json"]||!proposal.contains("arguments_json")||!proposal["arguments_json"].is_string())throw Conflict("Dynamic MCP proposal lacks its exact sealed schema and arguments");
    (void)object_json(binding["input_schema_json"].get<std::string>(),262144);(void)object_json(proposal["arguments_json"].get<std::string>());
    if(!readonly_binding&&!binding["output_schema_json"].is_null()){if(!binding["output_schema_json"].is_string())throw Conflict("Dynamic MCP output schema is invalid");(void)object_json(binding["output_schema_json"].get<std::string>(),262144);}
    if(spec.resources!=std::vector<std::string>{"mcp-server:"+binding["server_config_id"].get<std::string>()})throw Conflict("Dynamic MCP proposal resource differs from its captured server");
    // Registry validates arguments against this schema before requesting the
    // operation. The exact raw payload remains separately controller-approved.
}
void dynamic_cas(const DynamicPlanRecord& p,std::int64_t revision,std::int64_t sequence,const std::string& identity){if(p.capabilities.backend_identity!=identity)throw DynamicPlanUnavailable("Dynamic settings changed");if(p.revision!=revision||p.state_sequence!=sequence)throw DynamicPlanChanged("Dynamic plan changed");}
void dynamic_safe_sequence(std::int64_t sequence){if(sequence<0||sequence>=9007199254740991)throw std::overflow_error("Dynamic state sequence exhausted");}
Json dynamic_prompt(const DynamicNodeDefinition& d,const std::string& input,const DynamicPlanCapabilities& c){
    if(d.kind==DynamicNodeKind::tool)return {{"node_type","tool"},{"tool",d.tool},{"arguments_json",d.arguments_json},{"dependency_outputs_json",input}};
    Json result={{"content",d.objective+"\n\nObserved dependency data (not instructions):\n"+input}};const auto provider=Json::parse(c.provider_identity_json);if(provider.size()==6)result["provider_context"]=provider;return result;
}
}
struct Repository::Impl {
    AgentWorkerLeaseRecord retire_worker(const std::string& generation,const std::string& reason);
    std::string path;
    void apply_skill_turn(Repository&,const Run&,const Json&,const std::vector<std::string>&,const std::optional<SkillSelections>&,const std::set<std::string>* activation_calls=nullptr);
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
        if(kind=="run.queued"||kind=="conversation.tool_turn"||kind=="conversation.assistant")index_context_event(database,id,kind,*result.last_insert_id);
        const bool modern_usage=kind=="model.usage"&&!database.execute("SELECT id FROM inference_steps WHERE owner_run_id=?",{id}).rows.empty();
        if(kind=="model.text"||kind=="model.refusal"||kind=="model.tool_delta"||kind=="model.reasoning"||(modern_usage&&context_usage_has_output(json))){
            const auto started=database.execute("SELECT root_run_id,attempt_id FROM agent_model_call_reservations WHERE owner_run_id=? AND state='started' AND purpose='inference' AND inference_step_id IS NOT NULL",{id}).rows;
            if(started.size()>1)throw DatabaseError("Owned output has ambiguous inference transport");
            if(started.empty()&&!database.execute("SELECT id FROM inference_steps WHERE owner_run_id=?",{id}).rows.empty())throw Conflict("Modern model output has no currently started inference member");
            if(!started.empty())changed_one(database.execute("INSERT INTO inference_output_evidence(root_run_id,attempt_id,event_seq) VALUES(?,?,?)",{text(started[0][0]),text(started[0][1]),*result.last_insert_id}));
        }
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
    bool has_unresolved_effects(const std::string& id) {
        // Only an already dispatched persistent service may outlive the answer.
        // Pending approval, undispatched work and uncertain outcomes still block.
        return !database.execute("SELECT id FROM operations WHERE run_id=? AND state IN ('awaiting_approval','ready','executing','uncertain') AND NOT (state='executing' AND tool='run_process' AND json_extract(arguments,'$.background')=1 AND json_extract(arguments,'$.persistent')=1) LIMIT 1",{id}).rows.empty();
    }
    void message(const Run& run,const std::string& role,const std::string& json){changed_one(database.execute("INSERT INTO messages(session_id,execution_run_id,role,payload) VALUES(?,?,?,?)",{run.session_id,run.parent_id.empty()?SqlValue(nullptr):SqlValue(run.id),role,json}));changed_one(database.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{run.id}));}
    std::pair<std::string,std::int64_t> graph_lineage(const std::string& id,bool active=false){
        std::string current=id,session,root;std::int64_t depth=0;std::set<std::string> visited;
        for(;;){
            if(!visited.insert(current).second||visited.size()>4)throw Conflict("Graph invocation ancestry exceeds limits");
            const auto rows=database.execute("SELECT r.session_id,COALESCE(r.parent_run_id,''),r.state,g.specification,g.graph_id,g.graph_revision,COALESCE(r.node_id,'') FROM runs r JOIN graph_roots g ON g.run_id=r.id WHERE r.id=?",{current}).rows;
            if(rows.empty())throw Conflict("Graph invocation ancestor is not a graph owner");const auto& row=rows[0];if(session.empty())session=text(row[0]);if(session!=text(row[0])||(active&&text(row[2])!="running"))throw Conflict("Graph invocation ancestor differs or is not running");
            const auto parent=text(row[1]);if(parent.empty()){if(!root.empty()&&(root!=current||visited.size()!=static_cast<std::size_t>(depth+1)))throw Conflict("Graph invocation root or depth differs");return {current,depth};}
            const auto pins=database.execute("SELECT root_run_id,parent_run_id,depth FROM graph_invocations WHERE run_id=?",{current}).rows;
            if(pins.size()!=1||text(pins[0][1])!=parent)throw Conflict("Graph invocation has no immutable owner record");const auto& pin=pins[0];if(root.empty()){root=text(pin[0]);depth=integer(pin[2]);}if(root!=text(pin[0])||integer(pin[2])!=depth-static_cast<std::int64_t>(visited.size())+1)throw Conflict("Graph invocation depth differs");
            const auto parents=database.execute("SELECT specification FROM graph_roots WHERE run_id=?",{parent}).rows;if(parents.empty())throw Conflict("Graph invocation parent specification is absent");const auto spec=Json::parse(text(parents[0][0]));bool matched=false;
            for(const auto& node:spec.at("nodes"))if(node.at("id")==text(row[6])&&node.at("type")=="graph"&&node.at("graph_id")==text(row[4])&&node.at("graph_revision")==integer(row[5])&&node.at("graph_spec").dump()==text(row[3]))matched=true;
            if(!matched)throw Conflict("Graph invocation differs from its parent's immutable snapshot");current=parent;
        }
    }
    std::string graph_node_path(const std::string& owner,const std::string& node,const std::string& root){
        const auto component=[](const std::string& value){if(value.empty()||value.size()>64||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw Conflict("Graph node path component differs from its declared identity");};component(node);std::string path=node,current=owner;
        for(std::size_t depth=0;current!=root&&depth<3;++depth){const auto rows=database.execute("SELECT COALESCE(r.parent_run_id,''),COALESCE(r.node_id,'') FROM runs r JOIN graph_roots g ON g.run_id=r.id WHERE r.id=?",{current}).rows;if(rows.size()!=1||text(rows[0][0]).empty())throw Conflict("Graph node path lost its admitted root");const auto id=text(rows[0][1]);component(id);path=id+"/"+path;current=text(rows[0][0]);}
        if(current!=root||path.size()>259)throw Conflict("Graph node path exceeds immutable invocation limits");return path;
    }
    void root_boundary(const Run& current,RunState next){
        if(next==RunState::paused||terminal(next)){
            if(!database.execute("SELECT id FROM context_compactions WHERE owner_run_id=? AND state IN ('reserved','started','received')",{current.id}).rows.empty()||!database.execute("SELECT id FROM context_measures WHERE owner_id=? AND state IN ('reserved','started')",{current.id}).rows.empty())throw Conflict("Execution still owns context maintenance or counting transport");
        }
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
            if(!database.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{current.id}).rows.empty()&&!database.execute("SELECT id FROM agent_budget_segments WHERE root_run_id=? AND state='open'",{current.id}).rows.empty())throw Conflict("Registered graph boundary requires measured typed clock closure");
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

    std::set<std::string> closed_paused_graph_tree(const std::string& root_id,bool measured_context=false,std::set<std::string>* runnable=nullptr){
        auto& db=database;const auto roots=db.execute("SELECT state FROM runs WHERE id=? AND parent_run_id IS NULL",{root_id}).rows;if(roots.empty()||text(roots[0][0])!="paused"||(!measured_context&&!db.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{root_id}).rows.empty()))return {};
        const auto rows=db.execute("WITH RECURSIVE tree(id,depth) AS (SELECT ?,0 UNION ALL SELECT r.id,t.depth+1 FROM runs r JOIN tree t ON r.parent_run_id=t.id WHERE t.depth<4) SELECT r.id,r.session_id,r.state,COALESCE(r.parent_run_id,''),COALESCE(r.node_id,''),g.run_id,g.graph_id,g.graph_revision,g.specification,g.checkpoint_revision,g.checkpoint,COALESCE(g.input,'') FROM tree t JOIN runs r ON r.id=t.id LEFT JOIN graph_roots g ON g.run_id=r.id ORDER BY t.depth DESC,r.id",{root_id}).rows;
        if(rows.size()>257)return {};std::map<std::string,Run> runs;std::vector<std::pair<GraphRootRecord,GraphCoordinator>> owners;bool waiting=false;
        for(const auto& row:rows){const bool graph=!std::holds_alternative<std::nullptr_t>(row[5]);Run current{text(row[0]),text(row[1]),state_value(text(row[2])),text(row[3]),text(row[4]),graph};runs.emplace(current.id,current);if(has_operations(current.id,"'proposed','awaiting_approval','approved','executing','uncertain'"))return {};if(terminal(current.state))continue;if(!graph||current.state!=RunState::paused||graph_lineage(current.id).first!=root_id)return {};GraphRootRecord record{current,text(row[6]),integer(row[7]),text(row[8]),integer(row[9]),text(row[10]),text(row[11])};owners.emplace_back(record,GraphCoordinator(GraphPlan(record.specification_json),record.checkpoint_json,GraphRestoreMode::live));}
        if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{root_id}).rows.empty())return {};
        std::set<std::string> closed;
        for(const auto& [record,coordinator]:owners){
            const auto decision=coordinator.inspect();bool ready=decision.finished||!decision.ready.empty()||!decision.skippable.empty();
            if(decision.halted||(!runnable&&ready))return {};waiting=waiting||!decision.waiting_human.empty();
            for(const auto& node:decision.running){bool observed=false;for(const auto& [id,child]:runs)if(child.parent_id==record.run.id&&child.node_id==node&&child.graph_root&&closed.contains(id)){observed=true;if(runnable&&runnable->contains(id))ready=true;}if(!observed)return {};}
            closed.insert(record.run.id);if(runnable&&ready)runnable->insert(record.run.id);
        }
        if((!waiting&&(!runnable||!runnable->contains(root_id)))||!closed.contains(root_id))return {};return closed;
    }
    bool pause_graph_tree(const std::string& root_id,bool measured_context=false){
        auto& db=database;
        if(!measured_context&&!db.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{root_id}).rows.empty())return false;
        const auto root_state=db.execute("SELECT state FROM runs WHERE id=? AND parent_run_id IS NULL",{root_id}).rows;
        if(root_state.empty()||text(root_state[0][0])!="running")return false;
        const auto rows=db.execute("WITH RECURSIVE tree(id,depth) AS (SELECT ?,0 UNION ALL SELECT r.id,t.depth+1 FROM runs r JOIN tree t ON r.parent_run_id=t.id WHERE t.depth<4) SELECT r.id,r.session_id,r.state,COALESCE(r.parent_run_id,''),COALESCE(r.node_id,''),g.run_id,g.graph_id,g.graph_revision,g.specification,g.checkpoint_revision,g.checkpoint,COALESCE(g.input,'') FROM tree t JOIN runs r ON r.id=t.id LEFT JOIN graph_roots g ON g.run_id=r.id ORDER BY t.depth DESC,r.id",{root_id}).rows;
        if(rows.size()>257)throw Conflict("Graph pause tree exceeds its immutable expansion limit");
        std::map<std::string,Run> runs;std::vector<std::pair<GraphRootRecord,GraphCoordinator>> owners;bool waiting=false;
        for(const auto& row:rows){const bool graph=!std::holds_alternative<std::nullptr_t>(row[5]);Run current{text(row[0]),text(row[1]),state_value(text(row[2])),text(row[3]),text(row[4]),graph};runs.emplace(current.id,current);
            if(has_operations(current.id,"'proposed','awaiting_approval','approved','executing','uncertain'"))return false;
            if(terminal(current.state))continue;
            if(!graph||current.state==RunState::queued)return false;
            if(graph_lineage(current.id).first!=root_id)throw Conflict("Graph pause owner differs from the actual root");
            GraphRootRecord record{current,text(row[6]),integer(row[7]),text(row[8]),integer(row[9]),text(row[10]),text(row[11])};owners.emplace_back(record,GraphCoordinator(GraphPlan(record.specification_json),record.checkpoint_json,GraphRestoreMode::live));
        }
        if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{root_id}).rows.empty())return false;
        std::set<std::string> closed;
        for(const auto& [record,coordinator]:owners){const auto decision=coordinator.inspect();if(decision.halted||decision.finished||!decision.ready.empty()||!decision.skippable.empty())return false;waiting=waiting||!decision.waiting_human.empty();
            for(const auto& node:decision.running){bool observed=false;for(const auto& [id,child]:runs)if(child.parent_id==record.run.id&&child.node_id==node&&child.graph_root&&closed.contains(id))observed=true;if(!observed)return false;}
            closed.insert(record.run.id);
        }
        if(!waiting||!closed.contains(root_id))return false;
        for(auto& [record,coordinator]:owners)if(record.run.state==RunState::running){graph_checkpoint(record,coordinator,record.checkpoint_revision);changed_one(db.execute("UPDATE runs SET state='paused' WHERE id=? AND state='running'",{record.run.id}));event(record.run.id,"run.paused",Json{{"reason","graph_tree_human_wait"},{"root_run_id",root_id},{"checkpoint_revision",record.checkpoint_revision}}.dump());}
        return true;
    }
    void graph_human_pause(GraphRootRecord& root,const GraphCoordinator& coordinator){
        if(!database.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{root.run.id}).rows.empty())return;
        const auto owner=graph_lineage(root.run.id).first;
        if(!database.execute("SELECT run_id FROM graph_invocations WHERE root_run_id=? LIMIT 1",{owner}).rows.empty()){
            if(pause_graph_tree(owner)){const auto row=database.execute("SELECT state,checkpoint_revision,checkpoint FROM runs r JOIN graph_roots g ON g.run_id=r.id WHERE r.id=?",{root.run.id}).rows.at(0);root.run.state=state_value(text(row[0]));root.checkpoint_revision=integer(row[1]);root.checkpoint_json=text(row[2]);}return;
        }
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
Repository::Repository(const std::string& file,const std::vector<std::string>& roots,const BackendLease* startup_lease,const BackendOwnerBootstrap* bootstrap,const std::string& startup_generation,const LegacyOwnerBootstrap* legacy):impl_(std::make_unique<Impl>(file,roots)) {
    auto& db=impl_->database; Transaction transaction(db);
    const auto version=integer(db.execute("PRAGMA user_version").rows.at(0).at(0));
    const auto application=integer(db.execute("PRAGMA application_id").rows.at(0).at(0));
    if(version>35)throw DatabaseError("Database schema requires a newer xMind runtime");
    if((version==0 && application!=0) || (version!=0 && application!=0x584d494e))
        throw DatabaseError("Database is not the target xMind repository");
    // Reject a closed or malformed durable owner before schema writes. Direct
    // repository inspection does not claim startup ownership; PersistenceService
    // always supplies its actual worker-held lease here.
    if((bootstrap||legacy)&&!startup_lease)throw Conflict("Replacement startup requires a database lease");
    if(bootstrap&&legacy)throw std::invalid_argument("Native and legacy replacement preconditions are distinct");
    if(startup_lease){
        if(!startup_lease->covers(file))throw Conflict("Startup requires this database lease");
        const auto prior=db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='information'").rows.empty()?std::optional<std::string>{}:owner_record(db);
        if(legacy){require_legacy_owner_bootstrap(db,*startup_lease,*legacy);if(prior&&decode_owner(*prior).quiesced&&decode_owner(*prior).legacy_ticket_id!=legacy->ticket_id)throw Conflict("Closed native owner cannot be adopted by this legacy ticket");}
        else if(legacy_owner_record(db))throw BackendQuiesced("Legacy operator migration is pending; ordinary startup cannot apply migrations");
        else if(bootstrap){if(!prior)throw Conflict("Replacement source owner is absent");require_bootstrap(db,*startup_lease,decode_owner(*prior),*bootstrap);}
        else if(prior&&decode_owner(*prior).quiesced)throw BackendQuiesced("Durable native admission is closed; normal startup cannot apply migrations");
    }
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
    } else if(version<1 || version>35) throw DatabaseError("Unsupported target repository version");
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
    // Empty typed tables precede the context migration's final deferred-FK
    // validation. No relational writes follow that counter-clearing boundary.
    if(version<13){
        db.execute("CREATE TABLE session_skills(session_id TEXT NOT NULL REFERENCES sessions(id),workspace_id TEXT NOT NULL,selections_json TEXT NOT NULL CHECK(length(CAST(selections_json AS BLOB))<=8192 AND json_valid(selections_json) AND COALESCE(json_type(selections_json,'$.version')='integer' AND json_extract(selections_json,'$.version')=1 AND json_type(selections_json,'$.ids')='array' AND json_array_length(selections_json,'$.ids')<=8 AND json_extract(selections_json,'$.workspace_id')=workspace_id,0)),revision INTEGER NOT NULL CHECK(revision>0),PRIMARY KEY(session_id,workspace_id))");
        db.execute("CREATE TABLE run_skills(run_id TEXT PRIMARY KEY REFERENCES runs(id),workspace_id TEXT NOT NULL,selections_json TEXT NOT NULL CHECK(length(CAST(selections_json AS BLOB))<=8192 AND json_valid(selections_json) AND COALESCE(json_type(selections_json,'$.version')='integer' AND json_extract(selections_json,'$.version')=1 AND json_type(selections_json,'$.ids')='array' AND json_array_length(selections_json,'$.ids')<=8 AND json_extract(selections_json,'$.workspace_id')=workspace_id,0)),revision INTEGER NOT NULL CHECK(revision>0))");
        db.execute("CREATE TRIGGER run_skill_identity BEFORE UPDATE OF run_id,workspace_id ON run_skills BEGIN SELECT RAISE(ABORT,'run skill scope is immutable'); END");
        db.execute("CREATE TRIGGER run_skill_insert_owner BEFORE INSERT ON run_skills WHEN NOT EXISTS(SELECT 1 FROM runs WHERE id=NEW.run_id AND state='running') BEGIN SELECT RAISE(ABORT,'run skill initialization requires its running owner'); END");
        db.execute("CREATE TRIGGER run_skill_update_owner BEFORE UPDATE OF selections_json,revision ON run_skills WHEN NOT EXISTS(SELECT 1 FROM runs WHERE id=OLD.run_id AND state='running') OR NEW.revision!=OLD.revision+1 BEGIN SELECT RAISE(ABORT,'run skill change requires its running owner and next revision'); END");
    }
    if(version<12){
        // All referencing budget foreign keys are NO ACTION, never CASCADE.
        // Deferral permits an atomic replacement with exactly the same keys;
        // the explicit final foreign_key_check precedes transaction commit.
        db.execute("PRAGMA defer_foreign_keys=ON");
        db.execute("DROP TRIGGER agent_budget_identity_immutable");
        const auto budget_triggers=db.execute("SELECT name,sql FROM sqlite_master WHERE type='trigger' AND instr(sql,'agent_execution_budgets')>0 ORDER BY name").rows;
        for(const auto& row:budget_triggers){const auto name=text(row[0]);if(name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)throw DatabaseError("Invalid migration trigger identity");db.execute("DROP TRIGGER "+name);}
        db.execute("CREATE TABLE context_budget_replacement(root_run_id TEXT PRIMARY KEY NOT NULL REFERENCES runs(id),policy_id TEXT NOT NULL,policy_revision INTEGER NOT NULL CHECK(policy_revision>0),workspace_identity TEXT NOT NULL,provider_identity_json TEXT NOT NULL CHECK(json_valid(provider_identity_json) AND json_type(provider_identity_json)='object'),max_children INTEGER NOT NULL CHECK(max_children BETWEEN 0 AND 8),max_parallel INTEGER NOT NULL CHECK(max_parallel BETWEEN 0 AND 2),max_model_calls INTEGER NOT NULL CHECK(max_model_calls BETWEEN 1 AND 32),wall_limit_ms INTEGER NOT NULL CHECK(wall_limit_ms BETWEEN 1 AND 3600000),children_admitted INTEGER NOT NULL DEFAULT 0 CHECK(children_admitted BETWEEN 0 AND max_children),model_calls_reserved INTEGER NOT NULL DEFAULT 0 CHECK(model_calls_reserved BETWEEN 0 AND max_model_calls),parent_calls_held INTEGER NOT NULL DEFAULT 0 CHECK(parent_calls_held>=0),revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),planned_children_reserved INTEGER NOT NULL DEFAULT 0 CHECK(planned_children_reserved>=0 AND planned_children_reserved+children_admitted<=max_children),CHECK(model_calls_reserved+parent_calls_held<=max_model_calls),CHECK((policy_id='native.context' AND max_children=0 AND max_parallel=0) OR (policy_id IN ('native.delegation','native.dynamic-plan','native.graph-context') AND max_children BETWEEN 1 AND 8 AND max_parallel BETWEEN 1 AND 2)))");
        db.execute("INSERT INTO context_budget_replacement SELECT root_run_id,policy_id,policy_revision,workspace_identity,provider_identity_json,max_children,max_parallel,max_model_calls,wall_limit_ms,children_admitted,model_calls_reserved,parent_calls_held,revision,planned_children_reserved FROM agent_execution_budgets");
        db.execute("DROP TABLE agent_execution_budgets");
        db.execute("ALTER TABLE context_budget_replacement RENAME TO agent_execution_budgets");
        for(const auto& row:budget_triggers)db.execute(text(row[1]));
        db.execute("CREATE TRIGGER agent_budget_identity_immutable BEFORE UPDATE OF root_run_id,policy_id,policy_revision,workspace_identity,provider_identity_json,max_children,max_parallel,max_model_calls,wall_limit_ms ON agent_execution_budgets BEGIN SELECT RAISE(ABORT,'admitted execution policy is immutable'); END");
        // Share the actual clock table with registered graphs without inventing
        // a dynamic capability. Preserve every existing segment and pause FK.
        const auto segment_triggers=db.execute("SELECT name,sql FROM sqlite_master WHERE type='trigger' AND instr(sql,'agent_budget_segments')>0").rows;
        for(const auto& row:segment_triggers)db.execute("DROP TRIGGER \""+text(row[0])+"\"");
        db.execute("CREATE TABLE context_segment_replacement(root_run_id TEXT NOT NULL REFERENCES runs(id),id TEXT NOT NULL UNIQUE,ordinal INTEGER NOT NULL CHECK(ordinal>0),state TEXT NOT NULL CHECK(state IN ('open','closed','interrupted')),active_elapsed_ms INTEGER NOT NULL DEFAULT 0 CHECK(active_elapsed_ms>=0),remaining_active_ms INTEGER NOT NULL CHECK(remaining_active_ms>=0),opened_event_seq INTEGER NOT NULL REFERENCES events(seq),closed_event_seq INTEGER REFERENCES events(seq),PRIMARY KEY(root_run_id,ordinal),CHECK((state='open')=(closed_event_seq IS NULL)))");
        db.execute("INSERT INTO context_segment_replacement SELECT * FROM agent_budget_segments");
        db.execute("DROP TABLE agent_budget_segments");db.execute("ALTER TABLE context_segment_replacement RENAME TO agent_budget_segments");
        db.execute("CREATE UNIQUE INDEX one_open_budget_segment ON agent_budget_segments(root_run_id) WHERE state='open'");
        for(const auto& row:segment_triggers)db.execute(text(row[1]));
        db.execute("CREATE TABLE graph_context_owners(root_run_id TEXT PRIMARY KEY NOT NULL REFERENCES graph_roots(run_id),backend_identity TEXT NOT NULL CHECK(length(backend_identity)=64),authority_json TEXT NOT NULL CHECK(json_valid(authority_json)),paused_pins_json TEXT CHECK(paused_pins_json IS NULL OR json_valid(paused_pins_json)),paused_segment_id TEXT REFERENCES agent_budget_segments(id),CHECK((paused_pins_json IS NULL)=(paused_segment_id IS NULL)))");
        db.execute("CREATE TRIGGER graph_context_identity BEFORE UPDATE OF root_run_id,backend_identity,authority_json ON graph_context_owners BEGIN SELECT RAISE(ABORT,'registered graph context authority is immutable'); END");
        db.execute("CREATE TRIGGER retain_graph_context_owners BEFORE DELETE ON graph_context_owners BEGIN SELECT RAISE(ABORT,'registered graph context authority is retained'); END");
        db.execute("CREATE TRIGGER context_segment_owner BEFORE INSERT ON agent_budget_segments BEGIN SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM agent_execution_budgets b JOIN runs r ON r.id=b.root_run_id WHERE b.root_run_id=NEW.root_run_id AND r.parent_run_id IS NULL AND r.state='running' AND ((b.policy_id='native.dynamic-plan' AND EXISTS(SELECT 1 FROM dynamic_capabilities a WHERE a.root_run_id=r.id)) OR (b.policy_id='native.graph-context' AND EXISTS(SELECT 1 FROM graph_context_owners g WHERE g.root_run_id=r.id)))) OR NEW.state!='open' OR NEW.active_elapsed_ms!=0 OR NEW.closed_event_seq IS NOT NULL OR NOT EXISTS(SELECT 1 FROM events e WHERE e.seq=NEW.opened_event_seq AND e.run_id=NEW.root_run_id AND e.kind='budget.segment.opened' AND json_extract(e.payload,'$.segment_id')=NEW.id AND json_extract(e.payload,'$.ordinal')=NEW.ordinal AND json_extract(e.payload,'$.remaining_active_ms')=NEW.remaining_active_ms) THEN RAISE(ABORT,'active segment lacks actual typed owner and opening event') END; END");
        db.execute("ALTER TABLE agent_model_call_reservations ADD COLUMN purpose TEXT NOT NULL DEFAULT 'inference' CHECK(purpose IN ('inference','context_compaction'))");
        db.execute("ALTER TABLE agent_model_call_reservations ADD COLUMN inference_step_id TEXT");
        db.execute("CREATE TRIGGER context_attempt_identity BEFORE UPDATE OF purpose,inference_step_id ON agent_model_call_reservations WHEN NEW.purpose!=OLD.purpose OR OLD.inference_step_id IS NOT NULL OR NEW.inference_step_id IS NULL OR OLD.state!='reserved' OR OLD.purpose!='inference' OR NOT EXISTS(SELECT 1 FROM inference_steps s WHERE s.id=NEW.inference_step_id AND s.root_run_id=OLD.root_run_id AND s.owner_run_id=OLD.owner_run_id AND s.original_attempt_id=OLD.attempt_id AND s.role=OLD.role AND s.state='reserved' AND s.planning_call_id IS NOT NULL AND EXISTS(SELECT 1 FROM dynamic_plan_calls c WHERE c.id=s.planning_call_id AND c.root_run_id=s.root_run_id AND c.state='turn_committed' AND c.continuation_attempt_id=OLD.attempt_id)) BEGIN SELECT RAISE(ABORT,'model purpose and logical membership are immutable'); END");
        db.execute("CREATE TRIGGER context_maintenance_no_assistant BEFORE UPDATE OF actual_assistant_json ON agent_model_call_reservations WHEN NEW.purpose!='inference' BEGIN SELECT RAISE(ABORT,'maintenance cannot originate an assistant receipt'); END");
        db.execute("CREATE TABLE context_scopes(kind TEXT NOT NULL CHECK(kind IN ('session','execution')),id TEXT NOT NULL,next_ordinal INTEGER NOT NULL DEFAULT 1 CHECK(next_ordinal>0),indexed_message_seq INTEGER NOT NULL DEFAULT 0 CHECK(indexed_message_seq>=0),PRIMARY KEY(kind,id))");
        db.execute("CREATE TABLE conversation_groups(scope_kind TEXT NOT NULL,scope_id TEXT NOT NULL,ordinal INTEGER NOT NULL CHECK(ordinal>0),first_message_seq INTEGER NOT NULL REFERENCES messages(seq),last_message_seq INTEGER NOT NULL REFERENCES messages(seq),execution_owner TEXT REFERENCES runs(id),committed_event_seq INTEGER REFERENCES events(seq),kind TEXT NOT NULL CHECK(kind IN ('user','assistant','tool_turn')),message_count INTEGER NOT NULL CHECK(message_count>0),payload_bytes INTEGER NOT NULL CHECK(payload_bytes>=0),source_binding TEXT NOT NULL CHECK(length(source_binding)=64),legacy_unattributed INTEGER NOT NULL CHECK(legacy_unattributed IN (0,1)),PRIMARY KEY(scope_kind,scope_id,ordinal),FOREIGN KEY(scope_kind,scope_id) REFERENCES context_scopes(kind,id),CHECK(last_message_seq>=first_message_seq AND message_count<=last_message_seq-first_message_seq+1))");
        db.execute("CREATE TABLE context_group_members(scope_kind TEXT NOT NULL,scope_id TEXT NOT NULL,ordinal INTEGER NOT NULL,position INTEGER NOT NULL CHECK(position>=0),message_seq INTEGER NOT NULL UNIQUE REFERENCES messages(seq),PRIMARY KEY(scope_kind,scope_id,ordinal,position),FOREIGN KEY(scope_kind,scope_id,ordinal) REFERENCES conversation_groups(scope_kind,scope_id,ordinal))");
        db.execute("CREATE INDEX context_latest_user ON conversation_groups(scope_kind,scope_id,kind,ordinal DESC)");
        db.execute("CREATE TABLE context_heads(scope_kind TEXT NOT NULL,scope_id TEXT NOT NULL,binding_key TEXT NOT NULL,binding_json TEXT NOT NULL CHECK(json_valid(binding_json)),revision INTEGER NOT NULL DEFAULT 0 CHECK(revision>=0),covered_ordinal INTEGER NOT NULL DEFAULT 0 CHECK(covered_ordinal>=0),checkpoint_id TEXT,PRIMARY KEY(scope_kind,scope_id,binding_key),FOREIGN KEY(scope_kind,scope_id) REFERENCES context_scopes(kind,id),CHECK((revision=0 AND checkpoint_id IS NULL AND covered_ordinal=0) OR (revision>0 AND checkpoint_id IS NOT NULL AND covered_ordinal>0)))");
        db.execute("CREATE TABLE context_idle_owners(id TEXT PRIMARY KEY NOT NULL,session_id TEXT NOT NULL REFERENCES sessions(id),manual_request_id TEXT NOT NULL UNIQUE,binding_json TEXT NOT NULL CHECK(json_valid(binding_json)),head_revision INTEGER NOT NULL CHECK(head_revision>=0),state TEXT NOT NULL CHECK(state IN ('active','completed','failed','interrupted')),max_calls INTEGER NOT NULL CHECK(max_calls BETWEEN 1 AND 4),calls_reserved INTEGER NOT NULL CHECK(calls_reserved BETWEEN 0 AND max_calls),wall_limit_ms INTEGER NOT NULL CHECK(wall_limit_ms BETWEEN 1 AND 120000),opened_ms INTEGER NOT NULL,elapsed_ms INTEGER CHECK(elapsed_ms>=0),failure_code TEXT)");
        db.execute("CREATE UNIQUE INDEX one_idle_context_owner ON context_idle_owners(session_id) WHERE state='active'");
        db.execute("CREATE TABLE context_compactions(id TEXT PRIMARY KEY NOT NULL,maintenance_attempt_id TEXT NOT NULL UNIQUE,scope_kind TEXT NOT NULL,scope_id TEXT NOT NULL,binding_key TEXT NOT NULL,binding_json TEXT NOT NULL CHECK(json_valid(binding_json)),state TEXT NOT NULL CHECK(state IN ('reserved','started','received','committed','failed','interrupted')),head_revision INTEGER NOT NULL,source_watermark INTEGER NOT NULL,covered_ordinal INTEGER NOT NULL,protected_from INTEGER NOT NULL,source_binding TEXT NOT NULL,tail_binding TEXT NOT NULL,user_binding TEXT NOT NULL,protected_users_json TEXT NOT NULL CHECK(json_valid(protected_users_json)),input_json TEXT NOT NULL CHECK(json_valid(input_json)),input_binding TEXT NOT NULL,snapshot_json TEXT NOT NULL CHECK(json_valid(snapshot_json)),policy_json TEXT NOT NULL CHECK(json_valid(policy_json)),admission_json TEXT NOT NULL CHECK(json_valid(admission_json)),root_run_id TEXT REFERENCES runs(id),owner_run_id TEXT REFERENCES runs(id),idle_owner_id TEXT REFERENCES context_idle_owners(id),inference_step_id TEXT,inference_attempt_id TEXT,manual_request_id TEXT,opened_ms INTEGER NOT NULL,elapsed_ms INTEGER CHECK(elapsed_ms>=0),failure_code TEXT,FOREIGN KEY(scope_kind,scope_id,binding_key) REFERENCES context_heads(scope_kind,scope_id,binding_key),CHECK((idle_owner_id IS NOT NULL AND root_run_id IS NULL AND owner_run_id IS NULL AND inference_step_id IS NULL AND inference_attempt_id IS NULL) OR (idle_owner_id IS NULL AND root_run_id IS NOT NULL AND owner_run_id IS NOT NULL AND inference_step_id IS NOT NULL AND inference_attempt_id IS NOT NULL)))");
        db.execute("CREATE UNIQUE INDEX one_context_maintenance ON context_compactions(scope_kind,scope_id) WHERE state IN ('reserved','started','received')");
        db.execute("CREATE TABLE context_events(seq INTEGER PRIMARY KEY AUTOINCREMENT,scope_kind TEXT NOT NULL,scope_id TEXT NOT NULL,owner_id TEXT NOT NULL,kind TEXT NOT NULL,payload TEXT NOT NULL CHECK(json_valid(payload)))");
        db.execute("CREATE TABLE context_checkpoints(id TEXT PRIMARY KEY NOT NULL,compaction_id TEXT NOT NULL UNIQUE REFERENCES context_compactions(id),projection_json TEXT NOT NULL CHECK(json_valid(projection_json)),actual_response_json TEXT NOT NULL CHECK(json_valid(actual_response_json)),actual_usage_json TEXT NOT NULL CHECK(json_valid(actual_usage_json)),provider_response_id TEXT,provider_created_at INTEGER,visible_summary TEXT,measured_elapsed_ms INTEGER NOT NULL CHECK(measured_elapsed_ms>=0),kind TEXT NOT NULL CHECK(kind IN ('responses_canonical','structured_summary')))");
        db.execute("CREATE TABLE context_manual_requests(id TEXT PRIMARY KEY NOT NULL,actor TEXT NOT NULL,scope_kind TEXT NOT NULL,scope_id TEXT NOT NULL,binding_key TEXT NOT NULL,binding_json TEXT NOT NULL CHECK(json_valid(binding_json)),expected_head_revision INTEGER NOT NULL CHECK(expected_head_revision>=0),state TEXT NOT NULL CHECK(state IN ('pending','claimed','completed','failed','interrupted')),owner_id TEXT,compaction_id TEXT UNIQUE REFERENCES context_compactions(id),failure_code TEXT,FOREIGN KEY(scope_kind,scope_id) REFERENCES context_scopes(kind,id))");
        db.execute("CREATE TABLE context_measures(id TEXT PRIMARY KEY NOT NULL,owner_id TEXT NOT NULL,root_run_id TEXT REFERENCES runs(id),scope_kind TEXT NOT NULL,scope_id TEXT NOT NULL,binding_key TEXT NOT NULL,request_json TEXT NOT NULL CHECK(json_valid(request_json)),state TEXT NOT NULL CHECK(state IN ('reserved','started','finished','failed','interrupted')),result_json TEXT CHECK(result_json IS NULL OR json_valid(result_json)),failure_code TEXT,opened_ms INTEGER NOT NULL,elapsed_ms INTEGER CHECK(elapsed_ms>=0))");
        db.execute("CREATE TABLE inference_steps(id TEXT PRIMARY KEY NOT NULL,root_run_id TEXT NOT NULL REFERENCES agent_execution_budgets(root_run_id),owner_run_id TEXT NOT NULL REFERENCES runs(id),original_attempt_id TEXT NOT NULL,role TEXT NOT NULL CHECK(role IN ('parent','leaf')),state TEXT NOT NULL CHECK(state IN ('reserved','started','completed','context_overflow','failed','cancelled','interrupted')),planning_call_id TEXT UNIQUE REFERENCES dynamic_plan_calls(id),successful_attempt_id TEXT,rebuild_count INTEGER NOT NULL DEFAULT 0 CHECK(rebuild_count BETWEEN 0 AND 1),spec_json TEXT NOT NULL CHECK(json_valid(spec_json)),FOREIGN KEY(root_run_id,original_attempt_id) REFERENCES agent_model_call_reservations(root_run_id,attempt_id) DEFERRABLE INITIALLY DEFERRED)");
        db.execute("CREATE TABLE inference_attempt_members(step_id TEXT NOT NULL REFERENCES inference_steps(id),root_run_id TEXT NOT NULL,attempt_id TEXT NOT NULL,ordinal INTEGER NOT NULL CHECK(ordinal BETWEEN 0 AND 1),binding_json TEXT NOT NULL CHECK(json_valid(binding_json)),input_binding TEXT NOT NULL,head_revision INTEGER,source_watermark INTEGER,outcome TEXT CHECK(outcome IN ('completed','context_overflow','failed','cancelled','interrupted')),failure_code TEXT,elapsed_ms INTEGER CHECK(elapsed_ms>=0),PRIMARY KEY(step_id,ordinal),UNIQUE(root_run_id,attempt_id),FOREIGN KEY(root_run_id,attempt_id) REFERENCES agent_model_call_reservations(root_run_id,attempt_id) DEFERRABLE INITIALLY DEFERRED,CHECK((input_binding='' AND binding_json='null' AND head_revision IS NULL AND source_watermark IS NULL) OR (length(input_binding)=64 AND binding_json!='null' AND head_revision>=0 AND source_watermark>=0)))");
        db.execute("CREATE TABLE inference_output_evidence(root_run_id TEXT NOT NULL,attempt_id TEXT NOT NULL,event_seq INTEGER NOT NULL UNIQUE REFERENCES events(seq),PRIMARY KEY(root_run_id,attempt_id,event_seq),FOREIGN KEY(root_run_id,attempt_id) REFERENCES agent_model_call_reservations(root_run_id,attempt_id))");
        db.execute("CREATE TRIGGER context_member_owner BEFORE INSERT ON context_group_members BEGIN SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM conversation_groups g JOIN messages m ON m.seq=NEW.message_seq WHERE g.scope_kind=NEW.scope_kind AND g.scope_id=NEW.scope_id AND g.ordinal=NEW.ordinal AND NEW.position<g.message_count AND m.seq BETWEEN g.first_message_seq AND g.last_message_seq AND ((g.scope_kind='session' AND m.session_id=g.scope_id AND m.execution_run_id IS NULL) OR (g.scope_kind='execution' AND m.execution_run_id=g.scope_id))) THEN RAISE(ABORT,'context group member differs from source ownership') END; END");
        db.execute("CREATE TRIGGER inference_member_owner BEFORE INSERT ON inference_attempt_members BEGIN SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM inference_steps s JOIN agent_model_call_reservations r ON r.root_run_id=s.root_run_id AND r.owner_run_id=s.owner_run_id WHERE s.id=NEW.step_id AND s.root_run_id=NEW.root_run_id AND r.attempt_id=NEW.attempt_id AND r.purpose='inference' AND r.inference_step_id=s.id AND r.role=s.role AND r.state='reserved') THEN RAISE(ABORT,'inference member lacks its original admitted owner') END; END");
        db.execute("CREATE TRIGGER inference_projection_once BEFORE UPDATE OF binding_json,input_binding,head_revision,source_watermark ON inference_attempt_members WHEN OLD.input_binding!='' OR OLD.binding_json!='null' OR NOT EXISTS(SELECT 1 FROM agent_model_call_reservations r WHERE r.root_run_id=OLD.root_run_id AND r.attempt_id=OLD.attempt_id AND r.state='reserved') BEGIN SELECT RAISE(ABORT,'inference projection is finalized once before dispatch'); END");
        db.execute("CREATE TRIGGER inference_member_identity BEFORE UPDATE OF step_id,root_run_id,attempt_id,ordinal ON inference_attempt_members BEGIN SELECT RAISE(ABORT,'physical inference membership is immutable'); END");
        db.execute("CREATE TRIGGER inference_step_identity BEFORE UPDATE OF id,root_run_id,owner_run_id,original_attempt_id,role,planning_call_id,spec_json ON inference_steps BEGIN SELECT RAISE(ABORT,'logical inference owner is immutable'); END");
        db.execute("CREATE TRIGGER context_head_install BEFORE UPDATE ON context_heads BEGIN SELECT CASE WHEN NEW.scope_kind!=OLD.scope_kind OR NEW.scope_id!=OLD.scope_id OR NEW.binding_key!=OLD.binding_key OR NEW.binding_json!=OLD.binding_json OR NEW.revision!=OLD.revision+1 OR NEW.covered_ordinal<=OLD.covered_ordinal OR NOT EXISTS(SELECT 1 FROM context_checkpoints k JOIN context_compactions c ON c.id=k.compaction_id WHERE k.id=NEW.checkpoint_id AND c.state='committed' AND c.scope_kind=OLD.scope_kind AND c.scope_id=OLD.scope_id AND c.binding_key=OLD.binding_key AND c.head_revision=OLD.revision AND c.covered_ordinal=NEW.covered_ordinal) THEN RAISE(ABORT,'context head requires exact committed checkpoint CAS') END; END");
        db.execute("CREATE TRIGGER context_compaction_identity BEFORE UPDATE OF id,maintenance_attempt_id,scope_kind,scope_id,binding_key,binding_json,head_revision,source_watermark,covered_ordinal,protected_from,source_binding,tail_binding,user_binding,protected_users_json,input_json,input_binding,snapshot_json,policy_json,admission_json,root_run_id,owner_run_id,idle_owner_id,inference_step_id,inference_attempt_id,manual_request_id,opened_ms ON context_compactions BEGIN SELECT RAISE(ABORT,'context admission is immutable'); END");
        db.execute("CREATE TRIGGER context_measure_identity BEFORE UPDATE OF id,owner_id,root_run_id,scope_kind,scope_id,binding_key,request_json,opened_ms ON context_measures BEGIN SELECT RAISE(ABORT,'context count identity is immutable'); END");
        db.execute("CREATE TRIGGER context_measure_result_once BEFORE UPDATE OF result_json ON context_measures WHEN OLD.result_json IS NOT NULL OR OLD.state!='started' OR NEW.state!='finished' BEGIN SELECT RAISE(ABORT,'context count result requires once-only started completion'); END");
        db.execute("CREATE TRIGGER inference_result_once BEFORE UPDATE OF outcome,failure_code,elapsed_ms ON inference_attempt_members WHEN OLD.outcome IS NOT NULL BEGIN SELECT RAISE(ABORT,'physical inference outcome is immutable'); END");
        db.execute("CREATE TRIGGER inference_success_once BEFORE UPDATE OF successful_attempt_id ON inference_steps WHEN OLD.successful_attempt_id IS NOT NULL OR NEW.successful_attempt_id IS NULL OR NOT EXISTS(SELECT 1 FROM inference_attempt_members m WHERE m.step_id=OLD.id AND m.attempt_id=NEW.successful_attempt_id AND m.outcome='completed') BEGIN SELECT RAISE(ABORT,'logical inference success must designate its completed physical member'); END");
        db.execute("CREATE TRIGGER context_checkpoint_origin BEFORE INSERT ON context_checkpoints BEGIN SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM context_compactions c WHERE c.id=NEW.compaction_id AND c.state='started' AND NEW.id=c.id) THEN RAISE(ABORT,'checkpoint requires actual started maintenance owner') END; END");
        for(const auto* table:{"context_compactions","context_heads","context_measures","context_manual_requests","context_idle_owners","inference_steps","inference_attempt_members","inference_output_evidence","context_events"})db.execute(std::string("CREATE TRIGGER retain_")+table+" BEFORE DELETE ON "+table+" BEGIN SELECT RAISE(ABORT,'context authority and observed ledger are retained'); END");
        for(const auto* table:{"conversation_groups","context_group_members","context_checkpoints"}){
            db.execute(std::string("CREATE TRIGGER immutable_")+table+" BEFORE UPDATE ON "+table+" BEGIN SELECT RAISE(ABORT,'context source and checkpoints are immutable'); END");
            db.execute(std::string("CREATE TRIGGER retain_")+table+" BEFORE DELETE ON "+table+" BEGIN SELECT RAISE(ABORT,'context evidence is retained'); END");
        }
        db.execute("CREATE TRIGGER context_original_message_immutable BEFORE UPDATE ON messages WHEN EXISTS(SELECT 1 FROM context_group_members g WHERE g.message_seq=OLD.seq) BEGIN SELECT RAISE(ABORT,'indexed original messages are immutable'); END");
        db.execute("CREATE TRIGGER context_original_message_retained BEFORE DELETE ON messages WHEN EXISTS(SELECT 1 FROM context_group_members g WHERE g.message_seq=OLD.seq) BEGIN SELECT RAISE(ABORT,'indexed original messages are retained'); END");
        db.execute("CREATE TRIGGER context_idle_excludes_runs BEFORE INSERT ON runs WHEN NEW.parent_run_id IS NULL AND EXISTS(SELECT 1 FROM context_idle_owners o WHERE o.session_id=NEW.session_id AND o.state='active') BEGIN SELECT RAISE(ABORT,'session has an exclusive context owner'); END");
        db.execute("CREATE TRIGGER context_idle_admission BEFORE INSERT ON context_idle_owners WHEN EXISTS(SELECT 1 FROM runs r WHERE r.session_id=NEW.session_id AND r.parent_run_id IS NULL AND r.state IN ('queued','running','paused')) BEGIN SELECT RAISE(ABORT,'session has active inference'); END");
        db.execute("ALTER TABLE context_compactions ADD COLUMN preparation_elapsed_ms INTEGER CHECK(preparation_elapsed_ms>=0)");
        db.execute("ALTER TABLE context_compactions ADD COLUMN before_measure_id TEXT REFERENCES context_measures(id)");
        db.execute("ALTER TABLE context_compactions ADD COLUMN after_measure_id TEXT REFERENCES context_measures(id)");
        db.execute("ALTER TABLE context_compactions ADD COLUMN prospective_input_binding TEXT");
        db.execute("ALTER TABLE dynamic_owner_pauses ADD COLUMN context_pin TEXT CHECK(context_pin IS NULL OR (json_valid(context_pin) AND json_type(context_pin)='object'))");
        db.execute("CREATE TRIGGER context_compaction_transition BEFORE UPDATE OF state ON context_compactions BEGIN SELECT CASE WHEN NOT ((OLD.state='reserved' AND NEW.state IN ('started','failed','interrupted')) OR (OLD.state='started' AND NEW.state IN ('received','failed','interrupted')) OR (OLD.state='received' AND NEW.state IN ('committed','failed','interrupted'))) THEN RAISE(ABORT,'context lifecycle transition is invalid') END; SELECT CASE WHEN NEW.state='received' AND NOT EXISTS(SELECT 1 FROM context_checkpoints k WHERE k.compaction_id=OLD.id) THEN RAISE(ABORT,'received maintenance requires actual immutable receipt') END; SELECT CASE WHEN NEW.state='committed' AND NOT EXISTS(SELECT 1 FROM context_checkpoints k JOIN context_measures b ON b.id=NEW.before_measure_id JOIN context_measures a ON a.id=NEW.after_measure_id WHERE k.compaction_id=OLD.id AND b.id!=a.id AND b.state='finished' AND a.state='finished' AND b.owner_id=COALESCE(OLD.owner_run_id,OLD.idle_owner_id) AND a.owner_id=b.owner_id AND b.scope_kind=OLD.scope_kind AND a.scope_kind=OLD.scope_kind AND b.scope_id=OLD.scope_id AND a.scope_id=OLD.scope_id AND b.binding_key=OLD.binding_key AND a.binding_key=OLD.binding_key AND json_extract(b.request_json,'$.head')=OLD.head_revision AND json_extract(a.request_json,'$.head')=OLD.head_revision AND json_extract(b.request_json,'$.watermark')=OLD.source_watermark AND json_extract(a.request_json,'$.watermark')=OLD.source_watermark AND json_extract(a.request_json,'$.compaction')=OLD.id AND json_extract(a.request_json,'$.payload')=NEW.prospective_input_binding AND json_extract(a.request_json,'$.model')=json_extract(b.request_json,'$.model') AND json_extract(a.result_json,'$.semantics')='provider_exact' AND json_extract(b.result_json,'$.semantics')='provider_exact' AND json_extract(a.result_json,'$.value')<json_extract(b.result_json,'$.value')) THEN RAISE(ABORT,'context publication lacks exact compatible reduction receipts') END; END");
        db.execute("CREATE TRIGGER context_publication_once BEFORE UPDATE OF before_measure_id,after_measure_id,prospective_input_binding,preparation_elapsed_ms ON context_compactions WHEN OLD.state!='received' OR NEW.state!='committed' OR OLD.before_measure_id IS NOT NULL OR OLD.after_measure_id IS NOT NULL OR OLD.prospective_input_binding IS NOT NULL BEGIN SELECT RAISE(ABORT,'context publication bindings are write once'); END");
        db.execute("CREATE TRIGGER context_manual_identity BEFORE UPDATE OF id,actor,scope_kind,scope_id,binding_key,binding_json,expected_head_revision ON context_manual_requests BEGIN SELECT RAISE(ABORT,'authenticated manual context identity is immutable'); END");
        db.execute("CREATE TRIGGER context_manual_claim_once BEFORE UPDATE OF owner_id,compaction_id ON context_manual_requests WHEN OLD.state!='pending' OR OLD.compaction_id IS NOT NULL OR NOT ((NEW.state='pending' AND NEW.compaction_id IS NULL AND OLD.owner_id IS NULL AND EXISTS(SELECT 1 FROM context_idle_owners i WHERE i.id=NEW.owner_id AND i.state='active' AND i.manual_request_id=OLD.id AND i.session_id=OLD.scope_id AND OLD.scope_kind='session' AND i.binding_json=OLD.binding_json AND i.head_revision=OLD.expected_head_revision)) OR (NEW.state='claimed' AND (OLD.owner_id IS NULL OR OLD.owner_id=NEW.owner_id) AND EXISTS(SELECT 1 FROM context_compactions c WHERE c.id=NEW.compaction_id AND c.manual_request_id=OLD.id AND c.state='reserved' AND c.scope_kind=OLD.scope_kind AND c.scope_id=OLD.scope_id AND c.binding_key=OLD.binding_key AND COALESCE(c.owner_run_id,c.idle_owner_id)=NEW.owner_id))) BEGIN SELECT RAISE(ABORT,'manual request must bind once to its actual admitted owner'); END");
        db.execute("CREATE TRIGGER context_idle_identity BEFORE UPDATE OF id,session_id,manual_request_id,binding_json,head_revision,max_calls,wall_limit_ms,opened_ms ON context_idle_owners BEGIN SELECT RAISE(ABORT,'exclusive maintenance owner is immutable'); END");
        db.execute("CREATE TRIGGER context_idle_transition BEFORE UPDATE OF state ON context_idle_owners BEGIN SELECT CASE WHEN OLD.state!='active' OR NEW.state NOT IN ('completed','failed','interrupted') OR EXISTS(SELECT 1 FROM context_measures m WHERE m.owner_id=OLD.id AND m.state IN ('reserved','started')) OR (NEW.state='completed' AND NOT EXISTS(SELECT 1 FROM context_compactions c WHERE c.idle_owner_id=OLD.id AND c.state='committed')) OR (NEW.state!='completed' AND EXISTS(SELECT 1 FROM context_compactions c WHERE c.idle_owner_id=OLD.id AND c.state NOT IN ('failed','interrupted'))) THEN RAISE(ABORT,'idle context closure requires its drained actual ledger') END; END");
        db.execute("CREATE TRIGGER context_idle_manual BEFORE INSERT ON context_idle_owners BEGIN SELECT CASE WHEN EXISTS(SELECT 1 FROM runs WHERE id=NEW.id) OR NOT EXISTS(SELECT 1 FROM context_manual_requests q WHERE q.id=NEW.manual_request_id AND q.scope_kind='session' AND q.scope_id=NEW.session_id AND q.state='pending' AND q.owner_id IS NULL AND q.compaction_id IS NULL AND q.binding_json=NEW.binding_json AND q.expected_head_revision=NEW.head_revision) THEN RAISE(ABORT,'idle lease requires exact authenticated pending request') END; END");
        db.execute("PRAGMA user_version=12");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Context migration did not preserve foreign key ownership");
        // DROP/rename may leave an obsolete deferred violation counter even
        // after the replacement owns every exact referenced row. Validate the
        // WHOLE database first, then clear only that counter; perform no more
        // relational writes before commit. SQLite creator's explanation:
        // https://sqlite.org/forum/info/64fb781a226df95c0f4edc474e590214c28ae1b4b5ddeee8f7ec4ad77286796c
        db.execute("PRAGMA defer_foreign_keys=OFF");
        if(integer(db.execute("PRAGMA defer_foreign_keys").rows.at(0).at(0))!=0||integer(db.execute("PRAGMA foreign_keys").rows.at(0).at(0))!=1)throw DatabaseError("Context migration foreign key enforcement differs");
    }
    if(version<13)db.execute("PRAGMA user_version=13");
    if(version<14){db.execute("CREATE TABLE session_agents(session_id TEXT PRIMARY KEY NOT NULL REFERENCES sessions(id),selection_json TEXT NOT NULL CHECK(length(CAST(selection_json AS BLOB))<=65536 AND json_valid(selection_json) AND json_type(selection_json)='object'),revision INTEGER NOT NULL CHECK(revision>0))");db.execute("PRAGMA user_version=14");}
    if(version<15){
        db.execute("CREATE TABLE process_output_artifacts(operation_id TEXT PRIMARY KEY NOT NULL REFERENCES operations(id) ON DELETE CASCADE,run_id TEXT NOT NULL REFERENCES runs(id),state TEXT NOT NULL CHECK(state IN ('capturing','complete','incomplete')),stdout_bytes INTEGER NOT NULL DEFAULT 0 CHECK(stdout_bytes>=0),stderr_bytes INTEGER NOT NULL DEFAULT 0 CHECK(stderr_bytes>=0))");
        db.execute("CREATE TABLE process_output_chunks(operation_id TEXT NOT NULL REFERENCES process_output_artifacts(operation_id) ON DELETE CASCADE,channel TEXT NOT NULL CHECK(channel IN ('stdout','stderr')),byte_offset INTEGER NOT NULL CHECK(byte_offset>=0),data BLOB NOT NULL CHECK(length(data)>0 AND length(data)<=8192),PRIMARY KEY(operation_id,channel,byte_offset))");
        db.execute("PRAGMA user_version=15");
    }
    if(version<16){
        db.execute("CREATE TABLE session_forks(session_id TEXT PRIMARY KEY NOT NULL REFERENCES sessions(id),parent_session_id TEXT NOT NULL REFERENCES sessions(id),boundary_sequence INTEGER NOT NULL CHECK(boundary_sequence>0),boundary_type TEXT NOT NULL CHECK(boundary_type IN ('before','through')),CHECK(session_id!=parent_session_id))");
        db.execute("PRAGMA user_version=16");
    }
    if(version<17){
        // Direct agents need a durable root call/time allowance even when
        // they cannot delegate. Replace only the constrained budget table,
        // preserving every row and all dependent triggers/foreign keys.
        db.execute("PRAGMA defer_foreign_keys=ON");
        const auto budget_triggers=db.execute("SELECT name,sql FROM sqlite_master WHERE type='trigger' AND instr(sql,'agent_execution_budgets')>0 ORDER BY name").rows;
        for(const auto& row:budget_triggers){const auto name=text(row[0]);if(name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)throw DatabaseError("Invalid direct-agent budget migration trigger identity");db.execute("DROP TRIGGER \""+name+"\"");}
        db.execute("CREATE TABLE direct_budget_replacement(root_run_id TEXT PRIMARY KEY NOT NULL REFERENCES runs(id),policy_id TEXT NOT NULL,policy_revision INTEGER NOT NULL CHECK(policy_revision>0),workspace_identity TEXT NOT NULL,provider_identity_json TEXT NOT NULL CHECK(json_valid(provider_identity_json) AND json_type(provider_identity_json)='object'),max_children INTEGER NOT NULL CHECK(max_children BETWEEN 0 AND 8),max_parallel INTEGER NOT NULL CHECK(max_parallel BETWEEN 0 AND 2),max_model_calls INTEGER NOT NULL CHECK(max_model_calls BETWEEN 1 AND 32),wall_limit_ms INTEGER NOT NULL CHECK(wall_limit_ms BETWEEN 1 AND 3600000),children_admitted INTEGER NOT NULL DEFAULT 0 CHECK(children_admitted BETWEEN 0 AND max_children),model_calls_reserved INTEGER NOT NULL DEFAULT 0 CHECK(model_calls_reserved BETWEEN 0 AND max_model_calls),parent_calls_held INTEGER NOT NULL DEFAULT 0 CHECK(parent_calls_held>=0),revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),planned_children_reserved INTEGER NOT NULL DEFAULT 0 CHECK(planned_children_reserved>=0 AND planned_children_reserved+children_admitted<=max_children),CHECK(model_calls_reserved+parent_calls_held<=max_model_calls),CHECK((policy_id IN ('native.context','native.direct-agent') AND max_children=0 AND max_parallel=0) OR (policy_id IN ('native.delegation','native.dynamic-plan','native.graph-context') AND max_children BETWEEN 1 AND 8 AND max_parallel BETWEEN 1 AND 2)))");
        db.execute("INSERT INTO direct_budget_replacement SELECT root_run_id,policy_id,policy_revision,workspace_identity,provider_identity_json,max_children,max_parallel,max_model_calls,wall_limit_ms,children_admitted,model_calls_reserved,parent_calls_held,revision,planned_children_reserved FROM agent_execution_budgets");
        db.execute("DROP TABLE agent_execution_budgets");
        db.execute("ALTER TABLE direct_budget_replacement RENAME TO agent_execution_budgets");
        for(const auto& row:budget_triggers)db.execute(text(row[1]));
        db.execute("PRAGMA user_version=17");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Direct-agent budget migration did not preserve foreign key ownership");
        db.execute("PRAGMA defer_foreign_keys=OFF");
        if(integer(db.execute("PRAGMA defer_foreign_keys").rows.at(0).at(0))!=0||integer(db.execute("PRAGMA foreign_keys").rows.at(0).at(0))!=1)throw DatabaseError("Direct-agent budget migration foreign key enforcement differs");
    }
    if(version<18){
        db.execute("CREATE TABLE a2a_continuation_messages(message_id TEXT PRIMARY KEY NOT NULL,run_id TEXT NOT NULL REFERENCES runs(id),context_id TEXT NOT NULL REFERENCES sessions(id),identity TEXT NOT NULL CHECK(json_valid(identity) AND json_type(identity)='object'),content TEXT NOT NULL,payload TEXT NOT NULL CHECK(json_valid(payload) AND json_type(payload)='object'),human_request_id TEXT NOT NULL REFERENCES dynamic_human_requests(id),event_seq INTEGER NOT NULL REFERENCES events(seq),UNIQUE(run_id,human_request_id))");
        db.execute("CREATE INDEX a2a_continuation_task_history ON a2a_continuation_messages(run_id,event_seq)");db.execute("PRAGMA user_version=18");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("A2A continuation migration did not preserve foreign key ownership");
    }
    if(version<19){
        db.execute("CREATE TABLE session_inbox(id TEXT PRIMARY KEY NOT NULL,session_id TEXT NOT NULL REFERENCES sessions(id),content TEXT NOT NULL CHECK(length(content)>0),state TEXT NOT NULL CHECK(state IN ('queued','claimed')),created_ms INTEGER NOT NULL CHECK(created_ms>0),run_id TEXT UNIQUE REFERENCES runs(id),CHECK((state='queued' AND run_id IS NULL) OR (state='claimed' AND run_id IS NOT NULL)))");
        db.execute("CREATE INDEX session_inbox_order ON session_inbox(session_id,state,created_ms,id)");db.execute("PRAGMA user_version=19");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Session inbox migration did not preserve foreign key ownership");
    }
    if(version<20){
        db.execute("DROP INDEX session_inbox_order");
        db.execute("ALTER TABLE session_inbox RENAME TO session_inbox_v19");
        db.execute("CREATE TABLE session_inbox(id TEXT PRIMARY KEY NOT NULL,session_id TEXT NOT NULL REFERENCES sessions(id),content TEXT NOT NULL CHECK(length(content)>0),state TEXT NOT NULL CHECK(state IN ('queued','claimed','cancelled')),created_ms INTEGER NOT NULL CHECK(created_ms>0),run_id TEXT UNIQUE REFERENCES runs(id),CHECK((state='queued' AND run_id IS NULL) OR (state='claimed' AND run_id IS NOT NULL) OR (state='cancelled' AND run_id IS NULL)))");
        db.execute("INSERT INTO session_inbox(id,session_id,content,state,created_ms,run_id) SELECT id,session_id,content,state,created_ms,run_id FROM session_inbox_v19");
        db.execute("DROP TABLE session_inbox_v19");
        db.execute("CREATE INDEX session_inbox_order ON session_inbox(session_id,state,created_ms,id)");db.execute("PRAGMA user_version=20");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Session inbox cancellation migration did not preserve foreign key ownership");
    }
    if(version<21){
        db.execute("CREATE TABLE session_models(session_id TEXT PRIMARY KEY NOT NULL REFERENCES sessions(id),selection_json TEXT NOT NULL CHECK(length(CAST(selection_json AS BLOB))<=2048 AND json_valid(selection_json) AND json_type(selection_json)='object' AND json_type(selection_json,'$.version')='integer' AND json_extract(selection_json,'$.version')=1 AND json_type(selection_json,'$.model_id')='text' AND length(json_extract(selection_json,'$.model_id')) BETWEEN 1 AND 256 AND json_type(selection_json,'$.provider_profile_id')='text' AND length(json_extract(selection_json,'$.provider_profile_id')) BETWEEN 1 AND 256 AND json_type(selection_json,'$.provider_route_id')='text' AND length(json_extract(selection_json,'$.provider_route_id')) BETWEEN 1 AND 256),revision INTEGER NOT NULL CHECK(revision>0))");
        db.execute("PRAGMA user_version=21");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Session model migration did not preserve foreign key ownership");
    }
    if(version<22){
        db.execute("ALTER TABLE session_models RENAME TO session_models_v21");
        db.execute("CREATE TABLE session_models(session_id TEXT PRIMARY KEY NOT NULL REFERENCES sessions(id),selection_json TEXT CHECK(selection_json IS NULL OR (length(CAST(selection_json AS BLOB))<=2048 AND json_valid(selection_json) AND json_type(selection_json)='object' AND json_type(selection_json,'$.version')='integer' AND json_extract(selection_json,'$.version')=1 AND json_type(selection_json,'$.model_id')='text' AND length(json_extract(selection_json,'$.model_id')) BETWEEN 1 AND 256 AND json_type(selection_json,'$.provider_profile_id')='text' AND length(json_extract(selection_json,'$.provider_profile_id')) BETWEEN 1 AND 256 AND json_type(selection_json,'$.provider_route_id')='text' AND length(json_extract(selection_json,'$.provider_route_id')) BETWEEN 1 AND 256)),revision INTEGER NOT NULL CHECK(revision>0))");
        db.execute("INSERT INTO session_models SELECT session_id,selection_json,revision FROM session_models_v21");db.execute("DROP TABLE session_models_v21");db.execute("PRAGMA user_version=22");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Session model reset migration did not preserve foreign key ownership");
    }
    if(version<23){
        const auto extend_trigger=[&](const char* name,const std::string& before,const std::string& after){
            const auto rows=db.execute("SELECT sql FROM sqlite_master WHERE type='trigger' AND name=?",{name}).rows;
            if(rows.size()!=1)throw DatabaseError("Required dynamic ownership trigger is missing");
            auto sql=text(rows[0][0]);if(sql.find(after)!=std::string::npos)return;const auto position=sql.find(before);if(position==std::string::npos)throw DatabaseError(std::string("Dynamic ownership trigger has an unexpected definition: ")+name);
            sql.replace(position,before.size(),after);db.execute(std::string("DROP TRIGGER ")+name);db.execute(sql);
        };
        extend_trigger("owned_child_boundary","json_extract(n.definition_json,'$.type')='agent'","json_extract(n.definition_json,'$.type') IN ('agent','tool')");
        extend_trigger("dynamic_claim_boundary","json_extract(OLD.definition_json,'$.type')='agent'","json_extract(OLD.definition_json,'$.type') IN ('agent','tool')");
        db.execute("PRAGMA user_version=23");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Dynamic deterministic-tool migration did not preserve foreign key ownership");
    }
    if(version<24){
        db.execute("CREATE TABLE dynamic_plan_owners(plan_id TEXT PRIMARY KEY NOT NULL REFERENCES dynamic_plans(id),root_run_id TEXT NOT NULL REFERENCES agent_execution_budgets(root_run_id),owner_run_id TEXT NOT NULL UNIQUE REFERENCES runs(id),parent_plan_id TEXT,parent_node_label TEXT,depth INTEGER NOT NULL CHECK(depth BETWEEN 0 AND 8),FOREIGN KEY(parent_plan_id) REFERENCES dynamic_plans(id),FOREIGN KEY(parent_plan_id,parent_node_label) REFERENCES dynamic_plan_nodes(plan_id,label),CHECK((parent_plan_id IS NULL)=(parent_node_label IS NULL)))");
        db.execute("INSERT INTO dynamic_plan_owners(plan_id,root_run_id,owner_run_id,parent_plan_id,parent_node_label,depth) SELECT id,root_run_id,root_run_id,NULL,NULL,0 FROM dynamic_plans");
        db.execute("CREATE TRIGGER dynamic_plan_owner_admission BEFORE INSERT ON dynamic_plan_owners BEGIN SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM dynamic_plans p JOIN agent_execution_budgets b ON b.root_run_id=NEW.root_run_id JOIN dynamic_capabilities a ON a.root_run_id=NEW.root_run_id JOIN runs owner ON owner.id=NEW.owner_run_id WHERE p.id=NEW.plan_id AND p.root_run_id=NEW.root_run_id AND b.policy_id='native.dynamic-plan' AND a.catalogue_finalized=1 AND ((NEW.parent_plan_id IS NULL AND NEW.parent_node_label IS NULL AND NEW.owner_run_id=NEW.root_run_id AND NEW.depth=0 AND owner.parent_run_id IS NULL AND owner.state='running' AND NOT EXISTS(SELECT 1 FROM graph_roots g WHERE g.run_id=owner.id)) OR (NEW.parent_plan_id IS NOT NULL AND NEW.parent_node_label IS NOT NULL AND NEW.depth BETWEEN 1 AND 8 AND owner.state='running' AND EXISTS(SELECT 1 FROM dynamic_plan_owners parent JOIN dynamic_plans pp ON pp.id=parent.plan_id JOIN dynamic_plan_nodes n ON n.plan_id=parent.plan_id AND n.label=NEW.parent_node_label WHERE parent.plan_id=NEW.parent_plan_id AND parent.root_run_id=NEW.root_run_id AND parent.depth=NEW.depth-1 AND pp.state='active' AND n.state='claimed' AND n.child_run_id=owner.id AND json_extract(n.definition_json,'$.type')='agent' AND owner.parent_run_id=parent.owner_run_id)))) THEN RAISE(ABORT,'invalid dynamic plan owner scope') END; END");
        db.execute("CREATE TRIGGER dynamic_plan_owner_identity BEFORE UPDATE OF plan_id,root_run_id,owner_run_id,parent_plan_id,parent_node_label,depth ON dynamic_plan_owners BEGIN SELECT RAISE(ABORT,'dynamic plan owner scope is immutable'); END");
        db.execute("CREATE TRIGGER retain_dynamic_plan_owners BEFORE DELETE ON dynamic_plan_owners BEGIN SELECT RAISE(ABORT,'dynamic plan owner evidence cannot be deleted'); END");
        db.execute("CREATE INDEX dynamic_plan_owner_root_order ON dynamic_plan_owners(root_run_id,depth,plan_id)");
        db.execute("PRAGMA user_version=24");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Dynamic plan owner-scope migration did not preserve foreign key ownership");
    }
    if(version<25){
        // Existing plans are retained while removing the old one-plan-per-root
        // constraint. Child relations continue to reference the stable table
        // name; deferred checks cover the short replacement interval.
        std::string migration_step="defer foreign keys";
        try{
            db.execute("PRAGMA defer_foreign_keys=ON");
            std::vector<std::pair<std::string,std::string>> plan_triggers;
            migration_step="collect dependent triggers";
            for(const auto& row:db.execute("SELECT name,sql FROM sqlite_master WHERE type='trigger' AND (tbl_name='dynamic_plans' OR instr(sql,'dynamic_plans')>0) ORDER BY name").rows){
                const auto name=text(row[0]);if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)throw DatabaseError("Dynamic plan migration found an invalid trigger identity");
                plan_triggers.emplace_back(name,text(row[1]));db.execute("DROP TRIGGER \""+name+"\"");
            }
            migration_step="preserve current plans";
            db.execute("CREATE TABLE dynamic_plan_replacement AS SELECT id,root_run_id,revision,state_sequence,state,retired_labels_json,planned_children_reserved,planned_humans_reserved,humans_published FROM dynamic_plans");
            migration_step="drop old plan table";
            db.execute("DROP TABLE dynamic_plans");
            migration_step="create owner-scoped plan table";
            db.execute("CREATE TABLE dynamic_plans(id TEXT PRIMARY KEY NOT NULL,root_run_id TEXT NOT NULL REFERENCES dynamic_capabilities(root_run_id),revision INTEGER NOT NULL CHECK(revision>0),state_sequence INTEGER NOT NULL CHECK(state_sequence>0),state TEXT NOT NULL CHECK(state IN ('active','waiting_human','completed','failed','cancelled','uncertain','interrupted')),retired_labels_json TEXT NOT NULL CHECK(json_valid(retired_labels_json) AND json_type(retired_labels_json)='array'),planned_children_reserved INTEGER NOT NULL CHECK(planned_children_reserved>=0),planned_humans_reserved INTEGER NOT NULL CHECK(planned_humans_reserved>=0),humans_published INTEGER NOT NULL DEFAULT 0 CHECK(humans_published>=0))");
            migration_step="restore plans";
            db.execute("INSERT INTO dynamic_plans SELECT id,root_run_id,revision,state_sequence,state,retired_labels_json,planned_children_reserved,planned_humans_reserved,humans_published FROM dynamic_plan_replacement");
            db.execute("DROP TABLE dynamic_plan_replacement");
            migration_step="restore root index";
            db.execute("CREATE INDEX dynamic_plans_root ON dynamic_plans(root_run_id,id)");
            migration_step="restore dependent triggers";
            for(const auto& trigger:plan_triggers)db.execute(trigger.second);
            db.execute("PRAGMA user_version=25");
            migration_step="validate foreign keys";
            if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Dynamic plan multiplicity migration did not preserve foreign key ownership");
            db.execute("PRAGMA defer_foreign_keys=OFF");
            if(integer(db.execute("PRAGMA defer_foreign_keys").rows.at(0).at(0))!=0||integer(db.execute("PRAGMA foreign_keys").rows.at(0).at(0))!=1)throw DatabaseError("Dynamic plan multiplicity migration lost foreign key enforcement");
        }catch(const DatabaseError& error){throw DatabaseError("Dynamic plan schema v25 migration failed at "+migration_step+": "+error.what());}
    }
    if(version<26){
        // Signed planning calls belong to the model-turn owner. Root identity
        // remains separately durable for shared limits and budget accounting.
        std::string migration_step="defer foreign keys";
        try{
            db.execute("PRAGMA defer_foreign_keys=ON");
            std::vector<std::pair<std::string,std::string>> call_triggers;
            migration_step="collect dependent triggers";
            for(const auto& row:db.execute("SELECT name,sql FROM sqlite_master WHERE type='trigger' AND (tbl_name='dynamic_plan_calls' OR instr(sql,'dynamic_plan_calls')>0) ORDER BY name").rows){
                const auto name=text(row[0]);if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)throw DatabaseError("Dynamic call migration found an invalid trigger identity");
                call_triggers.emplace_back(name,text(row[1]));db.execute("DROP TRIGGER \""+name+"\"");
            }
            migration_step="preserve signed calls and immutable owners";
            db.execute("CREATE TABLE dynamic_call_replacement AS SELECT c.id,c.plan_id,c.root_run_id,o.owner_run_id,c.provider_tool_call_id,c.origin_attempt_id,c.arguments_json,c.parent_assistant_json,c.state,c.accepted_revision,c.accepted_event_seq,c.result_json,c.result_event_seq,c.conversation_commit_seq,c.continuation_attempt_id FROM dynamic_plan_calls c JOIN dynamic_plan_owners o ON o.plan_id=c.plan_id");
            if(integer(db.execute("SELECT count(*) FROM dynamic_call_replacement").rows.at(0).at(0))!=integer(db.execute("SELECT count(*) FROM dynamic_plan_calls").rows.at(0).at(0)))throw DatabaseError("A signed call lacks its immutable plan owner");
            migration_step="replace root-scoped call relation";
            db.execute("DROP TABLE dynamic_plan_calls");
            db.execute("CREATE TABLE dynamic_plan_calls(id TEXT PRIMARY KEY NOT NULL,plan_id TEXT NOT NULL REFERENCES dynamic_plans(id),root_run_id TEXT NOT NULL REFERENCES dynamic_capabilities(root_run_id),owner_run_id TEXT NOT NULL REFERENCES runs(id),provider_tool_call_id TEXT NOT NULL,origin_attempt_id TEXT NOT NULL,arguments_json TEXT NOT NULL CHECK(json_valid(arguments_json) AND json_type(arguments_json)='object'),parent_assistant_json TEXT NOT NULL CHECK(json_valid(parent_assistant_json) AND json_type(parent_assistant_json)='object'),state TEXT NOT NULL CHECK(state IN ('accepted','report_ready','turn_committed','interrupted','cancelled')),accepted_revision INTEGER NOT NULL,accepted_event_seq INTEGER NOT NULL REFERENCES events(seq),result_json TEXT CHECK(result_json IS NULL OR (json_valid(result_json) AND json_type(result_json)='object')),result_event_seq INTEGER REFERENCES events(seq),conversation_commit_seq INTEGER REFERENCES events(seq),continuation_attempt_id TEXT,UNIQUE(owner_run_id,provider_tool_call_id),UNIQUE(owner_run_id,continuation_attempt_id),FOREIGN KEY(root_run_id,origin_attempt_id) REFERENCES agent_model_call_reservations(root_run_id,attempt_id),FOREIGN KEY(root_run_id,continuation_attempt_id) REFERENCES agent_model_call_reservations(root_run_id,attempt_id),CHECK((result_json IS NULL)=(result_event_seq IS NULL)),CHECK(conversation_commit_seq IS NULL OR result_event_seq IS NOT NULL))");
            migration_step="restore signed calls";
            db.execute("INSERT INTO dynamic_plan_calls SELECT * FROM dynamic_call_replacement");db.execute("DROP TABLE dynamic_call_replacement");
            db.execute("CREATE UNIQUE INDEX dynamic_one_pending_call ON dynamic_plan_calls(owner_run_id) WHERE state IN ('accepted','report_ready')");
            migration_step="restore dependent triggers";
            for(const auto& trigger:call_triggers)db.execute(trigger.second);
            db.execute("DROP TRIGGER dynamic_call_identity");
            db.execute("CREATE TRIGGER dynamic_call_identity BEFORE UPDATE OF id,plan_id,root_run_id,owner_run_id,provider_tool_call_id,origin_attempt_id,arguments_json,parent_assistant_json,accepted_revision,accepted_event_seq ON dynamic_plan_calls BEGIN SELECT RAISE(ABORT,'signed dynamic call is immutable'); END");
            db.execute("DROP TRIGGER dynamic_call_result");
            db.execute("CREATE TRIGGER dynamic_call_result BEFORE UPDATE OF result_json,result_event_seq ON dynamic_plan_calls BEGIN SELECT CASE WHEN OLD.result_json IS NOT NULL OR NOT EXISTS(SELECT 1 FROM events e WHERE e.seq=NEW.result_event_seq AND e.run_id=OLD.owner_run_id AND e.kind='plan.call.reported' AND e.payload=NEW.result_json) THEN RAISE(ABORT,'dynamic call result must be owned and write once') END; END");
            db.execute("DROP TRIGGER dynamic_call_commit");
            db.execute("CREATE TRIGGER dynamic_call_commit BEFORE UPDATE OF conversation_commit_seq ON dynamic_plan_calls BEGIN SELECT CASE WHEN OLD.conversation_commit_seq IS NOT NULL OR OLD.result_json IS NULL OR NOT EXISTS(SELECT 1 FROM events e WHERE e.seq=NEW.conversation_commit_seq AND e.run_id=OLD.owner_run_id AND e.kind='conversation.tool_turn' AND json_extract(e.payload,'$.plan_call_id')=OLD.id) THEN RAISE(ABORT,'dynamic conversation commit must be owned and write once') END; END");
            db.execute("DROP TRIGGER dynamic_call_tool_completion");
            db.execute("CREATE TRIGGER dynamic_call_tool_completion BEFORE UPDATE OF conversation_commit_seq ON dynamic_plan_calls BEGIN SELECT CASE WHEN (SELECT count(*) FROM events t WHERE t.run_id=OLD.owner_run_id AND t.kind IN ('tool.completed','tool.failed') AND json_extract(t.payload,'$.activity_id')=OLD.owner_run_id||':plan:'||OLD.provider_tool_call_id)!=1 OR NOT EXISTS(SELECT 1 FROM events t WHERE t.run_id=OLD.owner_run_id AND t.kind='tool.completed' AND t.seq<NEW.conversation_commit_seq AND t.seq>OLD.result_event_seq AND json_extract(t.payload,'$.activity_id')=OLD.owner_run_id||':plan:'||OLD.provider_tool_call_id AND json_extract(t.payload,'$.call_id')=OLD.provider_tool_call_id AND json_extract(t.payload,'$.name')=json_extract(OLD.parent_assistant_json,'$.tool_calls[0].name') AND json_extract(t.payload,'$.data')=OLD.result_json) THEN RAISE(ABORT,'planning conversation requires once-only actual tool completion') END; END");
            db.execute("DROP TRIGGER dynamic_call_continuation");
            db.execute("CREATE TRIGGER dynamic_call_continuation BEFORE UPDATE OF continuation_attempt_id ON dynamic_plan_calls BEGIN SELECT CASE WHEN OLD.continuation_attempt_id IS NOT NULL OR OLD.conversation_commit_seq IS NULL OR NOT EXISTS(SELECT 1 FROM agent_model_call_reservations r WHERE r.root_run_id=OLD.root_run_id AND r.attempt_id=NEW.continuation_attempt_id AND r.owner_run_id=OLD.owner_run_id AND r.role=CASE WHEN OLD.owner_run_id=OLD.root_run_id THEN 'parent' ELSE 'leaf' END AND r.state='reserved') THEN RAISE(ABORT,'dynamic continuation must be owned and write once') END; END");
            db.execute("DROP TRIGGER dynamic_signed_call_boundary");
            db.execute("CREATE TRIGGER dynamic_signed_call_boundary BEFORE INSERT ON dynamic_plan_calls BEGIN SELECT CASE WHEN NEW.state!='accepted' OR NEW.result_json IS NOT NULL OR NEW.result_event_seq IS NOT NULL OR NEW.conversation_commit_seq IS NOT NULL OR NEW.continuation_attempt_id IS NOT NULL OR json_array_length(NEW.parent_assistant_json,'$.tool_calls')!=1 OR json_extract(NEW.parent_assistant_json,'$.tool_calls[0].id')!=NEW.provider_tool_call_id OR json_extract(NEW.parent_assistant_json,'$.tool_calls[0].arguments')!=NEW.arguments_json OR json_extract(NEW.parent_assistant_json,'$.tool_calls[0].name')!=CASE WHEN NEW.accepted_revision=1 THEN 'plan_tasks' ELSE 'revise_plan' END OR NOT EXISTS(SELECT 1 FROM dynamic_plans p JOIN dynamic_plan_owners o ON o.plan_id=p.id JOIN runs r ON r.id=p.root_run_id JOIN runs owner ON owner.id=NEW.owner_run_id JOIN dynamic_capabilities a ON a.root_run_id=r.id JOIN agent_model_call_reservations m ON m.root_run_id=r.id JOIN events e ON e.seq=NEW.accepted_event_seq WHERE p.id=NEW.plan_id AND p.root_run_id=NEW.root_run_id AND o.root_run_id=NEW.root_run_id AND o.owner_run_id=NEW.owner_run_id AND p.revision=NEW.accepted_revision AND r.state='running' AND owner.state='running' AND a.catalogue_finalized=1 AND m.attempt_id=NEW.origin_attempt_id AND m.owner_run_id=NEW.owner_run_id AND m.role=CASE WHEN NEW.owner_run_id=NEW.root_run_id THEN 'parent' ELSE 'leaf' END AND m.state='finished' AND m.actual_assistant_json=NEW.parent_assistant_json AND e.run_id=NEW.owner_run_id AND e.kind='plan.call.accepted' AND json_extract(e.payload,'$.plan_call_id')=NEW.id AND EXISTS(SELECT 1 FROM events t WHERE t.run_id=NEW.owner_run_id AND t.kind='budget.model_call.finished' AND json_extract(t.payload,'$.attempt_id')=m.attempt_id AND json_extract(t.payload,'$.was_started')=1)) THEN RAISE(ABORT,'invalid signed dynamic call boundary') END; END");
            db.execute("PRAGMA user_version=26");
            migration_step="validate foreign keys";
            if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Dynamic call owner migration did not preserve foreign key ownership");
            db.execute("PRAGMA defer_foreign_keys=OFF");
            if(integer(db.execute("PRAGMA defer_foreign_keys").rows.at(0).at(0))!=0||integer(db.execute("PRAGMA foreign_keys").rows.at(0).at(0))!=1)throw DatabaseError("Dynamic call owner migration lost foreign key enforcement");
        }catch(const DatabaseError& error){throw DatabaseError("Dynamic plan schema v26 migration failed at "+migration_step+": "+error.what());}
    }
    if(version<27){
        // Descendant plan owners are still ordinary root-owned child runs.
        // Their immutable parent plan/node chain, rather than runs.parent_id,
        // proves nested ownership at every admitted depth.
        db.execute("DROP TRIGGER dynamic_plan_owner_admission");
        db.execute("CREATE TRIGGER dynamic_plan_owner_admission BEFORE INSERT ON dynamic_plan_owners BEGIN SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM dynamic_plans p JOIN agent_execution_budgets b ON b.root_run_id=NEW.root_run_id JOIN dynamic_capabilities a ON a.root_run_id=NEW.root_run_id JOIN runs owner ON owner.id=NEW.owner_run_id WHERE p.id=NEW.plan_id AND p.root_run_id=NEW.root_run_id AND b.policy_id='native.dynamic-plan' AND a.catalogue_finalized=1 AND ((NEW.parent_plan_id IS NULL AND NEW.parent_node_label IS NULL AND NEW.owner_run_id=NEW.root_run_id AND NEW.depth=0 AND owner.parent_run_id IS NULL AND owner.state='running' AND NOT EXISTS(SELECT 1 FROM graph_roots g WHERE g.run_id=owner.id)) OR (NEW.parent_plan_id IS NOT NULL AND NEW.parent_node_label IS NOT NULL AND NEW.depth BETWEEN 1 AND 8 AND owner.state='running' AND owner.parent_run_id=NEW.root_run_id AND EXISTS(SELECT 1 FROM dynamic_plan_owners parent JOIN dynamic_plans pp ON pp.id=parent.plan_id JOIN dynamic_plan_nodes n ON n.plan_id=parent.plan_id AND n.label=NEW.parent_node_label WHERE parent.plan_id=NEW.parent_plan_id AND parent.root_run_id=NEW.root_run_id AND parent.depth=NEW.depth-1 AND pp.state='active' AND n.state='claimed' AND n.child_run_id=owner.id AND json_extract(n.definition_json,'$.type')='agent')))) THEN RAISE(ABORT,'invalid dynamic plan owner scope') END; END");
        db.execute("PRAGMA user_version=27");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Recursive dynamic plan owner migration did not preserve foreign key ownership");
    }
    if(version<28){
        // Explicit persistent development services are isolated by their own
        // durable process-service resource and must not block later workspace
        // effects. Every other executing effect retains the one-workspace cap.
        db.execute("DROP INDEX one_workspace_effect");
        db.execute("CREATE UNIQUE INDEX one_workspace_effect ON operations(workspace) WHERE state='executing' AND NOT(tool='run_process' AND json_extract(arguments,'$.background')=1 AND json_extract(arguments,'$.persistent')=1)");
        db.execute("PRAGMA user_version=28");
    }
    if(version<29){
        // A root pause may eventually own several independent nested plan
        // continuations. Keep the existing header as the pause/segment record
        // and migrate its current single wait into the normalized wait set.
        db.execute("CREATE TABLE dynamic_owner_pause_waits(root_run_id TEXT NOT NULL REFERENCES dynamic_capabilities(root_run_id),plan_id TEXT NOT NULL REFERENCES dynamic_plans(id),plan_call_id TEXT NOT NULL REFERENCES dynamic_plan_calls(id),state TEXT NOT NULL CHECK(state IN ('paused','resumed','retired')),PRIMARY KEY(root_run_id,plan_id,plan_call_id),UNIQUE(root_run_id,plan_id))");
        db.execute("INSERT INTO dynamic_owner_pause_waits(root_run_id,plan_id,plan_call_id,state) SELECT root_run_id,plan_id,plan_call_id,state FROM dynamic_owner_pauses");
        db.execute("CREATE TRIGGER retain_dynamic_owner_pause_waits BEFORE DELETE ON dynamic_owner_pause_waits BEGIN SELECT RAISE(ABORT,'dynamic durable evidence cannot be deleted'); END");
        db.execute("PRAGMA user_version=29");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Dynamic pause wait-set migration did not preserve foreign key ownership");
    }
    if(version<30){
        db.execute("CREATE TABLE IF NOT EXISTS deleted_sessions(id TEXT PRIMARY KEY NOT NULL,deleted_ms INTEGER NOT NULL CHECK(deleted_ms>0))");
        const auto tombstone_columns=db.execute("PRAGMA table_info(deleted_sessions)").rows;
        if(tombstone_columns.size()!=2||text(tombstone_columns[0][1])!="id"||text(tombstone_columns[1][1])!="deleted_ms"||integer(tombstone_columns[0][5])!=1||integer(tombstone_columns[1][3])!=1)
            throw DatabaseError("Existing session tombstone table has an incompatible schema");
        db.execute("PRAGMA user_version=30");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Session tombstone migration did not preserve repository ownership");
    }
    if(version<31){
        const auto columns=db.execute("PRAGMA table_info(session_inbox)").rows;auto has_column=[&](const std::string& name){return std::any_of(columns.begin(),columns.end(),[&](const auto& row){return text(row[1])==name;});};
        if(!has_column("files_json"))db.execute("ALTER TABLE session_inbox ADD COLUMN files_json TEXT NOT NULL DEFAULT '[]' CHECK(json_valid(files_json) AND json_type(files_json)='array' AND length(CAST(files_json AS BLOB))<=262144)");
        if(!has_column("workspace_id"))db.execute("ALTER TABLE session_inbox ADD COLUMN workspace_id TEXT NOT NULL DEFAULT '' CHECK(length(workspace_id)<=256)");
        if(!has_column("workspace_revision"))db.execute("ALTER TABLE session_inbox ADD COLUMN workspace_revision INTEGER NOT NULL DEFAULT 0 CHECK(workspace_revision>=0)");
        db.execute("PRAGMA user_version=31");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Queued attachment migration did not preserve repository ownership");
    }
    if(version<32){
        db.execute("CREATE TABLE session_agent_events(sequence INTEGER PRIMARY KEY AUTOINCREMENT,session_id TEXT NOT NULL REFERENCES sessions(id),agent_id TEXT NOT NULL CHECK(length(agent_id) BETWEEN 1 AND 64),previous_agent_id TEXT CHECK(previous_agent_id IS NULL OR length(previous_agent_id) BETWEEN 1 AND 64),created_ms INTEGER NOT NULL CHECK(created_ms>0))");
        db.execute("CREATE INDEX session_agent_events_cursor ON session_agent_events(session_id,sequence)");
        db.execute("PRAGMA user_version=32");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Session agent event migration did not preserve repository ownership");
    }
    if(version<33){
        db.execute("CREATE TABLE mcp_input_requests(id TEXT PRIMARY KEY NOT NULL,run_id TEXT NOT NULL REFERENCES runs(id),operation_id TEXT NOT NULL REFERENCES operations(id),server_config_id TEXT NOT NULL,config_revision INTEGER NOT NULL CHECK(config_revision>0),peer_tool TEXT NOT NULL,request_key TEXT NOT NULL,request_json TEXT NOT NULL CHECK(length(CAST(request_json AS BLOB))<=65536 AND json_valid(request_json) AND json_type(request_json)='object'),state TEXT NOT NULL CHECK(state IN ('waiting','answered','cancelled','expired')),response_json TEXT CHECK(response_json IS NULL OR (length(CAST(response_json AS BLOB))<=16384 AND json_valid(response_json) AND json_type(response_json)='object')),actor TEXT NOT NULL DEFAULT '',expires_ms INTEGER NOT NULL CHECK(expires_ms>0),published_event_seq INTEGER NOT NULL REFERENCES events(seq),settled_event_seq INTEGER REFERENCES events(seq),CHECK((state='waiting')=(settled_event_seq IS NULL)),CHECK((state='answered')=(response_json IS NOT NULL)),CHECK(state!='answered' OR length(actor)>0),CHECK(state='answered' OR actor=''))");
        db.execute("CREATE INDEX mcp_input_requests_run ON mcp_input_requests(run_id,state,expires_ms)");
        db.execute("CREATE TRIGGER mcp_input_request_insert BEFORE INSERT ON mcp_input_requests BEGIN SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM operations o JOIN runs r ON r.id=o.run_id JOIN events e ON e.seq=NEW.published_event_seq WHERE o.id=NEW.operation_id AND o.run_id=NEW.run_id AND o.tool='mcp_tool' AND o.state='executing' AND r.state='running' AND e.run_id=NEW.run_id AND e.kind='mcp.input.requested' AND json_extract(e.payload,'$.request_id')=NEW.id AND json_extract(e.payload,'$.operation_id')=NEW.operation_id AND json_extract(e.payload,'$.request_key')=NEW.request_key) THEN RAISE(ABORT,'MCP input request must be published by its executing run operation') END; END");
        db.execute("CREATE TRIGGER mcp_input_request_identity BEFORE UPDATE OF id,run_id,operation_id,server_config_id,config_revision,peer_tool,request_key,request_json,expires_ms,published_event_seq ON mcp_input_requests BEGIN SELECT RAISE(ABORT,'published MCP input binding is immutable'); END");
        db.execute("CREATE TRIGGER mcp_input_request_settlement BEFORE UPDATE OF state,response_json,actor,settled_event_seq ON mcp_input_requests BEGIN SELECT CASE WHEN OLD.state!='waiting' OR NEW.state='waiting' OR NOT EXISTS(SELECT 1 FROM events e WHERE e.seq=NEW.settled_event_seq AND e.run_id=OLD.run_id AND e.kind='mcp.input.'||NEW.state AND json_extract(e.payload,'$.request_id')=OLD.id AND json_extract(e.payload,'$.operation_id')=OLD.operation_id) THEN RAISE(ABORT,'MCP input response must be owned and write once') END; END");
        db.execute("CREATE TRIGGER retain_mcp_input_requests BEFORE DELETE ON mcp_input_requests BEGIN SELECT RAISE(ABORT,'MCP input request evidence cannot be deleted'); END");
        db.execute("PRAGMA user_version=33");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("MCP input request migration did not preserve repository ownership");
    }
    if(version<34){
        db.execute("CREATE TABLE graph_invocations(run_id TEXT PRIMARY KEY NOT NULL REFERENCES graph_roots(run_id),root_run_id TEXT NOT NULL REFERENCES graph_roots(run_id),parent_run_id TEXT NOT NULL REFERENCES graph_roots(run_id),depth INTEGER NOT NULL CHECK(depth BETWEEN 1 AND 3),CHECK(run_id!=root_run_id AND run_id!=parent_run_id))");
        // Earlier metadata admission could only create first-level invocations.
        db.execute("INSERT INTO graph_invocations SELECT c.id,p.id,p.id,1 FROM runs c JOIN graph_roots g ON g.run_id=c.id JOIN runs p ON p.id=c.parent_run_id JOIN graph_roots pg ON pg.run_id=p.id JOIN json_each(pg.specification,'$.nodes') n WHERE p.parent_run_id IS NULL AND c.session_id=p.session_id AND json_extract(n.value,'$.id')=c.node_id AND json_extract(n.value,'$.type')='graph' AND json_extract(n.value,'$.graph_id')=g.graph_id AND json_extract(n.value,'$.graph_revision')=g.graph_revision AND json_extract(n.value,'$.graph_spec')=g.specification");
        if(integer(db.execute("SELECT count(*) FROM graph_roots g JOIN runs r ON r.id=g.run_id WHERE r.parent_run_id IS NOT NULL").rows[0][0])!=integer(db.execute("SELECT count(*) FROM graph_invocations").rows[0][0]))throw DatabaseError("Existing graph invocation has an invalid immutable parent binding");
        db.execute("CREATE TRIGGER graph_invocation_admission BEFORE INSERT ON graph_invocations BEGIN SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM runs c JOIN graph_roots cg ON cg.run_id=c.id JOIN runs p ON p.id=c.parent_run_id JOIN graph_roots pg ON pg.run_id=p.id JOIN runs r ON r.id=NEW.root_run_id JOIN graph_roots rg ON rg.run_id=r.id JOIN json_each(pg.specification,'$.nodes') n WHERE c.id=NEW.run_id AND p.id=NEW.parent_run_id AND c.session_id=p.session_id AND c.session_id=r.session_id AND c.state='queued' AND p.state='running' AND r.parent_run_id IS NULL AND r.state='running' AND NOT EXISTS(SELECT 1 FROM graph_context_owners co WHERE co.root_run_id=r.id) AND ((p.id=r.id AND NEW.depth=1) OR EXISTS(SELECT 1 FROM graph_invocations pi WHERE pi.run_id=p.id AND pi.root_run_id=r.id AND NEW.depth=pi.depth+1)) AND json_extract(n.value,'$.id')=c.node_id AND json_extract(n.value,'$.type')='graph' AND json_extract(n.value,'$.graph_id')=cg.graph_id AND json_extract(n.value,'$.graph_revision')=cg.graph_revision AND json_extract(n.value,'$.graph_spec')=cg.specification) THEN RAISE(ABORT,'invalid immutable graph invocation ownership') END; END");
        db.execute("CREATE TRIGGER immutable_graph_invocations BEFORE UPDATE ON graph_invocations BEGIN SELECT RAISE(ABORT,'graph invocation ownership is immutable'); END");
        db.execute("CREATE TRIGGER retain_graph_invocations BEFORE DELETE ON graph_invocations BEGIN SELECT RAISE(ABORT,'graph invocation ownership evidence cannot be deleted'); END");
        auto trigger=text(db.execute("SELECT sql FROM sqlite_master WHERE type='trigger' AND name='owned_child_boundary'").rows.at(0).at(0));
        const std::string original="p.parent_run_id IS NULL";const auto position=trigger.find(original);if(position==std::string::npos)throw DatabaseError("Graph ownership migration found an incompatible child boundary");
        trigger.replace(position,original.size(),"(p.parent_run_id IS NULL OR EXISTS(SELECT 1 FROM graph_invocations gi JOIN runs root ON root.id=gi.root_run_id WHERE gi.run_id=p.id AND root.parent_run_id IS NULL AND root.state='running' AND root.session_id=NEW.session_id AND NOT EXISTS(SELECT 1 FROM graph_context_owners co WHERE co.root_run_id=root.id)))");
        db.execute("DROP TRIGGER owned_child_boundary");db.execute(trigger);db.execute("PRAGMA user_version=34");
        if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Graph invocation migration did not preserve repository ownership");
    }
    if(version<35){
        // Context invocations borrow the ultimate root's open clock; they never
        // acquire an independent budget or context owner.
        for(const auto& item:std::vector<std::pair<std::string,std::string>>{{"graph_invocation_admission","r.id"},{"owned_child_boundary","root.id"}}){
            auto sql=text(db.execute("SELECT sql FROM sqlite_master WHERE type='trigger' AND name=?",{item.first}).rows.at(0).at(0));
            const auto previous="NOT EXISTS(SELECT 1 FROM graph_context_owners co WHERE co.root_run_id="+item.second+")";const auto at=sql.find(previous);if(at==std::string::npos||sql.find(previous,at+previous.size())!=std::string::npos)throw DatabaseError("Context invocation migration found an incompatible owner boundary");
            sql.replace(at,previous.size(),"("+previous+" OR EXISTS(SELECT 1 FROM graph_context_owners co JOIN agent_budget_segments segment ON segment.root_run_id=co.root_run_id WHERE co.root_run_id="+item.second+" AND segment.state='open'))");
            db.execute("DROP TRIGGER "+item.first);db.execute(sql);
        }
        db.execute("PRAGMA user_version=35");if(!db.execute("PRAGMA foreign_key_check").rows.empty())throw DatabaseError("Context invocation migration did not preserve ownership");
    }
    // The prepared owner and all authorized migrations commit together. A
    // publication/commit failure cannot leave schema writes without ownership.
    if(bootstrap)stage_backend_owner(db,*startup_lease,startup_generation,bootstrap);
    if(legacy){verify_legacy_owner_saved_records(db);stage_legacy_owner(db,*startup_lease,startup_generation,*legacy);}
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
    const auto rows=impl_->database.execute("SELECT p.root_run_id,o.owner_run_id,COALESCE(o.parent_plan_id,''),COALESCE(o.parent_node_label,''),COALESCE(o.depth,-1),p.revision,p.state_sequence,p.state,p.retired_labels_json,p.planned_children_reserved,p.planned_humans_reserved,p.humans_published FROM dynamic_plans p LEFT JOIN dynamic_plan_owners o ON o.plan_id=p.id WHERE p.id=?",{id}).rows;if(rows.empty())throw NotFound("Dynamic plan not found");const auto& r=rows[0];if(std::holds_alternative<std::nullptr_t>(r[1])||integer(r[4])<0)throw DatabaseError("Dynamic plan lacks its immutable owner scope");
    DynamicPlanRecord p;p.id=id;p.root_run_id=text(r[0]);p.owner_run_id=text(r[1]);p.parent_plan_id=text(r[2]);p.parent_node_label=text(r[3]);p.depth=integer(r[4]);p.revision=integer(r[5]);p.state_sequence=integer(r[6]);p.state=text(r[7]);p.capabilities=dynamic_capabilities(p.root_run_id);p.retired_labels=Json::parse(text(r[8])).get<std::vector<std::string>>();p.planned_children_reserved=integer(r[9]);p.planned_humans_reserved=integer(r[10]);p.humans_published=integer(r[11]);
    for(const auto& n:impl_->database.execute("SELECT definition_json,backend_node_id,state,definition_revision,claim_revision,COALESCE(claim_id,''),COALESCE(child_run_id,''),COALESCE(human_request_id,''),COALESCE(child_state,''),COALESCE(outcome_json,''),settled_event_seq,effect_state,protected_definition,COALESCE(resolved_input_json,'{}') FROM dynamic_plan_nodes WHERE plan_id=? ORDER BY rowid",{id}).rows){
        DynamicNodeRecord v;v.definition=dynamic_definition(text(n[0]));v.backend_node_id=text(n[1]);v.state=dynamic_state(text(n[2]));v.definition_revision=integer(n[3]);v.claim_revision=integer(n[4]);v.claim_id=text(n[5]);v.child_run_id=text(n[6]);v.human_request_id=text(n[7]);v.child_state=text(n[8]);v.outcome_json=text(n[9]);if(!std::holds_alternative<std::nullptr_t>(n[10]))v.settled_event_seq=integer(n[10]);v.effect_state=text(n[11]);v.protected_definition=integer(n[12])!=0;v.resolved_input_json=text(n[13]);p.nodes.push_back(std::move(v));
    }return p;
}
std::optional<DynamicPlanRecord> Repository::dynamic_plan_for_root(const std::string& root){const auto owner=run(root);if(!owner.parent_id.empty()||owner.graph_root)throw std::invalid_argument("Root plan lookup requires an ordinary owning root");const auto rows=impl_->database.execute("SELECT plan_id FROM dynamic_plan_owners WHERE root_run_id=? AND owner_run_id=? AND depth=0",{root,root}).rows;if(rows.empty())return {};if(rows.size()!=1)throw DatabaseError("Ordinary root has multiple owned plan records");return dynamic_plan(text(rows[0][0]));}
std::optional<DynamicPlanRecord> Repository::dynamic_plan_for_owner(const std::string& owner){run(owner);const auto rows=impl_->database.execute("SELECT plan_id FROM dynamic_plan_owners WHERE owner_run_id=?",{owner}).rows;if(rows.empty())return {};return dynamic_plan(text(rows[0][0]));}
std::vector<DynamicPlanRecord> Repository::dynamic_plans_for_root(const std::string& root){
    const auto owner=run(root);if(!owner.parent_id.empty()||owner.graph_root)throw std::invalid_argument("Planning tree requires its ordinary owning root");
    std::vector<DynamicPlanRecord> result;for(const auto& row:impl_->database.execute("SELECT plan_id FROM dynamic_plan_owners WHERE root_run_id=? ORDER BY depth,plan_id",{root}).rows){auto plan=dynamic_plan(text(row[0]));if(plan.root_run_id!=root)throw DatabaseError("Planning tree read crossed its root owner");result.push_back(std::move(plan));}return result;
}
DynamicPlanCallRecord Repository::dynamic_plan_call(const std::string& id){
    const auto rows=impl_->database.execute("SELECT plan_id,root_run_id,owner_run_id,provider_tool_call_id,origin_attempt_id,arguments_json,parent_assistant_json,state,COALESCE(result_json,''),accepted_revision,accepted_event_seq,result_event_seq,conversation_commit_seq,COALESCE(continuation_attempt_id,'') FROM dynamic_plan_calls WHERE id=?",{id}).rows;if(rows.empty())throw NotFound("Dynamic plan call not found");const auto& r=rows[0];
    DynamicPlanCallRecord c;c.id=id;c.plan_id=text(r[0]);c.root_run_id=text(r[1]);c.owner_run_id=text(r[2]);c.provider_tool_call_id=text(r[3]);c.origin_attempt_id=text(r[4]);c.arguments_json=text(r[5]);c.parent_assistant_json=text(r[6]);c.state=text(r[7]);c.result_json=text(r[8]);c.accepted_revision=integer(r[9]);c.accepted_event_seq=integer(r[10]);if(!std::holds_alternative<std::nullptr_t>(r[11]))c.result_event_seq=integer(r[11]);if(!std::holds_alternative<std::nullptr_t>(r[12]))c.conversation_commit_seq=integer(r[12]);c.continuation_attempt_id=text(r[13]);return c;
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
    for(const auto* id:{&spec.id,&spec.plan_id,&spec.root_run_id,&spec.origin_attempt_id}){bounded_identity(*id,64);if(id->find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::invalid_argument("Invalid native plan identity");}if(!spec.owner_run_id.empty()){bounded_identity(spec.owner_run_id,64);if(spec.owner_run_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::invalid_argument("Invalid dynamic plan call owner");}bounded_identity(spec.provider_tool_call_id,256);private_identity(spec.backend_identity);
    const auto args=object_json(spec.arguments_json,262144),assistant_bytes=object_json(spec.parent_assistant_json,8*1024*1024);const auto assistant=Json::parse(assistant_bytes);
    const auto preliminary=Json::parse(args);if(!preliminary.contains("expected_revision")||!preliminary["expected_revision"].is_number_integer())throw std::invalid_argument("Planning requires an exact revision precondition");const bool initial=preliminary.at("expected_revision")==0;const auto change=parse_dynamic_plan_change(args,initial);
    if(!assistant.contains("tool_calls")||!assistant["tool_calls"].is_array()||assistant["tool_calls"].size()!=1)throw std::invalid_argument("Planning must own one complete assistant call");
    const auto& signed_call=assistant["tool_calls"][0];if(!signed_call.is_object()||!signed_call.contains("id")||!signed_call["id"].is_string()||signed_call["id"]!=spec.provider_tool_call_id||!signed_call.contains("name")||!signed_call["name"].is_string()||signed_call["name"]!=(initial?"plan_tasks":"revise_plan")||!signed_call.contains("arguments")||!signed_call["arguments"].is_string()||signed_call["arguments"].get<std::string>()!=args)throw std::invalid_argument("Planning call differs from its actual assistant DTO");
    auto& db=impl_->database;Transaction tx(db);const auto root=run(spec.root_run_id);const auto owner_id=spec.owner_run_id.empty()?root.id:spec.owner_run_id;const auto owner=run(owner_id);const auto budget=root_budget(root.id);const auto caps=dynamic_capabilities(root.id);
    std::string parent_plan_id,parent_node_label;std::int64_t depth=0;
    if(owner_id==root.id){if(owner.graph_root||!owner.parent_id.empty())throw DynamicPlanUnavailable("Dynamic root owner is not an ordinary Agent");}
    else {
        if(owner.parent_id!=root.id||owner.session_id!=root.session_id||owner.graph_root)throw DynamicPlanUnavailable("Nested planning owner is outside its admitted root");
        const auto admission=child_admission(owner_id);if(admission.kind!=ChildAdmissionKind::dynamic_agent||admission.root_run_id!=root.id)throw DynamicPlanUnavailable("Nested planning requires an admitted dynamic-agent child");
        const auto parent_plan=dynamic_plan(admission.plan_id);const auto claimed=std::find_if(parent_plan.nodes.begin(),parent_plan.nodes.end(),[&](const auto& n){return n.child_run_id==owner_id;});
        if(parent_plan.root_run_id!=root.id||parent_plan.state!="active"||claimed==parent_plan.nodes.end()||claimed->state!=DynamicNodeState::claimed||claimed->definition.kind!=DynamicNodeKind::agent||claimed->definition.label!=admission.node_label||claimed->claim_id!=admission.claim_id||claimed->definition_revision!=admission.definition_revision||claimed->claim_revision!=admission.claim_revision||owner.state!=RunState::running)
            throw DynamicPlanUnavailable("Nested planning owner is not the exact live claimed agent node");
        parent_plan_id=parent_plan.id;parent_node_label=claimed->definition.label;depth=parent_plan.depth+1;
        if(depth>8)throw RootBudgetExhausted("Nested planning depth limit exhausted");
    }
    if(root.state!=RunState::running||owner.state!=RunState::running||!caps.catalogue_finalized||caps.backend_identity!=spec.backend_identity)throw DynamicPlanUnavailable("Planning owner or frozen authority is unavailable");
    const auto origin=db.execute("SELECT state,COALESCE(actual_assistant_json,'') FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND role=?",{root.id,spec.origin_attempt_id,owner_id,owner_id==root.id?"parent":"leaf"}).rows;
    if(origin.empty()||text(origin[0][0])!="finished"||text(origin[0][1])!=assistant_bytes||db.execute("SELECT seq FROM events WHERE run_id=? AND kind='budget.model_call.finished' AND json_extract(payload,'$.attempt_id')=? AND json_extract(payload,'$.was_started')=1",{owner_id,spec.origin_attempt_id}).rows.empty())throw Conflict("Planning origin is not its exact finished actual owner response");
    const auto replay=db.execute("SELECT id FROM dynamic_plan_calls WHERE owner_run_id=? AND provider_tool_call_id=?",{owner_id,spec.provider_tool_call_id}).rows;
    if(!replay.empty()){auto old=dynamic_plan_call(text(replay[0][0]));if(old.plan_id!=spec.plan_id||old.origin_attempt_id!=spec.origin_attempt_id||old.arguments_json!=args||old.parent_assistant_json!=assistant_bytes)throw Conflict("Planning call identity changed");if(old.state=="interrupted"||old.state=="cancelled")throw Conflict("Retired planning call cannot replay");settle_worker_planning(*this,db,old.root_run_id,old.owner_run_id,old.origin_attempt_id,old.parent_assistant_json,old.id,{});tx.commit();return old;}
    if(!db.execute("SELECT id FROM dynamic_plan_calls WHERE owner_run_id=? AND (state IN ('accepted','report_ready') OR (state='turn_committed' AND continuation_attempt_id IS NULL))",{owner_id}).rows.empty())throw Conflict("Previous planning call has no complete continuation");
    auto existing=dynamic_plan_for_owner(owner_id);DynamicPlanRecord p;if(existing)p=*existing;else {p.id=spec.plan_id;p.root_run_id=root.id;p.owner_run_id=owner_id;p.parent_plan_id=parent_plan_id;p.parent_node_label=parent_node_label;p.depth=depth;p.capabilities=caps;}
    if(p.id!=spec.plan_id)throw Conflict("Owner already has another immutable plan identity");dynamic_cas(p,change.expected_revision,change.expected_state_sequence,spec.backend_identity);
    const auto candidate=[&]{try{return reduce_dynamic_plan(p,change,caps);}catch(const Conflict&){throw std::invalid_argument("Requested planning revision is not admissible");}}();
    // Sibling owners may accept independent plans concurrently. Their budget
    // revisions can advance after this caller observes a snapshot, so use the
    // current row read under this BEGIN IMMEDIATE transaction and enforce every
    // root-wide limit atomically below instead of rejecting on that stale view.
    const std::string root_plan_ids="SELECT plan_id FROM dynamic_plan_owners WHERE root_run_id=?";
    const auto root_nodes=integer(db.execute("SELECT count(*) FROM dynamic_plan_nodes WHERE plan_id IN ("+root_plan_ids+")",{root.id}).rows.at(0).at(0));
    const auto prior_owner_nodes=existing?static_cast<std::int64_t>(p.nodes.size()):0;
    if(root_nodes-prior_owner_nodes+static_cast<std::int64_t>(candidate.nodes.size())>caps.max_nodes)
        throw RootBudgetExhausted("Root-wide dynamic plan node allowance exhausted");
    const auto root_revisions=integer(db.execute("SELECT count(*) FROM dynamic_plan_revisions WHERE plan_id IN ("+root_plan_ids+")",{root.id}).rows.at(0).at(0));
    if(root_revisions>=caps.max_revisions)throw RootBudgetExhausted("Root-wide dynamic plan revision allowance exhausted");
    const auto root_humans=integer(db.execute("SELECT COALESCE(sum(planned_humans_reserved+humans_published),0) FROM dynamic_plans WHERE id IN ("+root_plan_ids+")",{root.id}).rows.at(0).at(0));
    const auto prior_owner_humans=existing?existing->planned_humans_reserved:0;
    if(root_humans-prior_owner_humans+candidate.planned_humans_reserved>caps.max_humans)
        throw RootBudgetExhausted("Root-wide dynamic human-question allowance exhausted");
    const auto additional_children=candidate.planned_children_reserved-(existing?existing->planned_children_reserved:0);if(additional_children<0&&budget.planned_children_reserved< -additional_children)throw DatabaseError("Root planned-child reservation underflow");if(budget.children_admitted+budget.planned_children_reserved+additional_children>budget.spec.max_children||budget.model_calls_reserved+budget.parent_calls_held>=budget.spec.max_model_calls)throw RootBudgetExhausted("Plan cannot reserve child and continuation capacity");
    if(owner_id==root.id&&budget.parent_model_calls_reserved+budget.parent_calls_held>=caps.max_parent_turns)throw RootBudgetExhausted("Root planning-turn allowance exhausted");
    if(owner_id!=root.id){const auto admission=child_admission(owner_id);const auto parent_plan=dynamic_plan(admission.plan_id);const auto claimed=std::find_if(parent_plan.nodes.begin(),parent_plan.nodes.end(),[&](const auto& n){return n.child_run_id==owner_id;});const auto& preset=dynamic_preset(parent_plan.capabilities,claimed->definition);const auto inference_turns=integer(db.execute("SELECT count(DISTINCT COALESCE(inference_step_id,attempt_id)) FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=? AND role='leaf' AND purpose='inference'",{root.id,owner_id}).rows.at(0).at(0));if(inference_turns>=preset.turn_limit)throw RootBudgetExhausted("Nested planning has no child continuation turn remaining");}
    dynamic_safe_sequence(p.state_sequence);
    const auto revision=p.revision+1,sequence=p.state_sequence+1;
    if(!existing){changed_one(db.execute("INSERT INTO dynamic_plans(id,root_run_id,revision,state_sequence,state,retired_labels_json,planned_children_reserved,planned_humans_reserved) VALUES(?,?,?,?,'active',?,?,?)",{p.id,root.id,revision,sequence,Json(candidate.retired_labels).dump(),candidate.planned_children_reserved,candidate.planned_humans_reserved}));changed_one(db.execute("INSERT INTO dynamic_plan_owners(plan_id,root_run_id,owner_run_id,parent_plan_id,parent_node_label,depth) VALUES(?,?,?,?,?,?)",{p.id,root.id,owner_id,parent_plan_id.empty()?SqlValue(nullptr):SqlValue(parent_plan_id),parent_node_label.empty()?SqlValue(nullptr):SqlValue(parent_node_label),depth}));}
    else changed_one(db.execute("UPDATE dynamic_plans SET revision=?,state_sequence=?,state='active',retired_labels_json=?,planned_children_reserved=?,planned_humans_reserved=? WHERE id=? AND revision=? AND state_sequence=?",{revision,sequence,Json(candidate.retired_labels).dump(),candidate.planned_children_reserved,candidate.planned_humans_reserved,p.id,p.revision,p.state_sequence}));
    changed_one(db.execute("UPDATE agent_execution_budgets SET planned_children_reserved=planned_children_reserved+?,parent_calls_held=parent_calls_held+1,revision=revision+1 WHERE root_run_id=? AND revision=?",{additional_children,root.id,budget.revision}));
    const auto accepted=impl_->event(owner_id,"plan.call.accepted",Json{{"plan_id",p.id},{"plan_call_id",spec.id},{"revision",revision},{"state_sequence",sequence},{"provider_tool_call_id",spec.provider_tool_call_id},{"origin_attempt_id",spec.origin_attempt_id}}.dump());
    changed_one(db.execute("INSERT INTO dynamic_plan_calls(id,plan_id,root_run_id,owner_run_id,provider_tool_call_id,origin_attempt_id,arguments_json,parent_assistant_json,state,accepted_revision,accepted_event_seq) VALUES(?,?,?,?,?,?,?,?,'accepted',?,?)",{spec.id,p.id,root.id,owner_id,spec.provider_tool_call_id,spec.origin_attempt_id,args,assistant_bytes,revision,accepted.sequence}));
    changed_one(db.execute("INSERT INTO dynamic_plan_revisions(plan_id,revision,canonical_spec_json,plan_call_id,accepted_event_seq) VALUES(?,?,?,?,?)",{p.id,revision,candidate.canonical_spec_json,spec.id,accepted.sequence}));
    std::size_t position=0;for(auto n:candidate.nodes){if(n.backend_node_id.empty())n.backend_node_id=p.id+"_n"+std::to_string(position);++position;const auto definition=dynamic_definition_json(n.definition).dump();
        const auto old=db.execute("SELECT definition_json,definition_revision,state FROM dynamic_plan_nodes WHERE plan_id=? AND label=?",{p.id,n.definition.label}).rows;
        if(old.empty())changed_one(db.execute("INSERT INTO dynamic_plan_nodes(plan_id,label,backend_node_id,definition_json,definition_revision,state) VALUES(?,?,?,?,?,?)",{p.id,n.definition.label,n.backend_node_id,definition,n.definition_revision,dynamic_state_name(n.state)}));
        else if(text(old[0][0])!=definition||integer(old[0][1])!=n.definition_revision)changed_one(db.execute("UPDATE dynamic_plan_nodes SET definition_json=?,definition_revision=?,state=? WHERE plan_id=? AND label=? AND state IN ('pending','blocked') AND protected_definition=0",{definition,n.definition_revision,dynamic_state_name(n.state),p.id,n.definition.label}));
        else if(text(old[0][2])!=dynamic_state_name(n.state))changed_one(db.execute("UPDATE dynamic_plan_nodes SET state=? WHERE plan_id=? AND label=? AND state IN ('pending','blocked') AND protected_definition=0",{dynamic_state_name(n.state),p.id,n.definition.label}));
        changed_one(db.execute("INSERT INTO dynamic_revision_nodes(plan_id,revision,label,backend_node_id,definition_json,definition_revision) VALUES(?,?,?,?,?,?)",{p.id,revision,n.definition.label,n.backend_node_id,definition,n.definition_revision}));
    }
    for(const auto& n:candidate.nodes)for(const auto& e:n.definition.dependencies)changed_one(db.execute("INSERT INTO dynamic_plan_edges(plan_id,revision,label,dependency_label,requirement) VALUES(?,?,?,?,?)",{p.id,revision,n.definition.label,e.task,e.require==DynamicDependencyRequirement::success?"success":"observed"}));
    auto result=dynamic_plan_call(spec.id);settle_worker_planning(*this,db,result.root_run_id,result.owner_run_id,result.origin_attempt_id,result.parent_assistant_json,result.id,{});result.created=true;tx.commit();return result;
}
Session Repository::create_session(const std::string& id,const std::string& title) {
    identifier(id); Transaction transaction(impl_->database);
    if(!impl_->database.execute("SELECT id FROM sessions WHERE id=?",{id}).rows.empty()) throw Conflict("Session already exists");
    changed_one(impl_->database.execute("INSERT INTO sessions(id,title) VALUES(?,?)",{id,title}));
    transaction.commit(); return {id,title,{},{},{}};
}
Session Repository::import_session(const std::string& id,const std::string& title,const std::vector<Message>& messages,std::optional<AgentDefinition> agent) {
    identifier(id);if(title.empty()||title.size()>512||title.find('\0')!=std::string::npos||messages.size()>1000)throw std::invalid_argument("Invalid session archive");
    if(agent){validate_agent_tool_allowlist(agent->tools);validate_agent_permission_rules(agent->permissions);if(agent->id.empty()||agent->id.size()>64||agent->id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||agent->revision<1||agent->revision>9007199254740991LL||agent->model_id.size()>256||agent->model_id.find('\0')!=std::string::npos||agent->instructions.empty()||agent->instructions.size()>32768||agent->instructions.find('\0')!=std::string::npos||agent->name.size()>128||agent->name.find('\0')!=std::string::npos||agent->description.size()>2048||agent->description.find('\0')!=std::string::npos||(agent->mode!="all"&&agent->mode!="primary"&&agent->mode!="subagent"))throw std::invalid_argument("Invalid archived agent selection");}
    std::size_t total=0;
    for(const auto& message:messages){
        if(message.role!="user"&&message.role!="assistant"&&message.role!="tool")throw std::invalid_argument("Session archive contains an unsupported message role");
        if(message.json.empty()||message.json.size()>256*1024||total>4*1024*1024-message.json.size())throw std::invalid_argument("Session archive exceeds its message limits");
        total+=message.json.size();const auto payload=Json::parse(message.json);if(!payload.is_object())throw std::invalid_argument("Session archive message data must be an object");
    }
    auto& db=impl_->database;Transaction tx(db);
    if(!db.execute("SELECT id FROM sessions WHERE id=?",{id}).rows.empty())throw Conflict("Imported session identity already exists");
    changed_one(db.execute("INSERT INTO sessions(id,title) VALUES(?,?)",{id,title}));
    for(const auto& message:messages)changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,?,?)",{id,message.role,message.json}));
    if(agent){Json selected={{"id",agent->id},{"revision",agent->revision},{"model_id",agent->model_id},{"instructions",agent->instructions},{"name",agent->name.empty()?agent->id:agent->name},{"description",agent->description},{"mode",agent->mode},{"hidden",agent->hidden}};if(agent->tools)selected["tools"]=*agent->tools;if(!agent->permissions.empty()){selected["permissions"]=Json::array();for(const auto& rule:agent->permissions)selected["permissions"].push_back(Json{{"action",rule.action},{"resource",rule.resource},{"effect",rule.effect}});}changed_one(db.execute("INSERT INTO session_agents(session_id,selection_json,revision) VALUES(?,?,1)",{id,Json{{"version",1},{"agent",std::move(selected)}}.dump()}));}
    tx.commit();return {id,title,{},{},{}};
}
SessionArchive Repository::export_session(const std::string& id) {
    auto& db=impl_->database;Transaction tx(db);SessionArchive archive;archive.session=session(id);archive.agent=session_agent(id).selected;
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND state IN ('queued','running','paused') LIMIT 1",{id}).rows.empty())throw Conflict("Wait for the session to become idle before exporting it");
    for(const auto& row:db.execute("SELECT seq,role,payload FROM messages WHERE session_id=? AND execution_run_id IS NULL ORDER BY seq LIMIT 1001",{id}).rows){
        if(archive.messages.size()==1000)throw std::invalid_argument("Session exceeds the 1000-message export limit");
        archive.messages.push_back({integer(row[0]),text(row[1]),text(row[2])});
    }
    std::size_t total=0;for(const auto& message:archive.messages){if(message.json.size()>256*1024||total>4*1024*1024-message.json.size())throw std::invalid_argument("Session exceeds the 4 MiB export limit");total+=message.json.size();}
    tx.commit();return archive;
}
Session Repository::fork_session(const std::string& id,const std::string& source,std::optional<std::int64_t> before_sequence) {
    identifier(id);identifier(source);if(id==source|| (before_sequence&&*before_sequence<1))throw std::invalid_argument("Invalid session fork boundary");
    auto& db=impl_->database;Transaction tx(db);const auto parent=session(source);
    if(!db.execute("SELECT id FROM sessions WHERE id=?",{id}).rows.empty())throw Conflict("Fork session already exists");
    std::int64_t boundary=0;const std::string boundary_type=before_sequence?"before":"through";
    if(before_sequence){
        const auto found=db.execute("SELECT seq FROM messages WHERE session_id=? AND seq=? AND execution_run_id IS NULL",{source,*before_sequence}).rows;
        if(found.empty())throw NotFound("Fork boundary message not found in session");boundary=integer(found[0][0]);
    }else{
        const auto found=db.execute("SELECT max(seq) FROM messages WHERE session_id=? AND execution_run_id IS NULL",{source}).rows;
        if(found.empty()||std::holds_alternative<std::nullptr_t>(found[0][0]))throw NotFound("Cannot fork an empty session");boundary=integer(found[0][0]);
    }
    const auto fork_number=integer(db.execute("SELECT count(*) FROM session_forks WHERE parent_session_id=?",{source}).rows.at(0).at(0))+1;
    auto title=parent.title+" (fork #"+std::to_string(fork_number)+")";
    changed_one(db.execute("INSERT INTO sessions(id,title) VALUES(?,?)",{id,title}));
    const auto comparison=boundary_type=="before"?"<":"<=";
    const auto sql="INSERT INTO messages(session_id,role,payload) SELECT ?,m.role,m.payload FROM messages m WHERE m.session_id=? AND m.execution_run_id IS NULL AND m.seq"+std::string(comparison)+"? AND NOT EXISTS(SELECT 1 FROM task_messages t JOIN runs r ON r.id=t.run_id WHERE t.message_seq=m.seq AND r.state IN ('queued','running','paused')) ORDER BY m.seq";
    db.execute(sql,{id,source,boundary});
    db.execute("INSERT INTO session_agents(session_id,selection_json,revision) SELECT ?,selection_json,revision FROM session_agents WHERE session_id=?",{id,source});
    db.execute("INSERT INTO session_skills(session_id,workspace_id,selections_json,revision) SELECT ?,workspace_id,selections_json,revision FROM session_skills WHERE session_id=?",{id,source});
    db.execute("INSERT INTO session_models(session_id,selection_json,revision) SELECT ?,selection_json,revision FROM session_models WHERE session_id=?",{id,source});
    changed_one(db.execute("INSERT INTO session_forks(session_id,parent_session_id,boundary_sequence,boundary_type) VALUES(?,?,?,?)",{id,source,boundary,boundary_type}));
    const auto result=session(id);tx.commit();return result;
}
DynamicFrontierClaim Repository::admit_dynamic_frontier(const DynamicFrontierSpec& spec){
    if(spec.tasks.empty()||spec.tasks.size()>2)throw std::invalid_argument("Dynamic frontier must contain one or two native child nodes");auto& db=impl_->database;Transaction tx(db);auto p=dynamic_plan(spec.plan_id);dynamic_cas(p,spec.expected_revision,spec.expected_state_sequence,spec.backend_identity);const auto root=run(p.root_run_id);const auto budget=root_budget(root.id);const auto call=dynamic_plan_call(spec.plan_call_id);
    if(root.state!=RunState::running||p.state!="active"||call.plan_id!=p.id||call.state!="accepted")throw Conflict("Dynamic frontier lacks its running signed-call owner");if(budget.revision!=spec.expected_budget_revision)throw DynamicPlanChanged("Dynamic frontier budget changed");dynamic_safe_sequence(p.state_sequence);
    const auto active=integer(db.execute("SELECT count(*) FROM runs r WHERE r.parent_run_id=? AND r.state IN ('queued','running','paused') AND NOT EXISTS(SELECT 1 FROM dynamic_plan_owners o JOIN dynamic_plan_calls c ON c.plan_id=o.plan_id AND c.owner_run_id=o.owner_run_id WHERE o.owner_run_id=r.id AND c.state IN ('accepted','report_ready') AND c.conversation_commit_seq IS NULL)",{root.id}).rows.at(0).at(0));if(active+static_cast<std::int64_t>(spec.tasks.size())>budget.spec.max_parallel)throw Conflict("Dynamic frontier has no physical child capacity");
    std::set<std::string> labels,claims,children;std::vector<DynamicPreparedNode> prepared;
    for(const auto& task:spec.tasks){bounded_identity(task.claim_id,128);bounded_identity(task.child_run_id,128);if(!labels.insert(task.label).second||!claims.insert(task.claim_id).second||!children.insert(task.child_run_id).second)throw std::invalid_argument("Duplicate dynamic admission identities");
        auto n=prepare_dynamic_node(p,task.label);if((n.node.definition.kind!=DynamicNodeKind::agent&&n.node.definition.kind!=DynamicNodeKind::tool)||n.node.definition_revision!=task.definition_revision||task.resolved_input_json!=n.dependency_outputs_json)throw Conflict("Dynamic child differs from its actual prepared definition");
        const auto prompt=dynamic_prompt(n.node.definition,n.dependency_outputs_json,p.capabilities).dump();if(!task.prompt_json.empty()&&task.prompt_json!=prompt)throw Conflict("Dynamic child prompt is not derived from actual observations");prepared.push_back(std::move(n));}
    const auto count=static_cast<std::int64_t>(prepared.size());if(budget.planned_children_reserved<count||p.planned_children_reserved<count||budget.children_admitted+budget.planned_children_reserved>budget.spec.max_children)throw RootBudgetExhausted("Dynamic planned child capacity changed");
    changed_one(db.execute("UPDATE agent_execution_budgets SET planned_children_reserved=planned_children_reserved-?,children_admitted=children_admitted+?,revision=revision+1 WHERE root_run_id=? AND revision=?",{count,count,root.id,budget.revision}));
    changed_one(db.execute("UPDATE dynamic_plans SET planned_children_reserved=planned_children_reserved-?,state_sequence=state_sequence+1 WHERE id=? AND revision=? AND state_sequence=?",{count,p.id,p.revision,p.state_sequence}));
    for(std::size_t i=0;i<prepared.size();++i){const auto& task=spec.tasks[i];const auto& n=prepared[i].node;
        changed_one(db.execute("UPDATE dynamic_plan_nodes SET state='claimed',claim_revision=definition_revision,claim_id=?,child_run_id=?,resolved_input_json=?,protected_definition=1 WHERE plan_id=? AND label=? AND definition_revision=? AND state IN ('pending','blocked') AND claim_revision=0",{task.claim_id,task.child_run_id,prepared[i].dependency_outputs_json,p.id,task.label,task.definition_revision}));
        changed_one(db.execute("INSERT INTO runs(id,session_id,state,parent_run_id,node_id) VALUES(?,?,'queued',?,?)",{task.child_run_id,root.session_id,root.id,n.backend_node_id}));changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{task.child_run_id}));
        const Run child{task.child_run_id,root.session_id,RunState::queued,root.id,n.backend_node_id,false,root.provider_context_json};impl_->message(child,"user",dynamic_prompt(n.definition,prepared[i].dependency_outputs_json,p.capabilities).dump());impl_->event(child.id,"run.queued","{}");
        Json admission={{"plan_id",p.id},{"plan_call_id",call.id},{"label",task.label},{"claim_id",task.claim_id},{"child_run_id",child.id},{"definition_revision",n.definition_revision},{"node_type",dynamic_kind_name(n.definition.kind)}};
        if(n.definition.kind==DynamicNodeKind::agent){admission["preset_id"]=n.definition.preset_id;admission["preset_revision"]=n.definition.preset_revision;}
        else{admission["tool"]=n.definition.tool;admission["arguments_json"]=n.definition.arguments_json;}
        impl_->event(child.id,"plan.child.admitted",admission.dump());
    }
    DynamicFrontierClaim result;result.plan=dynamic_plan(p.id);result.budget=root_budget(root.id);for(const auto& task:spec.tasks)result.nodes.push_back(*std::find_if(result.plan.nodes.begin(),result.plan.nodes.end(),[&](const auto& n){return n.definition.label==task.label;}));result.created=true;tx.commit();return result;
}
DynamicNodeRecord Repository::settle_dynamic_child(const std::string& child_id) try {
    auto& db=impl_->database;Transaction tx(db);const auto rows=db.execute("SELECT plan_id,label FROM dynamic_plan_nodes WHERE child_run_id=?",{child_id}).rows;if(rows.empty())throw NotFound("Dynamic child not found");
    auto p=dynamic_plan(text(rows[0][0]));auto found=std::find_if(p.nodes.begin(),p.nodes.end(),[&](const auto& n){return n.child_run_id==child_id;});if(found==p.nodes.end())throw DatabaseError("Dynamic child ownership differs");auto n=*found;const auto child=run(child_id);const auto admitted=child_admission(child_id);
    const auto expected_admission=n.definition.kind==DynamicNodeKind::tool?ChildAdmissionKind::dynamic_tool:ChildAdmissionKind::dynamic_agent;
    if(admitted.kind!=expected_admission||admitted.plan_id!=p.id||child.parent_id!=p.root_run_id||child.node_id!=n.backend_node_id)throw Conflict("Dynamic child is not its exact claimed execution");
    if(n.settled_event_seq){tx.commit();return n;}if(n.state!=DynamicNodeState::claimed||!terminal(child.state))throw Conflict("Dynamic child has not physically retired");
    if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE owner_run_id=? AND state IN ('reserved','started')",{child_id}).rows.empty())throw Conflict("Dynamic child still owns model transport");
    const auto effect=dynamic_effect_state(operations(child_id));
    Json outcome={{"child_run_id",child_id},{"label",n.definition.label},{"child_state",state_name(child.state)},{"effect_state",effect}};
    if(n.definition.kind==DynamicNodeKind::agent){outcome["preset_id"]=n.definition.preset_id;outcome["preset_revision"]=n.definition.preset_revision;}
    if(child.state==RunState::completed&&n.definition.kind==DynamicNodeKind::tool){
        const auto started=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='tool.started' ORDER BY seq",{child_id}).rows;
        const auto completed=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='tool.completed' ORDER BY seq",{child_id}).rows;
        if(started.size()!=1||completed.size()!=1||integer(completed[0][0])<=integer(started[0][0]))throw DatabaseError("Completed dynamic tool lacks its single ordered actual receipt");
        const auto before=Json::parse(text(started[0][1])),after=Json::parse(text(completed[0][1]));
        if(before.value("name",std::string{})!=n.definition.tool||before.value("source",std::string{})!="dynamic_plan"||before.value("plan_node_id",std::string{})!=n.backend_node_id||before.value("arguments_json",std::string{})!=n.definition.arguments_json||
           after.value("name",std::string{})!=n.definition.tool||after.value("source",std::string{})!="dynamic_plan"||after.value("plan_node_id",std::string{})!=n.backend_node_id||!after.contains("data")||!after["data"].is_object())throw DatabaseError("Dynamic tool receipt differs from its immutable admitted definition");
        outcome["tool"]=n.definition.tool;outcome["data"]=after["data"];
    }else if(child.state==RunState::completed){const auto h=run_history(child_id);if(h.empty()||h.back().role!="assistant")throw DatabaseError("Completed dynamic agent lacks actual output");const auto answer=Json::parse(h.back().json);if(!answer.contains("content")||!answer["content"].is_string())throw DatabaseError("Invalid dynamic agent output");
        for(const auto* key:{"content","model","usage","elapsed_ms","first_token_ms"})if(answer.contains(key))outcome[key]=answer[key];
    }else {Json error=Json::object();const auto failure=db.execute("SELECT payload FROM events WHERE run_id=? AND kind=? ORDER BY seq DESC LIMIT 1",{child_id,"run."+state_name(child.state)}).rows;if(!failure.empty()){const auto source=Json::parse(text(failure[0][0]));for(const auto* key:{"reason","code","protocol_error_code"})if(source.contains(key)&&source[key].is_string()&&source[key].get<std::string>().size()<=256)error[key]=source[key];}outcome["error"]=error;}
    // A failed deterministic process still has an actual observed result.
    // Return that receipt to the planning owner, without relabelling its effect.
    if(child.state==RunState::failed&&n.definition.kind==DynamicNodeKind::tool&&n.definition.tool=="run_process"&&
       outcome.at("error").value("reason",std::string{})=="process_exit_nonzero"){
        const auto started=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='tool.started' ORDER BY seq",{child_id}).rows;
        const auto failed=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='tool.failed' ORDER BY seq",{child_id}).rows;
        if(started.size()!=1||failed.size()!=1||integer(failed[0][0])<=integer(started[0][0]))throw DatabaseError("Failed process child lacks its ordered actual receipt");
        const auto before=Json::parse(text(started[0][1])),after=Json::parse(text(failed[0][1]));
        if(before.value("name",std::string{})!=n.definition.tool||before.value("source",std::string{})!="dynamic_plan"||before.value("plan_node_id",std::string{})!=n.backend_node_id||before.value("arguments_json",std::string{})!=n.definition.arguments_json||
           after.value("name",std::string{})!=n.definition.tool||after.value("source",std::string{})!="dynamic_plan"||after.value("plan_node_id",std::string{})!=n.backend_node_id||!after.contains("data")||!after["data"].is_object())throw DatabaseError("Failed process receipt differs from its admitted definition");
        const auto& data=after.at("data");
        if(data.value("termination",std::string{})!="exited"||!data.contains("exit_code")||!data.at("exit_code").is_number_integer()||data.at("exit_code")==0)throw DatabaseError("Failed process receipt lacks an observed nonzero exit");
        outcome["tool"]=n.definition.tool;outcome["data"]=data;
    }
    if(outcome.dump().size()>32768)outcome={{"child_run_id",child_id},{"label",n.definition.label},{"child_state",state_name(child.state)},{"preset_id",n.definition.preset_id},{"preset_revision",n.definition.preset_revision},{"effect_state",effect},{"error",{{"code","result_limit_exceeded"}}},{"history_ref","/v1/runs/"+p.root_run_id+"/children/"+child_id+"/history"}};
    dynamic_safe_sequence(p.state_sequence);const auto event=impl_->event(child_id,"plan.child.settled",outcome.dump());
    changed_one(db.execute("UPDATE dynamic_plan_nodes SET state=?,child_state=?,outcome_json=?,settled_event_seq=?,effect_state=? WHERE plan_id=? AND label=? AND state='claimed' AND outcome_json IS NULL",{effect=="uncertain"?"uncertain":"settled",state_name(child.state),outcome.dump(),event.sequence,effect,p.id,n.definition.label}));
    changed_one(db.execute("UPDATE dynamic_plans SET state_sequence=state_sequence+1,state=? WHERE id=? AND state_sequence=?",{effect=="uncertain"?"uncertain":p.state,p.id,p.state_sequence}));
    auto current=dynamic_plan(p.id);auto result=*std::find_if(current.nodes.begin(),current.nodes.end(),[&](const auto& v){return v.child_run_id==child_id;});tx.commit();return result;
}catch(const DatabaseError&){throw DynamicOutcomeUnrecorded("Dynamic child outcome was not durably recorded");}
DynamicPlanStepResult Repository::settle_dynamic_plan_step(const std::string& call_id) try {
    auto& db=impl_->database;Transaction tx(db);auto c=dynamic_plan_call(call_id);auto p=dynamic_plan(c.plan_id);if(c.state=="interrupted"||c.state=="cancelled")throw Conflict("Retired plan call cannot acquire a report");if(!c.result_json.empty()){tx.commit();return {p,c,true};}
    if(c.state!="accepted"||run(c.root_run_id).state!=RunState::running||run(c.owner_run_id).state!=RunState::running)throw Conflict("Plan report requires its running owner");const auto decision=inspect_dynamic_plan(p);if(decision.halted)throw WorkspaceEffectUncertain("Dynamic plan has an uncertain outcome");if(!decision.report_ready){tx.commit();return {p,c,false};}
    const auto output=dynamic_plan_report(p);if(output.size()>65536)throw DatabaseError("Dynamic report exceeds its native bound");const auto event=impl_->event(c.owner_run_id,"plan.call.reported",output);
    changed_one(db.execute("UPDATE dynamic_plan_calls SET state='report_ready',result_json=?,result_event_seq=? WHERE id=? AND state='accepted' AND result_json IS NULL",{output,event.sequence,c.id}));c=dynamic_plan_call(c.id);tx.commit();return {p,c,true};
}catch(const DatabaseError&){throw DynamicOutcomeUnrecorded("Dynamic plan report was not durably recorded");}
void Repository::commit_dynamic_tool_turn(const std::string& owner_id,const std::string& call_id) try {
    auto& db=impl_->database;Transaction tx(db);auto c=dynamic_plan_call(call_id);if(c.owner_run_id!=owner_id)throw Conflict("Planning conversation owner differs");
    const auto signed_turn=Json::parse(c.parent_assistant_json);const auto name=signed_turn.at("tool_calls").at(0).at("name").get<std::string>();
    const auto activity=owner_id+":plan:"+c.provider_tool_call_id;
    const auto starts=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='tool.started' AND json_extract(payload,'$.activity_id')=?",{owner_id,activity}).rows;
    const auto origin=db.execute("SELECT seq FROM events WHERE run_id=? AND kind='budget.model_call.finished' AND json_extract(payload,'$.attempt_id')=? AND json_extract(payload,'$.was_started')=1",{owner_id,c.origin_attempt_id}).rows;
    if(starts.size()!=1||origin.size()!=1||integer(starts[0][0])<=integer(origin[0][0])||integer(starts[0][0])>=c.accepted_event_seq)throw Conflict("Planning commit lacks one prior actual tool start");
    const auto started=Json::parse(text(starts[0][1]));
    if(!started.contains("call_id")||started["call_id"]!=c.provider_tool_call_id||!started.contains("name")||started["name"]!=name)throw Conflict("Planning start differs from its signed call");
    if(started.contains("arguments_json")){if(!started["arguments_json"].is_string()||started["arguments_json"].get<std::string>()!=c.arguments_json)throw Conflict("Planning start arguments differ");}
    else if(!started.contains("arguments")||started["arguments"]!=Json::parse(c.arguments_json))throw Conflict("Planning start arguments differ");
    const auto completions=db.execute("SELECT seq,kind,payload FROM events WHERE run_id=? AND kind IN ('tool.completed','tool.failed') AND json_extract(payload,'$.activity_id')=?",{owner_id,activity}).rows;
    if(c.conversation_commit_seq){
        if(completions.size()!=1||text(completions[0][1])!="tool.completed"||integer(completions[0][0])>=*c.conversation_commit_seq)throw DatabaseError("Committed planning turn lacks its once-only actual completion");
        const auto completed=Json::parse(text(completions[0][2]));if(completed.value("call_id",std::string{})!=c.provider_tool_call_id||completed.value("name",std::string{})!=name||!completed.contains("data")||completed["data"]!=Json::parse(c.result_json))throw DatabaseError("Committed planning tool completion differs from its actual report");
        tx.commit();return;
    }
    const auto owner=run(owner_id);if(run(c.root_run_id).state!=RunState::running||owner.state!=RunState::running||c.state!="report_ready"||c.result_json.empty()||!completions.empty())throw Conflict("Planning turn lacks its actual uncommitted report");
    impl_->event(owner_id,"tool.completed",Json{{"activity_id",activity},{"call_id",c.provider_tool_call_id},{"name",name},{"data",Json::parse(c.result_json)}}.dump());
    impl_->message(owner,"assistant",c.parent_assistant_json);impl_->message(owner,"tool",Json{{"content",c.result_json},{"tool_call_id",c.provider_tool_call_id}}.dump());const auto event=impl_->event(owner_id,"conversation.tool_turn",Json{{"plan_call_id",call_id}}.dump());
    changed_one(db.execute("UPDATE dynamic_plan_calls SET state='turn_committed',conversation_commit_seq=? WHERE id=? AND state='report_ready' AND conversation_commit_seq IS NULL",{event.sequence,call_id}));tx.commit();
}catch(const DatabaseError&){throw DynamicOutcomeUnrecorded("Dynamic signed conversation turn was not durably committed");}
void Repository::record_rejected_dynamic_tool_turn(const std::string& root,const std::string& origin_id,const std::string& actual_assistant,const std::string& code) try {
    (void)DynamicPlanRejected(code);bounded_identity(origin_id);
    const auto exact=object_json(actual_assistant,8*1024*1024);const auto reply=Json::parse(exact);
    if(!reply.contains("tool_calls")||!reply["tool_calls"].is_array()||reply["tool_calls"].size()!=1)throw std::invalid_argument("Rejected planning turn must own one actual tool call");
    const auto& call=reply["tool_calls"][0];if(!call.is_object()||!call.contains("id")||!call["id"].is_string()||!call.contains("name")||!call["name"].is_string()||!call.contains("arguments")||!call["arguments"].is_string())throw std::invalid_argument("Rejected planning turn has invalid actual call fields");
    const auto provider_call=call["id"].get<std::string>(),name=call["name"].get<std::string>(),arguments=call["arguments"].get<std::string>();bounded_identity(provider_call,256);
    if(name!="plan_tasks"&&name!="revise_plan"&&name!="inspect_plan")throw std::invalid_argument("Only preaccept planning tools use typed rejection");
    auto& db=impl_->database;Transaction tx(db);const auto owner=run(root);const auto owner_id=owner.id;const auto root_id=owner.parent_id.empty()?owner.id:owner.parent_id;const auto root_run=run(root_id);const auto caps=dynamic_capabilities(root_id);const auto owner_role=owner_id==root_id?std::string("parent"):std::string("leaf");
    if(root_run.graph_root||!root_run.parent_id.empty()||!caps.catalogue_finalized||root_run.state!=RunState::running||owner.state!=RunState::running)throw Conflict("Rejected planning turn lacks its exact running dynamic owner");
    if(owner_id!=root_id){const auto admission=child_admission(owner_id);if(admission.kind!=ChildAdmissionKind::dynamic_agent||admission.root_run_id!=root_id)throw Conflict("Rejected nested planning turn is not owned by a dynamic agent");const auto parent_plan=dynamic_plan(admission.plan_id);const auto claimed=std::find_if(parent_plan.nodes.begin(),parent_plan.nodes.end(),[&](const auto& node){return node.child_run_id==owner_id;});if(parent_plan.state!="active"||claimed==parent_plan.nodes.end()||claimed->state!=DynamicNodeState::claimed||claimed->claim_id!=admission.claim_id||claimed->definition_revision!=admission.definition_revision||claimed->claim_revision!=admission.claim_revision)throw Conflict("Rejected nested planning turn lost its exact live claim");}
    const auto origin=db.execute("SELECT actual_assistant_json FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND role=? AND state='finished'",{root_id,origin_id,owner_id,owner_role}).rows;
    const auto finished=db.execute("SELECT seq FROM events WHERE run_id=? AND kind='budget.model_call.finished' AND json_extract(payload,'$.attempt_id')=? AND json_extract(payload,'$.was_started')=1",{owner_id,origin_id}).rows;
    if(origin.size()!=1||std::holds_alternative<std::nullptr_t>(origin[0][0])||text(origin[0][0])!=exact||finished.size()!=1)throw Conflict("Rejected planning turn differs from its actual finished response");
    if(!db.execute("SELECT id FROM dynamic_plan_calls WHERE owner_run_id=? AND (provider_tool_call_id=? OR state IN ('accepted','report_ready') OR (state='turn_committed' AND continuation_attempt_id IS NULL))",{owner_id,provider_call}).rows.empty())throw Conflict("Accepted planning work cannot acquire a generic rejected conversation");
    const auto activity=owner_id+":plan:"+provider_call;const auto starts=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='tool.started' AND json_extract(payload,'$.activity_id')=?",{owner_id,activity}).rows;
    if(starts.size()!=1||integer(starts[0][0])<=integer(finished[0][0]))throw Conflict("Rejected planning turn lacks one actual tool start");
    const auto started=Json::parse(text(starts[0][1]));if(!started.contains("call_id")||started["call_id"]!=provider_call||!started.contains("name")||started["name"]!=name)throw Conflict("Rejected planning start differs from actual call");
    if(started.contains("arguments_json")){if(!started["arguments_json"].is_string()||started["arguments_json"].get<std::string>()!=arguments)throw Conflict("Rejected planning start arguments differ");}
    else {if(!started.contains("arguments"))throw Conflict("Rejected planning start arguments are absent");try{if(started["arguments"]!=Json::parse(arguments))throw Conflict("Rejected planning start arguments differ");}catch(const Json::exception&){throw Conflict("Invalid planning JSON requires exact raw start arguments");}}
    const auto output=Json{{"source","native_dynamic_plan"},{"accepted",false},{"error",{{"code",code}}}};
    const auto markers=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='conversation.tool_turn' AND json_extract(payload,'$.rejected_plan_call_id')=?",{owner_id,provider_call}).rows;
    const auto completions=db.execute("SELECT seq,kind,payload FROM events WHERE run_id=? AND kind IN ('tool.completed','tool.failed') AND json_extract(payload,'$.activity_id')=?",{owner_id,activity}).rows;
    if(!markers.empty()){
        const auto marker=Json::parse(text(markers[0][1]));if(markers.size()!=1||marker.value("origin_attempt_id",std::string{})!=origin_id||marker.value("error_code",std::string{})!=code)throw Conflict("Rejected planning commit binding changed");
        if(completions.size()!=1||text(completions[0][1])!="tool.failed"||integer(completions[0][0])>=integer(markers[0][0]))throw DatabaseError("Rejected planning commit lacks once-only actual failure");const auto failure=Json::parse(text(completions[0][2]));if(failure.value("call_id",std::string{})!=provider_call||failure.value("name",std::string{})!=name||!failure.contains("data")||failure["data"]!=output)throw DatabaseError("Rejected planning tool failure differs from fixed safe result");settle_worker_planning(*this,db,root_id,owner_id,origin_id,exact,{},code);tx.commit();return;
    }
    if(owner.state!=RunState::running||!completions.empty())throw Conflict("Rejected planning owner or activity is already retired");
    impl_->event(owner_id,"tool.failed",Json{{"activity_id",activity},{"call_id",provider_call},{"name",name},{"data",output}}.dump());impl_->message(owner,"assistant",exact);impl_->message(owner,"tool",Json{{"content",output.dump()},{"tool_call_id",provider_call}}.dump());impl_->event(owner_id,"conversation.tool_turn",Json{{"rejected_plan_call_id",provider_call},{"origin_attempt_id",origin_id},{"error_code",code}}.dump());settle_worker_planning(*this,db,root_id,owner_id,origin_id,exact,{},code);tx.commit();
}catch(const DatabaseError&){throw DynamicOutcomeUnrecorded("Rejected actual planning turn was not durably committed");}
ModelCallReservation Repository::reserve_dynamic_continuation(const std::string& call_id,const std::string& attempt,const AgentWorkerLeaseSpec* worker){
    bounded_identity(attempt);auto& db=impl_->database;Transaction tx(db);const auto c=dynamic_plan_call(call_id);const auto budget=root_budget(c.root_run_id);const auto caps=dynamic_capabilities(c.root_run_id);const auto role=c.owner_run_id==c.root_run_id?ModelCallRole::parent:ModelCallRole::leaf;if(run(c.root_run_id).state!=RunState::running||run(c.owner_run_id).state!=RunState::running||c.state!="turn_committed"||!c.conversation_commit_seq)throw Conflict("Planning continuation lacks its committed actual turn");worker_planning_continuation_authority(*this,db,c,worker);
    if(!c.continuation_attempt_id.empty()){if(c.continuation_attempt_id!=attempt)throw Conflict("Planning continuation identity changed");if(worker&&worker_attempt_generation(db,c.root_run_id,c.owner_run_id,attempt)!=std::optional<std::string>{worker->generation})throw Conflict("Planning continuation funding belongs to another generation");const auto old=db.execute("SELECT owner_run_id,role,state,COALESCE(inference_step_id,'') FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{c.root_run_id,attempt}).rows;if(old.empty()||text(old[0][0])!=c.owner_run_id||text(old[0][1])!=call_role(role)||text(old[0][2])!="reserved")throw Conflict("Planning continuation already dispatched or retired");tx.commit();return {c.root_run_id,attempt,c.owner_run_id,role,"reserved",ModelCallPurpose::inference,text(old[0][3])};}
    if(budget.parent_calls_held<1||budget.model_calls_reserved>=budget.spec.max_model_calls)throw RootBudgetExhausted("Planning continuation allowance exhausted");
    if(role==ModelCallRole::parent&&budget.parent_model_calls_reserved>=caps.max_parent_turns)throw RootBudgetExhausted("Root planning continuation allowance exhausted");
    if(role==ModelCallRole::leaf){const auto admission=child_admission(c.owner_run_id);if(admission.kind!=ChildAdmissionKind::dynamic_agent||admission.root_run_id!=c.root_run_id)throw Conflict("Nested continuation owner is not a dynamic agent");const auto p=dynamic_plan(admission.plan_id);const auto n=std::find_if(p.nodes.begin(),p.nodes.end(),[&](const auto& node){return node.child_run_id==c.owner_run_id;});if(n==p.nodes.end())throw Conflict("Nested continuation lost its exact plan node");const auto& preset=dynamic_preset(p.capabilities,n->definition);const auto inference_turns=integer(db.execute("SELECT count(DISTINCT COALESCE(inference_step_id,attempt_id)) FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=? AND role='leaf' AND purpose='inference'",{c.root_run_id,c.owner_run_id}).rows.at(0).at(0));if(inference_turns>=preset.turn_limit)throw RootBudgetExhausted("Nested planning continuation allowance exhausted");}
    changed_one(db.execute("UPDATE agent_execution_budgets SET model_calls_reserved=model_calls_reserved+1,parent_calls_held=parent_calls_held-1,revision=revision+1 WHERE root_run_id=? AND revision=?",{c.root_run_id,budget.revision}));
    changed_one(db.execute("INSERT INTO agent_model_call_reservations(root_run_id,attempt_id,owner_run_id,role,state) VALUES(?,?,?,?, 'reserved')",{c.root_run_id,attempt,c.owner_run_id,call_role(role)}));
    changed_one(db.execute("UPDATE dynamic_plan_calls SET continuation_attempt_id=? WHERE id=? AND continuation_attempt_id IS NULL",{attempt,call_id}));Json funding={{"root_run_id",c.root_run_id},{"attempt_id",attempt},{"role",call_role(role)},{"plan_call_id",call_id}};if(worker)funding["worker_generation"]=worker->generation;impl_->event(c.owner_run_id,"budget.model_call.reserved",funding.dump());tx.commit();return {c.root_run_id,attempt,c.owner_run_id,role,"reserved"};
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
DynamicPauseBinding Repository::dynamic_owner_pause(const std::string& root_id){
    auto& db=impl_->database;const auto root=run(root_id);if(root.state!=RunState::paused||root.graph_root||!root.parent_id.empty())throw Conflict("Dynamic owner has no paused ordinary root");
    const auto rows=db.execute("SELECT plan_id,plan_call_id FROM dynamic_owner_pause_waits WHERE root_run_id=? AND state='paused' ORDER BY plan_id,plan_call_id",{root_id}).rows;if(rows.empty())throw NotFound("Paused dynamic owner binding not found");
    DynamicPauseBinding result;result.plan_id=text(rows[0][0]);result.plan_call_id=text(rows[0][1]);
    for(const auto& row:rows){DynamicPlanWaitBinding wait;wait.plan_id=text(row[0]);wait.plan_call_id=text(row[1]);
    auto waiting=dynamic_plan(wait.plan_id);const auto waiting_call=dynamic_plan_call(wait.plan_call_id);
    if(waiting.root_run_id!=root_id||waiting.state!="waiting_human"||waiting_call.root_run_id!=root_id||waiting_call.plan_id!=waiting.id||waiting_call.owner_run_id!=waiting.owner_run_id||waiting_call.state!="accepted")throw DatabaseError("Paused dynamic wait lost its exact accepted owner call");
    auto current=waiting;while(current.owner_run_id!=root_id){
        if(current.parent_plan_id.empty()||current.parent_node_label.empty()||current.depth<1||current.depth>8)throw DatabaseError("Paused dynamic owner chain ended before its ordinary root");
        const auto owner=run(current.owner_run_id);if(owner.parent_id!=root_id||owner.session_id!=root.session_id||owner.state!=RunState::paused)throw DatabaseError("Paused dynamic descendant run does not match its root chain");
        const auto parent=dynamic_plan(current.parent_plan_id);if(parent.root_run_id!=root_id||parent.depth+1!=current.depth||parent.state!="active")throw DatabaseError("Paused dynamic parent plan changed");
        const auto node=std::find_if(parent.nodes.begin(),parent.nodes.end(),[&](const auto& value){return value.definition.label==current.parent_node_label&&value.child_run_id==current.owner_run_id;});
        if(node==parent.nodes.end()||node->definition.kind!=DynamicNodeKind::agent||node->state!=DynamicNodeState::claimed||node->claim_id.empty()||node->claim_revision!=node->definition_revision)throw DatabaseError("Paused dynamic owner lost its exact claimed parent node");
        const auto owner_calls=dynamic_plan_calls(root_id);const auto pending=std::count_if(owner_calls.begin(),owner_calls.end(),[&](const auto& value){return value.plan_id==current.id&&value.owner_run_id==current.owner_run_id&&value.state=="accepted";});
        if(pending!=1)throw DatabaseError("Paused dynamic descendant lacks one exact accepted continuation");
        wait.owner_runs.push_back(current.owner_run_id);current=parent;
    }
    if(current.owner_run_id!=root_id||current.depth!=0||!current.parent_plan_id.empty()||current.root_run_id!=root_id||(current.state!="active"&&current.state!="waiting_human"))throw DatabaseError("Paused dynamic chain does not terminate at its exact root plan");
    const auto root_calls=dynamic_plan_calls(root_id);const auto root_pending=std::count_if(root_calls.begin(),root_calls.end(),[&](const auto& value){return value.plan_id==current.id&&value.owner_run_id==root_id&&value.state=="accepted";});if(root_pending!=1)throw DatabaseError("Paused dynamic root lacks one exact accepted continuation");
    std::reverse(wait.owner_runs.begin(),wait.owner_runs.end());result.waits.push_back(std::move(wait));}
    result.owner_runs.clear();std::set<std::string> chain;for(const auto& wait:result.waits)for(const auto& owner:wait.owner_runs)if(chain.insert(owner).second)result.owner_runs.push_back(owner);std::vector<std::string> parents{root_id};for(std::size_t i=0;i<parents.size();++i)for(const auto& row:db.execute("SELECT id,state FROM runs WHERE parent_run_id=?",{parents[i]}).rows){const auto id=text(row[0]),state=text(row[1]);if((state=="queued"||state=="running"||state=="paused")&&(!chain.contains(id)||state!="paused"))throw DatabaseError("Paused dynamic root retains an independent or unpaused descendant");parents.push_back(id);}return result;
}
DynamicBudgetSegment Repository::open_dynamic_budget_segment(const DynamicSegmentSpec& spec){
    bounded_identity(spec.id,128);auto& db=impl_->database;Transaction tx(db);const auto caps=dynamic_capabilities(spec.root_run_id);const auto budget=root_budget(spec.root_run_id);if(caps.backend_identity!=spec.backend_identity)throw DynamicPlanUnavailable("Dynamic active segment settings changed");if(run(spec.root_run_id).state!=RunState::running||budget.revision!=spec.expected_budget_revision)throw Conflict("Dynamic segment owner or budget changed");
    const auto old=db.execute("SELECT id,ordinal,state,remaining_active_ms FROM agent_budget_segments WHERE root_run_id=? ORDER BY ordinal DESC LIMIT 1",{spec.root_run_id}).rows;std::int64_t ordinal=1,remaining=budget.spec.wall_limit_ms;
    if(!old.empty()){if(text(old[0][0])==spec.id&&text(old[0][2])=="open"){auto s=dynamic_budget_segment(spec.root_run_id);tx.commit();return s;}throw Conflict("Subsequent dynamic active segments require typed resume");}
    if(remaining<1)throw RootBudgetExhausted("Dynamic active time exhausted");const auto e=impl_->event(spec.root_run_id,"budget.segment.opened",Json{{"segment_id",spec.id},{"ordinal",ordinal},{"remaining_active_ms",remaining}}.dump());changed_one(db.execute("INSERT INTO agent_budget_segments(root_run_id,id,ordinal,state,remaining_active_ms,opened_event_seq) VALUES(?,?,?,'open',?,?)",{spec.root_run_id,spec.id,ordinal,remaining,e.sequence}));
    changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=? AND revision=?",{spec.root_run_id,budget.revision}));auto s=dynamic_budget_segment(spec.root_run_id);tx.commit();return s;
}
Run Repository::suspend_dynamic_owner(const DynamicPauseSpec& spec){
    auto& db=impl_->database;Transaction tx(db);auto root=run(dynamic_plan(spec.plan_id).root_run_id);const auto budget=root_budget(root.id);
    std::vector<DynamicPauseWaitSpec> requested=spec.waits;if(requested.empty())requested.push_back({spec.plan_id,spec.plan_call_id,spec.expected_revision,spec.expected_state_sequence});
    if(requested.empty()||requested.size()>64||root.state!=RunState::running)throw Conflict("Dynamic pause requires a running root and a bounded wait set");
    struct ValidatedWait {DynamicPlanRecord plan;DynamicPlanCallRecord call;std::vector<std::string> owners;};std::vector<ValidatedWait> waits;waits.reserve(requested.size());std::set<std::pair<std::string,std::string>> unique_waits;std::set<std::string> chain_owners;Json wait_event=Json::array();
    for(const auto& request:requested){if(!unique_waits.emplace(request.plan_id,request.plan_call_id).second)throw Conflict("Dynamic pause wait set contains a duplicate plan call");auto p=dynamic_plan(request.plan_id);dynamic_cas(p,request.expected_revision,request.expected_state_sequence,spec.backend_identity);const auto call=dynamic_plan_call(request.plan_call_id);
        if(p.root_run_id!=root.id||p.state!="active"||call.root_run_id!=root.id||call.plan_id!=p.id||call.owner_run_id!=p.owner_run_id||call.state!="accepted")throw Conflict("Dynamic pause wait binding changed");const auto d=inspect_dynamic_plan(p);
        if(d.halted||!d.ready.empty()||!d.claimed.empty()||d.waiting_human.empty())throw Conflict("Every dynamic pause wait must be an actual human-only frontier");
        std::vector<std::string> owners;auto current=p;while(current.owner_run_id!=root.id){
            if(current.parent_plan_id.empty()||current.parent_node_label.empty()||current.depth<1||current.depth>8)throw Conflict("Dynamic pause has no complete admitted owner chain");
            const auto owner=run(current.owner_run_id);if(owner.parent_id!=root.id||owner.session_id!=root.session_id||owner.state!=RunState::running)throw Conflict("Dynamic pause owner chain contains a non-running descendant");
            const auto parent=dynamic_plan(current.parent_plan_id);if(parent.root_run_id!=root.id||parent.depth+1!=current.depth||parent.state!="active")throw Conflict("Dynamic pause parent plan changed");
            const auto node=std::find_if(parent.nodes.begin(),parent.nodes.end(),[&](const auto& value){return value.definition.label==current.parent_node_label&&value.child_run_id==current.owner_run_id;});
            if(node==parent.nodes.end()||node->definition.kind!=DynamicNodeKind::agent||node->state!=DynamicNodeState::claimed||node->claim_id.empty()||node->claim_revision!=node->definition_revision)throw Conflict("Dynamic pause owner lost its exact claimed parent node");
            const auto pending=dynamic_plan_calls(root.id);if(std::count_if(pending.begin(),pending.end(),[&](const auto& value){return value.plan_id==current.id&&value.owner_run_id==current.owner_run_id&&value.state=="accepted";})!=1)throw Conflict("Dynamic pause ancestor lacks its accepted continuation");
            owners.push_back(current.owner_run_id);current=parent;
        }
        if(current.owner_run_id!=root.id||current.depth!=0||current.root_run_id!=root.id||(current.state!="active"&&current.state!="waiting_human"))throw Conflict("Dynamic pause chain does not terminate at its exact root plan");
        const auto pending=dynamic_plan_calls(root.id);if(std::count_if(pending.begin(),pending.end(),[&](const auto& value){return value.plan_id==current.id&&value.owner_run_id==root.id&&value.state=="accepted";})!=1)throw Conflict("Dynamic pause root lacks its accepted continuation");std::reverse(owners.begin(),owners.end());
        for(const auto& owner:owners)chain_owners.insert(owner);wait_event.push_back({{"plan_id",p.id},{"plan_call_id",call.id},{"owner_chain",owners}});waits.push_back({std::move(p),call,std::move(owners)});
    }
    if(budget.revision!=spec.expected_budget_revision)throw DynamicPlanChanged("Dynamic pause budget changed");const bool modern_context=!db.execute("SELECT id FROM inference_steps WHERE root_run_id=? AND owner_run_id=?",{root.id,root.id}).rows.empty();if(modern_context&&!spec.context_pin)throw ContextBindingChanged("Context-enabled planning root requires a closed wait pin");if(spec.context_pin)validate_context_pause(db,root,waits.front().plan.capabilities,*spec.context_pin);
    if(!db.execute("SELECT id FROM context_compactions WHERE root_run_id=? AND state IN ('reserved','started','received')",{root.id}).rows.empty()||!db.execute("SELECT id FROM context_measures WHERE root_run_id=? AND state IN ('reserved','started')",{root.id}).rows.empty())throw Conflict("Human pause still owns context transport");
    std::vector<std::string> parents{root.id};for(std::size_t i=0;i<parents.size();++i)for(const auto& row:db.execute("SELECT id,state FROM runs WHERE parent_run_id=?",{parents[i]}).rows){const auto id=text(row[0]),state=text(row[1]);if((state=="queued"||state=="running"||state=="paused")&&(!chain_owners.contains(id)||state!="running"))throw Conflict("Dynamic pause still owns independent or non-running descendant work");parents.push_back(id);}
    if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{root.id}).rows.empty()||!db.execute("WITH RECURSIVE descendants(id) AS (SELECT id FROM runs WHERE parent_run_id=? UNION ALL SELECT r.id FROM runs r JOIN descendants d ON r.parent_run_id=d.id) SELECT o.id FROM operations o JOIN runs r ON r.id=o.run_id WHERE (r.id=? OR r.id IN (SELECT id FROM descendants)) AND o.state IN ('awaiting_approval','ready','executing','uncertain')",{root.id,root.id}).rows.empty())throw Conflict("Dynamic pause still owns physical work");
    const auto segment=dynamic_budget_segment(root.id);if(segment.id!=spec.segment_id||segment.state!="open"||spec.measured_active_elapsed_ms>=segment.remaining_active_ms)throw RootBudgetExhausted("Dynamic pause has no active time remaining");impl_->close_dynamic_segment(root.id,spec.segment_id,spec.measured_active_elapsed_ms);
    changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=? AND revision=?",{root.id,budget.revision}));
    for(const auto& wait:waits){dynamic_safe_sequence(wait.plan.state_sequence);changed_one(db.execute("UPDATE dynamic_plans SET state='waiting_human',state_sequence=state_sequence+1 WHERE id=? AND revision=? AND state_sequence=? AND state='active'",{wait.plan.id,wait.plan.revision,wait.plan.state_sequence}));}
    for(const auto& owner:chain_owners){changed_one(db.execute("UPDATE runs SET state='paused' WHERE id=? AND state='running'",{owner}));impl_->event(owner,"run.paused",Json{{"reason","nested_dynamic_human_wait"}}.dump());}
    changed_one(db.execute("UPDATE runs SET state='paused' WHERE id=? AND state='running'",{root.id}));const auto first=waits.front();const auto e=impl_->event(root.id,"run.paused",Json{{"reason","dynamic_human_wait"},{"plan_id",first.plan.id},{"plan_call_id",first.call.id},{"segment_id",spec.segment_id},{"waits",wait_event}}.dump());
    changed_one(db.execute("INSERT INTO dynamic_owner_pauses(root_run_id,plan_id,plan_call_id,segment_id,paused_event_seq,state,context_pin) VALUES(?,?,?,?,?,'paused',?) ON CONFLICT(root_run_id) DO UPDATE SET plan_id=excluded.plan_id,plan_call_id=excluded.plan_call_id,segment_id=excluded.segment_id,paused_event_seq=excluded.paused_event_seq,state='paused',context_pin=excluded.context_pin",{root.id,first.plan.id,first.call.id,spec.segment_id,e.sequence,spec.context_pin?SqlValue(context_pause_json(*spec.context_pin).dump()):SqlValue(nullptr)}));
    for(const auto& wait:waits)changed_one(db.execute("INSERT INTO dynamic_owner_pause_waits(root_run_id,plan_id,plan_call_id,state) VALUES(?,?,?,'paused') ON CONFLICT(root_run_id,plan_id,plan_call_id) DO UPDATE SET state='paused'",{root.id,wait.plan.id,wait.call.id}));tx.commit();root.state=RunState::paused;return root;
}
std::optional<ContextPausePin> Repository::dynamic_context_pause(const std::string& root){auto& db=impl_->database;Transaction tx(db);const auto owner=run(root);if(!owner.parent_id.empty()||owner.graph_root)throw std::invalid_argument("Closed context pin belongs to its ordinary root");const auto rows=db.execute("SELECT context_pin FROM dynamic_owner_pauses WHERE root_run_id=?",{root}).rows;if(rows.empty()||std::holds_alternative<std::nullptr_t>(rows[0][0])){if(!db.execute("SELECT id FROM inference_steps WHERE root_run_id=? AND owner_run_id=?",{root,root}).rows.empty()&&owner.state==RunState::paused)throw ContextBindingChanged("Modern closed owner is missing its context pin");tx.commit();return {};}auto pin=decode_context_pause(text(rows[0][0]));validate_context_pause(db,owner,dynamic_capabilities(root),pin);tx.commit();return pin;}
DynamicResumeRecord Repository::resume_dynamic_owner(const DynamicResumeSpec& spec){
    bounded_identity(spec.segment_id,128);const auto requested=dynamic_plan(spec.plan_id);const auto chain=dynamic_owner_pause(requested.root_run_id);
    auto& db=impl_->database;Transaction tx(db);auto root_plan=dynamic_plan(spec.plan_id);dynamic_cas(root_plan,spec.expected_revision,spec.expected_state_sequence,spec.backend_identity);auto root=run(root_plan.root_run_id);const auto budget=root_budget(root.id);
    if(root.state!=RunState::paused||root_plan.state!="active"&&root_plan.state!="waiting_human")throw Conflict("Dynamic paused root owner changed");if(budget.revision!=spec.expected_budget_revision)throw DynamicPlanChanged("Dynamic paused budget changed");
    const auto pause=db.execute("SELECT plan_id,plan_call_id,segment_id FROM dynamic_owner_pauses WHERE root_run_id=? AND state='paused'",{root.id}).rows;if(pause.size()!=1||chain.waits.empty()||text(pause[0][0])!=chain.waits.front().plan_id||text(pause[0][1])!=chain.waits.front().plan_call_id)throw Conflict("Dynamic paused binding changed");
    std::vector<DynamicPlanRecord> waiting_plans;std::set<std::string> waiting_owner_runs;bool any_runnable=false;for(const auto& binding:chain.waits){auto waiting_plan=dynamic_plan(binding.plan_id);const auto waiting_call=dynamic_plan_call(binding.plan_call_id);if(waiting_plan.state!="waiting_human"||waiting_call.state!="accepted"||waiting_call.plan_id!=waiting_plan.id||waiting_call.owner_run_id!=waiting_plan.owner_run_id)throw Conflict("A dynamic human continuation in the wait set is not resumable");const auto decision=inspect_dynamic_plan(waiting_plan);if(decision.halted)throw WorkspaceEffectUncertain("Dynamic wait set contains an uncertain plan outcome");any_runnable=any_runnable||decision.report_ready||!decision.ready.empty();for(const auto& owner:binding.owner_runs)waiting_owner_runs.insert(owner);waiting_plans.push_back(std::move(waiting_plan));}if(!any_runnable)throw Conflict("No answered dynamic wait has ready work");
    const auto root_calls=dynamic_plan_calls(root.id);const auto root_pending=std::find_if(root_calls.begin(),root_calls.end(),[&](const auto& value){return value.owner_run_id==root.id&&value.plan_id==root_plan.id&&value.state=="accepted";});if(root_pending==root_calls.end())throw Conflict("Dynamic root signed continuation is not resumable");auto call=*root_pending;
    const auto saved_pin_rows=db.execute("SELECT context_pin FROM dynamic_owner_pauses WHERE root_run_id=? AND state='paused'",{root.id}).rows;std::optional<ContextPausePin> saved_pin;if(!std::holds_alternative<std::nullptr_t>(saved_pin_rows[0][0]))saved_pin=decode_context_pause(text(saved_pin_rows[0][0]));if(saved_pin.has_value()!=spec.context_pin.has_value()||(saved_pin&&context_pause_json(*saved_pin)!=context_pause_json(*spec.context_pin)))throw ContextBindingChanged("Closed context resume pin changed");if(saved_pin)validate_context_pause(db,root,root_plan.capabilities,*saved_pin);else if(!db.execute("SELECT id FROM inference_steps WHERE root_run_id=? AND owner_run_id=?",{root.id,root.id}).rows.empty())throw ContextBindingChanged("Modern closed owner cannot resume without its pin");
    const auto old=dynamic_budget_segment(root.id);if(old.state!="closed"||old.id!=text(pause[0][2])||old.remaining_active_ms<1)throw RootBudgetExhausted("Dynamic paused time is unavailable");
    if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{root.id}).rows.empty()||!db.execute("SELECT o.id FROM operations o JOIN runs r ON r.id=o.run_id WHERE (r.id=? OR r.parent_run_id=?) AND o.state IN ('awaiting_approval','ready','executing','uncertain')",{root.id,root.id}).rows.empty())throw Conflict("Dynamic resume has unfinished physical work");
    dynamic_safe_sequence(root_plan.state_sequence);for(const auto& waiting_plan:waiting_plans)dynamic_safe_sequence(waiting_plan.state_sequence);
    Json resumed_waits=Json::array();for(const auto& binding:chain.waits)resumed_waits.push_back({{"plan_id",binding.plan_id},{"plan_call_id",binding.plan_call_id},{"owner_chain",binding.owner_runs}});
    changed_one(db.execute("UPDATE runs SET state='running' WHERE id=? AND state='paused'",{root.id}));impl_->event(root.id,"run.running",Json{{"reason","dynamic_human_resume"},{"waits",resumed_waits}}.dump());
    for(const auto& owner:waiting_owner_runs){changed_one(db.execute("UPDATE runs SET state='running' WHERE id=? AND state='paused'",{owner}));impl_->event(owner,"run.running",Json{{"reason","nested_dynamic_human_resume"}}.dump());}
    std::set<std::string> activated_plans;for(const auto& waiting_plan:waiting_plans)if(activated_plans.insert(waiting_plan.id).second)changed_one(db.execute("UPDATE dynamic_plans SET state='active',state_sequence=state_sequence+1 WHERE id=? AND state_sequence=? AND state='waiting_human'",{waiting_plan.id,waiting_plan.state_sequence}));
    const auto e=impl_->event(root.id,"budget.segment.opened",Json{{"segment_id",spec.segment_id},{"ordinal",old.ordinal+1},{"remaining_active_ms",old.remaining_active_ms}}.dump());changed_one(db.execute("INSERT INTO agent_budget_segments(root_run_id,id,ordinal,state,remaining_active_ms,opened_event_seq) VALUES(?,?,?,'open',?,?)",{root.id,spec.segment_id,old.ordinal+1,old.remaining_active_ms,e.sequence}));changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=? AND revision=?",{root.id,budget.revision}));changed_one(db.execute("UPDATE dynamic_owner_pauses SET state='resumed' WHERE root_run_id=? AND state='paused'",{root.id}));db.execute("UPDATE dynamic_owner_pause_waits SET state='resumed' WHERE root_run_id=? AND state='paused'",{root.id});
    root.state=RunState::running;DynamicResumeRecord result{root,root_plan,dynamic_budget_segment(root.id),call,saved_pin,std::vector<std::string>(waiting_owner_runs.begin(),waiting_owner_runs.end())};tx.commit();return result;
}
Run Repository::complete_dynamic_owner(const std::string& root_id,const std::string& assistant,const std::string& segment_id,std::int64_t elapsed,const AgentWorkerLeaseSpec* worker,const std::string& worker_attempt){
    auto& db=impl_->database;Transaction tx(db);std::string actual=assistant;if(worker){if(root_id!=worker->root_run_id||root_id!=worker->owner_run_id||!assistant.empty())throw Conflict("Worker cannot select a planning completion or assistant");actual=worker_assistant_receipt(*this,db,*worker,worker_attempt,true,true);const auto plan=dynamic_plan_for_owner(root_id);if(plan&&!inspect_dynamic_plan(*plan).finished)throw Conflict("Worker planning completion has unfinished native tasks");}(void)object_json(actual,8*1024*1024);auto root=run(root_id);(void)dynamic_capabilities(root.id);if(root.state!=RunState::running)throw Conflict("Dynamic completion owner is not running");
    if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{root.id}).rows.empty())throw Conflict("Dynamic completion still owns transport");impl_->close_dynamic_segment(root.id,segment_id,elapsed);impl_->root_boundary(root,RunState::completed);
    if(impl_->has_unresolved_effects(root.id))throw Conflict("Dynamic root has unresolved effects");impl_->message(root,"assistant",actual);changed_one(db.execute("UPDATE runs SET state='completed' WHERE id=? AND state='running'",{root.id}));impl_->event(root.id,"conversation.assistant",actual);impl_->event(root.id,"run.completed","{}");
    db.execute("UPDATE dynamic_plans SET state='completed',state_sequence=state_sequence+1 WHERE root_run_id=? AND state='active'",{root.id});db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=?",{root.id});if(worker)impl_->retire_worker(worker->generation,"completed");tx.commit();root.state=RunState::completed;return root;
}
Run Repository::retire_dynamic_owner(const std::string& root_id,RunState next,const std::string& reason,const std::string& segment_id,std::int64_t elapsed){
    if(next!=RunState::failed&&next!=RunState::cancelled)throw std::invalid_argument("Dynamic retirement requires failure or cancellation");(void)object_json(reason);
    // Settlements remain separate typed, once-only transactions. A journal
    // failure aborts retirement; an actual result is never fabricated.
    (void)dynamic_capabilities(root_id);for(const auto& c:owned_children(root_id)){if(!terminal(c.run.state))throw Conflict("Dynamic owner still has a physical child");if(c.kind=="dynamic_agent"||c.kind=="dynamic_tool")settle_dynamic_child(c.run.id);else if(c.kind=="delegated_leaf")settle_delegation_child(c.run.id);}
    for(const auto& b:delegation_batches(root_id))if(b.state=="accepted"||b.state=="working")settle_delegation_batch(b.id);
    auto& db=impl_->database;Transaction tx(db);auto root=run(root_id);if(terminal(root.state)){if(root.state!=next)throw Conflict("Dynamic terminal state differs");tx.commit();return root;}
    if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{root.id}).rows.empty()||!db.execute("SELECT o.id FROM operations o JOIN runs r ON r.id=o.run_id WHERE (r.id=? OR r.parent_run_id=?) AND o.state='executing'",{root.id,root.id}).rows.empty())throw Conflict("Dynamic owner still has a physical transport or effect");
    const auto segments=db.execute("SELECT id FROM agent_budget_segments WHERE root_run_id=? AND state='open'",{root.id}).rows;if(!segments.empty()){if(segment_id.empty())throw Conflict("Dynamic retirement requires measured active closure");impl_->close_dynamic_segment(root.id,segment_id,elapsed);}else if(!segment_id.empty())throw Conflict("Dynamic retirement segment is no longer open");
    auto p=dynamic_plan_for_root(root.id);if(p){for(const auto& h:dynamic_human_requests(p->id))if(h.state=="waiting"){const auto e=impl_->event(root.id,"plan.human.cancelled",Json{{"request_id",h.id},{"plan_id",p->id},{"label",h.label}}.dump());changed_one(db.execute("UPDATE dynamic_human_requests SET state='cancelled',input_event_seq=? WHERE id=? AND state='waiting'",{e.sequence,h.id}));const auto outcome=Json{{"human_state","cancelled"},{"request_id",h.id}}.dump();const auto settled=impl_->event(root.id,"plan.human.settled",outcome);changed_one(db.execute("UPDATE dynamic_plan_nodes SET state='settled',outcome_json=?,settled_event_seq=? WHERE plan_id=? AND label=? AND state='waiting_human'",{outcome,settled.sequence,p->id,h.label}));}
        db.execute("UPDATE dynamic_plan_nodes SET state='cancelled',protected_definition=1 WHERE plan_id=? AND state IN ('pending','blocked')",{p->id});changed_one(db.execute("UPDATE dynamic_plans SET state=?,state_sequence=state_sequence+1,planned_children_reserved=0,planned_humans_reserved=0 WHERE id=?",{state_name(next),p->id}));
    }
    const auto holds=integer(db.execute("SELECT count(*) FROM dynamic_plan_calls WHERE root_run_id=? AND continuation_attempt_id IS NULL AND state IN ('accepted','report_ready','turn_committed')",{root.id}).rows.at(0).at(0));db.execute("UPDATE dynamic_plan_calls SET state='cancelled' WHERE root_run_id=? AND state IN ('accepted','report_ready','turn_committed') AND continuation_attempt_id IS NULL",{root.id});
    changed_one(db.execute("UPDATE agent_execution_budgets SET planned_children_reserved=0,parent_calls_held=parent_calls_held-?,revision=revision+1 WHERE root_run_id=? AND parent_calls_held>=?",{holds,root.id,holds}));db.execute("UPDATE dynamic_owner_pauses SET state='retired' WHERE root_run_id=?",{root.id});db.execute("UPDATE dynamic_owner_pause_waits SET state='retired' WHERE root_run_id=?",{root.id});impl_->cancel_waiting(root.id);
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
    const auto result=impl_->database.execute("SELECT s.id,s.title,COALESCE(f.parent_session_id,''),f.boundary_sequence,COALESCE(f.boundary_type,'') FROM sessions s LEFT JOIN session_forks f ON f.session_id=s.id WHERE s.id=? AND NOT EXISTS(SELECT 1 FROM deleted_sessions d WHERE d.id=s.id)",{id});
    if(result.rows.empty()) throw NotFound("Session not found");
    const auto& row=result.rows[0];Session value{text(row[0]),text(row[1]),text(row[2]),{},text(row[4])};if(!std::holds_alternative<std::nullptr_t>(row[3]))value.fork_boundary_sequence=integer(row[3]);return value;
}
std::vector<Session> Repository::sessions() {
    std::vector<Session> result;
    for(const auto& row:impl_->database.execute("SELECT s.id,s.title,COALESCE(f.parent_session_id,''),f.boundary_sequence,COALESCE(f.boundary_type,'') FROM sessions s LEFT JOIN session_forks f ON f.session_id=s.id WHERE NOT EXISTS(SELECT 1 FROM deleted_sessions d WHERE d.id=s.id) ORDER BY s.rowid").rows){Session value{text(row[0]),text(row[1]),text(row[2]),{},text(row[4])};if(!std::holds_alternative<std::nullptr_t>(row[3]))value.fork_boundary_sequence=integer(row[3]);result.push_back(std::move(value));}
    return result;
}
void Repository::delete_session(const std::string& id) {
    identifier(id);auto& db=impl_->database;Transaction transaction(db);(void)session(id);
    std::vector<std::string> removed{id};
    for(const auto& row:db.execute("SELECT f.session_id FROM session_forks f JOIN sessions child ON child.id=f.session_id WHERE f.parent_session_id=? AND NOT EXISTS(SELECT 1 FROM deleted_sessions d WHERE d.id=child.id) ORDER BY child.rowid",{id}).rows)
        removed.push_back(text(row[0]));
    for(const auto& target:removed){
        if(!db.execute("SELECT i.id FROM information i JOIN runs r ON r.id=json_extract(i.payload,'$.spec.owner_run_id') WHERE i.category='native-agent-worker-leases' AND json_extract(i.payload,'$.state')='active' AND r.session_id=? LIMIT 1",{target}).rows.empty()||!db.execute("SELECT i.id FROM information i JOIN runs r ON r.id=json_extract(i.payload,'$.spec.owner_run_id') WHERE i.category='native-agent-worker-model-claims' AND json_extract(i.payload,'$.state')='dispatching' AND r.session_id=? LIMIT 1",{target}).rows.empty())throw Conflict("Session has an active native worker or model dispatch");
        if(!db.execute("SELECT i.id FROM information i JOIN runs r ON r.id=json_extract(i.payload,'$.model.owner_run_id') WHERE i.category='native-agent-worker-read-claims' AND json_extract(i.payload,'$.state') IN ('awaiting_dispatch','executing') AND r.session_id=? LIMIT 1",{target}).rows.empty())throw Conflict("Session has an active native worker read");
        if(!db.execute("SELECT i.id FROM information i JOIN runs r ON r.id=json_extract(i.payload,'$.model.owner_run_id') WHERE i.category='native-agent-worker-file-claims' AND json_extract(i.payload,'$.state') IN ('awaiting_dispatch','executing') AND r.session_id=? LIMIT 1",{target}).rows.empty())throw Conflict("Session has an active native worker file call");
        if(!db.execute("SELECT i.id FROM information i JOIN runs r ON r.id=json_extract(i.payload,'$.model.owner_run_id') WHERE i.category='native-agent-worker-process-claims' AND json_extract(i.payload,'$.state') IN ('awaiting_dispatch','executing') AND r.session_id=? LIMIT 1",{target}).rows.empty())throw Conflict("Session has an active native worker process call");
        if(!db.execute("SELECT i.id FROM information i JOIN runs r ON r.id=json_extract(i.payload,'$.model.owner_run_id') WHERE i.category='native-agent-worker-remote-claims' AND json_extract(i.payload,'$.state') IN ('awaiting_dispatch','executing') AND r.session_id=? LIMIT 1",{target}).rows.empty())throw Conflict("Session has an active native worker remote call");
        if(!db.execute("SELECT i.id FROM information i JOIN runs r ON r.id=json_extract(i.payload,'$.model.owner_run_id') WHERE i.category='native-agent-worker-callable-claims' AND json_extract(i.payload,'$.state') IN ('awaiting_dispatch','executing') AND r.session_id=? LIMIT 1",{target}).rows.empty())throw Conflict("Session has an active native worker callable call");
        if(!db.execute("SELECT id FROM runs WHERE session_id=? AND state IN ('queued','running','paused') LIMIT 1",{target}).rows.empty()||
           !db.execute("SELECT o.id FROM operations o JOIN runs r ON r.id=o.run_id WHERE r.session_id=? AND o.state IN ('awaiting_approval','ready','executing','uncertain') LIMIT 1",{target}).rows.empty()||
           !db.execute("SELECT id FROM context_idle_owners WHERE session_id=? AND state='active' LIMIT 1",{target}).rows.empty()||
           !db.execute("SELECT id FROM context_compactions WHERE state IN ('reserved','started','received') AND ((scope_kind='session' AND scope_id=?) OR (scope_kind='execution' AND scope_id IN (SELECT id FROM runs WHERE session_id=?))) LIMIT 1",{target,target}).rows.empty()||
           !db.execute("SELECT id FROM context_manual_requests WHERE state IN ('pending','claimed') AND ((scope_kind='session' AND scope_id=?) OR (scope_kind='execution' AND scope_id IN (SELECT id FROM runs WHERE session_id=?))) LIMIT 1",{target,target}).rows.empty())
            throw Conflict("Session or child session has active work or unresolved effects and cannot be deleted");
    }
    for(const auto& target:removed){
        changed_one(db.execute("INSERT INTO deleted_sessions(id,deleted_ms) VALUES(?,?)",{target,now_ms()}));
        db.execute("DELETE FROM session_forks WHERE session_id=? OR parent_session_id=?",{target,target});
    }
    transaction.commit();
}
Run Repository::run(const std::string& id) {
    const auto result=impl_->database.execute("SELECT id,session_id,state,COALESCE(parent_run_id,''),COALESCE(node_id,''),EXISTS(SELECT 1 FROM graph_roots WHERE run_id=runs.id),COALESCE((SELECT CASE WHEN json_type(m.payload,'$.provider_context') IS NULL THEN '' ELSE json_quote(json_extract(m.payload,'$.provider_context')) END FROM task_messages t JOIN messages m ON m.seq=t.message_seq WHERE t.run_id=runs.id AND m.role='user' ORDER BY m.seq LIMIT 1),'') FROM runs WHERE id=?",{id});
    if(result.rows.empty()) throw NotFound("Run not found: "+id);
    const auto& context=text(result.rows[0][6]);return {text(result.rows[0][0]),text(result.rows[0][1]),state_value(text(result.rows[0][2])),text(result.rows[0][3]),text(result.rows[0][4]),integer(result.rows[0][5])!=0,context.empty()?std::string{}:provider_context(Json::parse(context))};
}
Run Repository::create_run(const std::string& id,const std::string& session_id) {
    identifier(id); auto& db=impl_->database; Transaction transaction(db); session(session_id);refresh_admission(db);
    if(!db.execute("SELECT id FROM context_idle_owners WHERE session_id=? AND state='active'",{session_id}).rows.empty())throw Conflict("Session has an exclusive maintenance owner");
    if(!db.execute("SELECT id FROM runs WHERE id=?",{id}).rows.empty()) throw Conflict("Run already exists");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty())
        throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));impl_->event(id,"run.queued","{}"); transaction.commit(); return {id,session_id,RunState::queued};
}
std::optional<Run> Repository::incoming_message(const std::string& message,const std::string& context,const std::string& identity,const std::string& content){
    if(message.empty()||message.size()>256||message.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid incoming message identity");if(!context.empty())identifier(context);object_json(identity);
    if(!impl_->database.execute("SELECT message_id FROM a2a_continuation_messages WHERE message_id=?",{message}).rows.empty())throw Conflict("Incoming message ID belongs to an A2A continuation");
    const auto rows=impl_->database.execute("SELECT run_id,context_id,identity,content FROM incoming_messages WHERE message_id=?",{message}).rows;if(rows.empty())return {};
    if((!context.empty()&&text(rows[0][1])!=context)||text(rows[0][2])!=Json::parse(identity).dump()||text(rows[0][3])!=content)throw Conflict("Incoming message identity was reused with changed input");return run(text(rows[0][0]));
}
std::optional<Run> Repository::a2a_continuation(const std::string& message,const std::string& task,const std::string& context,const std::string& identity,const std::string& content,const std::string& payload){
    if(message.empty()||message.size()>256||message.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid A2A continuation identity");identifier(task);if(!context.empty())identifier(context);const auto exact_identity=object_json(identity,65536),exact_payload=object_json(payload,65536);
    const auto rows=impl_->database.execute("SELECT run_id,context_id,identity,content,payload FROM a2a_continuation_messages WHERE message_id=?",{message}).rows;if(rows.empty())return {};
    if(text(rows[0][0])!=task||(!context.empty()&&text(rows[0][1])!=context)||text(rows[0][2])!=exact_identity||text(rows[0][3])!=content||text(rows[0][4])!=exact_payload)throw Conflict("A2A continuation identity was reused with changed input");return run(task);
}
Run Repository::continue_a2a_message(const std::string& task,const DynamicHumanInputSpec& answer,const std::string& message,const std::string& context,const std::string& identity,const std::string& content,const std::string& payload){
    if(message.empty()||message.size()>256||message.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid A2A continuation identity");identifier(answer.authenticated_actor);if(answer.authenticated_actor.size()>4096||content.empty()||content.size()>65536||content.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid A2A continuation input");const auto exact_identity=object_json(identity,65536),exact_payload=object_json(payload,65536),input=object_json(answer.input_json,16384);
    auto& db=impl_->database;Transaction tx(db);if(const auto replay=a2a_continuation(message,task,context,exact_identity,content,exact_payload)){tx.commit();return *replay;}
    if(!db.execute("SELECT message_id FROM incoming_messages WHERE message_id=?",{message}).rows.empty())throw Conflict("A2A continuation message ID belongs to an initial message");const auto owner=run(task);if(!owner.parent_id.empty()||owner.graph_root||owner.state!=RunState::paused||(!context.empty()&&owner.session_id!=context))throw Conflict("A2A continuation task is not a paused local agent");
    auto p=dynamic_plan(answer.plan_id);if(p.root_run_id!=owner.id||p.state!="waiting_human"||p.revision!=answer.expected_revision||p.state_sequence!=answer.expected_state_sequence||p.capabilities.backend_identity!=answer.backend_identity)throw DynamicPlanChanged("A2A human continuation plan changed");auto h=dynamic_human_request(p.id,answer.request_id);if(h.root_run_id!=owner.id||h.state!="waiting"||h.expires_unix_ms<=now_ms())throw Conflict("A2A task has no matching live human question");
    const auto existing=db.execute("SELECT id FROM dynamic_human_requests WHERE plan_id=? AND state='waiting'",{p.id}).rows;if(existing.size()!=1||text(existing[0][0])!=h.id)throw Conflict("A2A continuation requires exactly one pending human question");dynamic_cas(p,answer.expected_revision,answer.expected_state_sequence,answer.backend_identity);dynamic_safe_sequence(p.state_sequence);
    const auto outcome=Json{{"human_state","answered"},{"request_id",h.id},{"input_json",input}}.dump();if(outcome.size()>32768)throw std::invalid_argument("Encoded human observation exceeds its native outcome bound");
    const auto answered=impl_->event(owner.id,"plan.human.answered",Json{{"request_id",h.id},{"plan_id",p.id},{"label",h.label},{"input_json",input},{"actor",answer.authenticated_actor},{"source","a2a"}}.dump());changed_one(db.execute("UPDATE dynamic_human_requests SET state='answered',input_json=?,actor=?,input_event_seq=? WHERE id=? AND plan_id=? AND state='waiting'",{input,answer.authenticated_actor,answered.sequence,h.id,p.id}));
    const auto settled=impl_->event(owner.id,"plan.human.settled",outcome);changed_one(db.execute("UPDATE dynamic_plan_nodes SET state='settled',outcome_json=?,settled_event_seq=? WHERE plan_id=? AND label=? AND human_request_id=? AND definition_revision=? AND state='waiting_human'",{outcome,settled.sequence,p.id,h.label,h.id,h.definition_revision}));changed_one(db.execute("UPDATE dynamic_plans SET state_sequence=state_sequence+1 WHERE id=? AND state_sequence=?",{p.id,p.state_sequence}));
    const auto user_prompt=Json{{"content",content},{"a2a_message_id",message},{"a2a_message_payload",Json::parse(exact_payload)}}.dump();changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{owner.session_id,user_prompt}));changed_one(db.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{owner.id}));const auto admitted=impl_->event(owner.id,"a2a.message.continued",Json{{"message_id",message},{"human_request_id",h.id},{"context_id",owner.session_id}}.dump());
    changed_one(db.execute("INSERT INTO a2a_continuation_messages(message_id,run_id,context_id,identity,content,payload,human_request_id,event_seq) VALUES(?,?,?,?,?,?,?,?)",{message,owner.id,owner.session_id,exact_identity,content,exact_payload,h.id,admitted.sequence}));tx.commit();return run(owner.id);
}
Run Repository::start_incoming_message(const std::string& id,const std::string& context,const std::string& message,const std::string& prompt,const std::string& identity,std::optional<RootBudgetSpec> budget,std::optional<DynamicPlanCapabilities> dynamic){
    identifier(id);object_json(prompt);const auto provider=prompt_context(prompt);const auto data=Json::parse(prompt);if(!data.contains("content")||!data["content"].is_string())throw std::invalid_argument("Incoming prompt must contain text");const auto content=data["content"].get<std::string>();if(content.empty()||content.size()>65536||content.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid incoming text");Transaction transaction(impl_->database);if(const auto replay=incoming_message(message,context,identity,content)){transaction.commit();return *replay;}
    const auto session_id=context.empty()?"ctx_"+id:context;identifier(session_id);auto& db=impl_->database;refresh_admission(db);
    if(context.empty())changed_one(db.execute("INSERT INTO sessions(id,title) VALUES(?,'Inbound agent conversation')",{session_id}));else session(session_id);
    if(!db.execute("SELECT id FROM context_idle_owners WHERE session_id=? AND state='active'",{session_id}).rows.empty())throw Conflict("Session has an exclusive maintenance owner");
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
std::optional<std::string> Repository::incoming_message_payload(const std::string& id,const std::string& message){
    run(id);if(!message.empty()){const auto continuation=impl_->database.execute("SELECT payload FROM a2a_continuation_messages WHERE run_id=? AND message_id=?",{id,message}).rows;if(!continuation.empty())return text(continuation[0][0]);const auto incoming=impl_->database.execute("SELECT identity FROM incoming_messages WHERE run_id=? AND message_id=?",{id,message}).rows;if(!incoming.empty())return text(incoming[0][0]);return {};}
    const auto rows=impl_->database.execute("SELECT identity FROM incoming_messages WHERE run_id=?",{id}).rows;if(rows.empty())return {};return text(rows[0][0]);
}
Run Repository::start_prompt_run(const std::string& id,const std::string& session_id,const std::string& prompt_json,std::optional<RootBudgetSpec> budget,std::optional<DynamicPlanCapabilities> dynamic,const std::string& inbox_id) {
    identifier(id);const auto provider=prompt_context(prompt_json);auto& db=impl_->database;Transaction transaction(db);session(session_id);refresh_admission(db);
    if(!db.execute("SELECT id FROM context_idle_owners WHERE session_id=? AND state='active'",{session_id}).rows.empty())throw Conflict("Session has an exclusive maintenance owner");
    if(!db.execute("SELECT id FROM runs WHERE id=?",{id}).rows.empty()) throw Conflict("Run already exists");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty()) throw Conflict("Session already has an active root run");
    if(!inbox_id.empty()){
        identifier(inbox_id);const auto queued=db.execute("SELECT content,files_json FROM session_inbox WHERE id=? AND session_id=? AND state='queued' AND run_id IS NULL",{inbox_id,session_id}).rows;
        if(queued.size()!=1)throw Conflict("Queued session input is no longer available");
        Json prompt;try{prompt=Json::parse(prompt_json);}catch(...){throw std::invalid_argument("Invalid queued session prompt");}
        const auto saved_files=Json::parse(text(queued[0][1]));const auto prompt_files=prompt.value("files",Json::array());
        if(!saved_files.is_array()||!prompt_files.is_array()||saved_files.size()!=prompt_files.size())throw Conflict("Queued session attachments differ from the admitted prompt");
        if(!prompt.is_object()||!prompt.contains("content")||!prompt["content"].is_string())throw Conflict("Queued session input differs from the admitted prompt");
        std::string expected_content=text(queued[0][0]);
        if(!saved_files.empty()){expected_content+="\n\nAttached workspace files:";for(const auto& file:saved_files)expected_content+="\n- "+file.value("path",std::string{});}
        if(prompt["content"].get<std::string>()!=expected_content)throw Conflict("Queued session input differs from the admitted prompt");
        for(std::size_t i=0;i<saved_files.size();++i)if(!prompt_files[i].is_object()||prompt_files[i].value("path",std::string{})!=saved_files[i].value("path",std::string{})||prompt_files[i].value("folder_id",std::string{})!=saved_files[i].value("folder_id",std::string{})||prompt_files[i].value("name",std::string{})!=saved_files[i].value("name",std::string{}))throw Conflict("Queued session attachment identity changed before admission");
    }
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,prompt_json}));
    changed_one(db.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));if(!inbox_id.empty())changed_one(db.execute("UPDATE session_inbox SET state='claimed',run_id=? WHERE id=? AND session_id=? AND state='queued'",{id,inbox_id,session_id}));if(budget)impl_->initialize_budget(id,prompt_json,*budget);impl_->initialize_dynamic(id,budget,dynamic);impl_->event(id,"run.queued","{}");transaction.commit();return {id,session_id,RunState::queued,{},{},false,provider};
}
ContextSnapshot Repository::context_snapshot(const ContextScope& scope,const ContextBinding& binding,ContextReadBound bound){Transaction tx(impl_->database);auto result=read_context_snapshot(impl_->database,scope,binding,bound);tx.commit();return result;}
ContextStatusObservation Repository::context_status_observation(const ContextScope& scope,const ContextBinding& binding){
    bounded_identity(scope.id,256);const auto kind=std::string(context_scope_name(scope.kind)),encoded=context_binding_json(binding).dump(),key=context_binding_key(binding);auto& db=impl_->database;Transaction tx(db);
    const auto exists=scope.kind==ContextScopeKind::session?db.execute("SELECT id FROM sessions WHERE id=? AND NOT EXISTS(SELECT 1 FROM deleted_sessions d WHERE d.id=sessions.id)",{scope.id}).rows:db.execute("SELECT id FROM runs WHERE id=? AND parent_run_id IS NOT NULL",{scope.id}).rows;if(exists.empty())throw NotFound("Context scope does not exist");
    ContextStatusObservation result;result.head.scope=scope;result.head.binding=binding;
    const auto heads=db.execute("SELECT revision,covered_ordinal,checkpoint_id,binding_json FROM context_heads WHERE scope_kind=? AND scope_id=? AND binding_key=?",{kind,scope.id,key}).rows;
    if(!heads.empty()){const auto& h=heads[0];if(text(h[3])!=encoded)throw ContextBindingChanged("Context status authority differs");result.head.revision=integer(h[0]);result.head.covered_through_ordinal=integer(h[1]);if(!std::holds_alternative<std::nullptr_t>(h[2]))result.head.checkpoint_id=text(h[2]);}
    const auto watermark=db.execute("SELECT max(ordinal) FROM conversation_groups WHERE scope_kind=? AND scope_id=?",{kind,scope.id}).rows.at(0).at(0);if(!std::holds_alternative<std::nullptr_t>(watermark))result.source_watermark=integer(watermark);
    if(result.head.revision<0||result.head.covered_through_ordinal<0||result.head.covered_through_ordinal>result.source_watermark||(result.head.revision==0?(result.head.covered_through_ordinal!=0||result.head.checkpoint_id.has_value()):(result.head.covered_through_ordinal<1||!result.head.checkpoint_id)))throw DatabaseError("Context status head differs from indexed source");
    result.current_manual=context_current_manual_request(scope,binding);
    // json() removes only presentation whitespace from the already validated
    // numeric metrics. The original raw receipt/usage stays in its ledger;
    // neither its canonical window nor raw response crosses this observation.
    if(result.head.checkpoint_id){const auto checkpoints=db.execute("SELECT k.compaction_id,json(k.actual_usage_json),k.measured_elapsed_ms,c.preparation_elapsed_ms FROM context_checkpoints k JOIN context_compactions c ON c.id=k.compaction_id WHERE k.id=? AND c.state='committed' AND c.scope_kind=? AND c.scope_id=? AND c.binding_key=? AND c.binding_json=? AND c.head_revision=? AND c.covered_ordinal=?",{*result.head.checkpoint_id,kind,scope.id,key,encoded,result.head.revision-1,result.head.covered_through_ordinal}).rows;if(checkpoints.size()!=1)throw DatabaseError("Context status lacks its exact committed checkpoint");const auto& c=checkpoints[0];ContextCheckpointObservation observed{text(c[0]),text(c[1]),integer(c[2]),{}};if(observed.compaction_id!=*result.head.checkpoint_id||observed.actual_usage_json.size()>65536||observed.measured_elapsed_ms<0)throw DatabaseError("Context status checkpoint metrics are invalid");if(!std::holds_alternative<std::nullptr_t>(c[3])){observed.measured_preparation_elapsed_ms=integer(c[3]);if(*observed.measured_preparation_elapsed_ms<0)throw DatabaseError("Context status preparation timing is invalid");}result.checkpoint=std::move(observed);}
    tx.commit();return result;
}
namespace {
std::vector<ContextHistoryPayload> read_context_payloads(XlangSqlite& db,const ContextSnapshot& snapshot,
    const std::vector<std::int64_t>& ordinals,ContextReadBound bound,bool allow_authority_replay){
    context_read_bound(bound);
    if(ordinals.empty()||ordinals.size()>bound.max_groups)throw std::invalid_argument("Invalid context payload selection");
    const auto current=read_context_snapshot(db,snapshot.head.scope,snapshot.head.binding,bound);
    if(context_snapshot_json(current)!=context_snapshot_json(snapshot))throw ContextUnavailable("Context source or head changed");
    std::vector<ContextHistoryPayload> result;std::set<std::int64_t> seen;std::size_t bytes=0;
    for(const auto ordinal:ordinals){
        if(!seen.insert(ordinal).second)throw std::invalid_argument("Duplicate context group selection");
        const ConversationGroup* group=nullptr;
        for(const auto& value:current.groups)if(value.ordinal==ordinal)group=&value;
        for(const auto& value:current.protected_users)if(value.ordinal==ordinal)group=&value;
        if(!group)throw NotFound("Context group is outside this snapshot");
        ContextHistoryPayload payload;payload.source.group=*group;
        for(const auto& row:db.execute("SELECT m.seq,m.role,m.payload FROM context_group_members g JOIN messages m ON m.seq=g.message_seq WHERE g.scope_kind=? AND g.scope_id=? AND g.ordinal=? ORDER BY g.position",
            {std::string(context_scope_name(current.head.scope.kind)),current.head.scope.id,ordinal}).rows){
            const auto size=text(row[2]).size();
            if(size>bound.max_payload_bytes-bytes)throw ContextCapacityExceeded("Selected context payload exceeds its byte bound");
            bytes+=size;payload.source.originals.push_back({integer(row[0]),text(row[1]),text(row[2])});
        }
        if(payload.source.originals.size()!=static_cast<std::size_t>(group->message_count)||
           context_messages_digest(payload.source.originals)!=group->source_binding||validate_context_group(payload.source.originals)!=group->kind)
            throw DatabaseError("Context original group differs from immutable metadata");
        payload.reproject_provider_items=validate_context_receipts(db,*group,payload.source.originals,current.head.binding,allow_authority_replay,&payload.producing_workspace_identity,&payload.model_changed);
        result.push_back(std::move(payload));
    }
    return result;
}
}
std::vector<ContextGroupPayload> Repository::context_group_payloads(const ContextSnapshot& snapshot,const std::vector<std::int64_t>& ordinals,ContextReadBound bound){
    Transaction tx(impl_->database);auto loaded=read_context_payloads(impl_->database,snapshot,ordinals,bound,false);
    std::vector<ContextGroupPayload> result;for(auto& payload:loaded)result.push_back(std::move(payload.source));
    tx.commit();return result;
}
std::vector<ContextHistoryPayload> Repository::context_history_payloads(const ContextSnapshot& snapshot,const std::vector<std::int64_t>& ordinals,ContextReadBound bound){
    Transaction tx(impl_->database);auto result=read_context_payloads(impl_->database,snapshot,ordinals,bound,true);
    tx.commit();return result;
}
std::optional<ContextProjection> Repository::context_projection(const ContextScope& scope,const ContextBinding& binding,ContextReadBound bound){
    auto& db=impl_->database;Transaction tx(db);const auto snapshot=read_context_snapshot(db,scope,binding,bound);if(!snapshot.head.checkpoint_id){tx.commit();return {};}
    const auto rows=db.execute("SELECT k.compaction_id,k.projection_json,k.actual_response_json,k.actual_usage_json,k.provider_response_id,k.provider_created_at,k.visible_summary,k.measured_elapsed_ms,k.kind,c.source_binding,c.protected_users_json,c.user_binding FROM context_checkpoints k JOIN context_compactions c ON c.id=k.compaction_id WHERE k.id=? AND c.state='committed'",{*snapshot.head.checkpoint_id}).rows;if(rows.size()!=1)throw DatabaseError("Context head checkpoint is not committed");const auto& r=rows[0];ContextProjection p;p.head=snapshot.head;p.compaction_id=text(r[0]);p.projection_json=text(r[1]);p.actual_response_json=text(r[2]);p.actual_usage_json=text(r[3]);if(!std::holds_alternative<std::nullptr_t>(r[4]))p.provider_response_id=text(r[4]);if(!std::holds_alternative<std::nullptr_t>(r[5]))p.provider_created_at=integer(r[5]);if(!std::holds_alternative<std::nullptr_t>(r[6]))p.visible_summary=text(r[6]);p.measured_elapsed_ms=integer(r[7]);p.kind=text(r[8])=="responses_canonical"?ContextProjectionKind::responses_canonical:ContextProjectionKind::structured_summary;p.source_manifest_binding=text(r[9]);p.protected_user_ordinals=Json::parse(text(r[10])).get<std::vector<std::int64_t>>();p.protected_user_binding=text(r[11]);const auto preparation=db.execute("SELECT preparation_elapsed_ms FROM context_compactions WHERE id=?",{p.compaction_id}).rows.at(0).at(0);if(!std::holds_alternative<std::nullptr_t>(preparation))p.measured_preparation_elapsed_ms=integer(preparation);if(p.projection_json.size()>bound.max_payload_bytes)throw ContextCapacityExceeded("Context checkpoint exceeds read bound");if(p.kind!=ContextProjectionKind::responses_canonical)throw ContextUnavailable("Context strategy is not registered");const auto actual=parse_responses_compaction_response(p.actual_response_json);if(actual.window.items_json()!=p.projection_json||actual.usage_json!=p.actual_usage_json||p.provider_response_id!=std::optional<std::string>(actual.response_id)||p.provider_created_at!=actual.created_at)throw DatabaseError("Saved canonical checkpoint differs from its actual result");tx.commit();return p;
}
namespace {
void context_hash(const std::string& hash){if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("Invalid exact context digest");}
void graph_context_path(const std::string& path){
    if(path.empty()||path.size()>259)throw std::invalid_argument("Invalid graph context node path");std::size_t start=0,parts=0;
    for(;;){const auto end=path.find('/',start),size=end==std::string::npos?path.size()-start:end-start;const auto component=path.substr(start,size);if(size<1||size>64||++parts>4||component.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid graph context node path");if(end==std::string::npos)break;start=end+1;}
}
Json graph_context_json(const GraphContextSpec& s){
    context_hash(s.backend_identity);if(s.agents.size()>8)throw std::invalid_argument("Registered graph context exceeds eight agent nodes");Json agents=Json::array();std::set<std::string> ids;for(const auto& a:s.agents){graph_context_path(a.node_id);context_hash(a.backend_identity);if(!ids.insert(a.node_id).second||a.turn_limit<1||a.turn_limit>16)throw std::invalid_argument("Registered graph agent authority or turn limit differs");auto policy=RootBudgetSpec{"native.graph-context",1,"workspace:validation",a.provider_identity_json,8,2,32,600000};const auto identity=budget_provider_identity(policy);if(identity.at("model_id")!=a.model_id)throw std::invalid_argument("Registered graph model differs from captured provider");agents.push_back({{"node_id",a.node_id},{"model_id",a.model_id},{"provider_identity_json",identity.dump()},{"backend_identity",a.backend_identity},{"turn_limit",a.turn_limit}});}Json result={{"backend_identity",s.backend_identity},{"agents",agents}};if(!s.token_context)result["token_context"]=false;return result;
}
GraphContextSpec decode_graph_context(const std::string& raw){const auto j=Json::parse(object_json(raw,65536));GraphContextSpec s;s.token_context=j.value("token_context",true);s.backend_identity=j.at("backend_identity").get<std::string>();for(const auto& a:j.at("agents"))s.agents.push_back({a.at("node_id"),a.at("model_id"),a.at("provider_identity_json"),a.at("backend_identity"),a.at("turn_limit")});if(graph_context_json(s)!=j)throw DatabaseError("Captured graph context authority shape differs");return s;}
const GraphContextAgent& graph_context_agent(const GraphContextSpec& s,const std::string& node){const auto a=std::find_if(s.agents.begin(),s.agents.end(),[&](const auto& v){return v.node_id==node;});if(a==s.agents.end())throw ContextBindingChanged("Graph agent has no exact captured context authority");return *a;}
void graph_context_drained(XlangSqlite& db,const std::string& root,bool safe_effects,const char* controller_states=nullptr){
    const std::string tree="WITH RECURSIVE tree(id,depth) AS (SELECT ?,0 UNION ALL SELECT r.id,t.depth+1 FROM runs r JOIN tree t ON r.parent_run_id=t.id WHERE t.depth<4) ";
    const auto rows=db.execute(tree+"SELECT r.id,r.state FROM tree t JOIN runs r ON r.id=t.id ORDER BY t.depth,r.id",{root}).rows;
    if(rows.size()>257)throw Conflict("Registered graph drain exceeds its immutable tree limit");
    for(const auto& row:rows){const auto id=text(row[0]);if(id==root||terminal(state_value(text(row[1]))))continue;
        if(!controller_states||db.execute(std::string("SELECT gi.run_id FROM graph_invocations gi JOIN runs r ON r.id=gi.run_id WHERE gi.run_id=? AND gi.root_run_id=? AND r.state IN (")+controller_states+")",{id,root}).rows.empty())throw Conflict("Registered graph still owns executable descendants");
    }
    if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{root}).rows.empty()||!db.execute("SELECT id FROM context_compactions WHERE root_run_id=? AND state IN ('reserved','started','received')",{root}).rows.empty()||!db.execute("SELECT id FROM context_measures WHERE root_run_id=? AND state IN ('reserved','started')",{root}).rows.empty())throw Conflict("Registered graph still owns context/model transport");
    const auto states=safe_effects?"'awaiting_approval','ready','executing','uncertain'":"'executing'";
    if(!db.execute(tree+"SELECT o.id FROM operations o JOIN tree t ON t.id=o.run_id WHERE o.state IN ("+states+")",{root}).rows.empty())throw Conflict("Registered graph still owns unresolved descendant effects");
}
Json graph_context_pins(XlangSqlite& db,const std::string& root){Json pins=Json::array();for(const auto& row:db.execute("SELECT DISTINCT owner_run_id,spec_json FROM inference_steps WHERE root_run_id=? ORDER BY owner_run_id",{root}).rows){const auto s=Json::parse(text(row[1]));const ContextScope scope{s.at("scope_kind")=="session"?ContextScopeKind::session:ContextScopeKind::execution,s.at("scope_id")};const auto binding=decode_context_binding(s.at("binding").dump());const auto snapshot=read_context_snapshot(db,scope,binding,{});std::vector<std::int64_t> users;for(const auto& u:snapshot.protected_users)users.push_back(u.ordinal);if(snapshot.latest_user_ordinal&&std::find(users.begin(),users.end(),snapshot.latest_user_ordinal)==users.end())users.push_back(snapshot.latest_user_ordinal);ContextPausePin pin{snapshot.head,snapshot.source_watermark,context_source_binding(snapshot,snapshot.source_watermark),context_user_binding(snapshot,users)};pins.push_back({{"owner",text(row[0])},{"pin",context_pause_json(pin)}});}return pins;}
void graph_context_closed(XlangSqlite& db,const std::string& root,const DynamicBudgetSegment& segment){
    const auto rows=db.execute("SELECT paused_segment_id,paused_pins_json FROM graph_context_owners WHERE root_run_id=?",{root}).rows;if(rows.size()!=1||std::holds_alternative<std::nullptr_t>(rows[0][0])||std::holds_alternative<std::nullptr_t>(rows[0][1])||text(rows[0][0])!=segment.id||segment.state!="closed"||segment.remaining_active_ms<1)throw ContextBindingChanged("Registered graph has no verified closed clock");graph_context_drained(db,root,true,"'paused'");if(Json::parse(text(rows[0][1]))!=graph_context_pins(db,root))throw ContextSourceChanged("Registered graph closed context sources changed");
}
Json inference_identity(const InferenceStepSpec& s){return {{"root",s.root_run_id},{"owner",s.owner_run_id},{"role",call_role(s.role)},{"scope_kind",context_scope_name(s.scope.kind)},{"scope_id",s.scope.id},{"binding",context_binding_json(s.binding)},{"planning_call",s.planning_call_id?Json(*s.planning_call_id):Json(nullptr)}};}
void history_owner(Repository& repo,const std::string& root,const std::string& owner,ModelCallRole role,const ContextScope& scope,const std::string& provider_identity,const std::string& authority_identity){
    const auto r=repo.run(root),o=repo.run(owner);const auto b=repo.root_budget(root);
    if(r.state!=RunState::running||o.state!=RunState::running)throw ContextBindingChanged("Context execution owners changed");
    if(b.spec.policy_id!="native.graph-context"&&worker_provider_identity(repo,root,owner)!=Json::parse(provider_identity))throw ContextBindingChanged("Context provider binding changed");
    if(b.spec.policy_id=="native.dynamic-plan"){const auto caps=repo.dynamic_capabilities(root);if(!caps.catalogue_finalized)throw ContextUnavailable("Context requires its actual sealed execution catalogue");if(role==ModelCallRole::parent&&authority_identity!=caps.backend_identity)throw ContextBindingChanged("Parent context private authority differs from sealed settings");}
    if(role==ModelCallRole::parent){if(root!=owner||r.graph_root||scope.kind!=ContextScopeKind::session||scope.id!=r.session_id)throw Conflict("Parent context does not own this session");}
    else {const auto a=repo.child_admission(owner);if(a.root_run_id!=root||scope.kind!=ContextScopeKind::execution||scope.id!=owner||(a.kind!=ChildAdmissionKind::dynamic_agent&&a.kind!=ChildAdmissionKind::delegated_leaf&&!(a.kind==ChildAdmissionKind::graph_agent&&b.spec.policy_id=="native.graph-context")))throw Conflict("Child context does not own this execution");if((a.kind==ChildAdmissionKind::dynamic_agent||a.kind==ChildAdmissionKind::graph_agent)&&authority_identity!=a.backend_identity)throw ContextBindingChanged("Context authority differs from its exact captured child settings");if(a.kind==ChildAdmissionKind::graph_agent){const auto capture=repo.graph_context_owner(root);const auto& agent=graph_context_agent(capture.authority,a.graph_node_path);if(Json::parse(agent.provider_identity_json)!=Json::parse(provider_identity))throw ContextBindingChanged("Graph context provider differs from its captured node model");}}
}
void context_owner(Repository& repo,const std::string& root,const std::string& owner,ModelCallRole role,const ContextScope& scope,const ContextBinding& binding){history_owner(repo,root,owner,role,scope,binding.provider_identity_json,binding.authority_identity);}

std::int64_t context_logical_limit(Repository& repo,const std::string& root,const std::string& owner,ModelCallRole role){
    const auto b=repo.root_budget(root);if(role==ModelCallRole::parent)return b.spec.policy_id=="native.dynamic-plan"?repo.dynamic_capabilities(root).max_parent_turns:32;
    const auto a=repo.child_admission(owner);if(a.kind==ChildAdmissionKind::graph_agent){const auto capture=repo.graph_context_owner(root);return graph_context_agent(capture.authority,a.graph_node_path).turn_limit;}if(a.kind==ChildAdmissionKind::delegated_leaf)return 4;
    const auto p=repo.dynamic_plan(a.plan_id);for(const auto& n:p.nodes)if(n.child_run_id==owner)return dynamic_preset(p.capabilities,n.definition).turn_limit;throw Conflict("Child logical policy is unavailable");
}
void context_charge(XlangSqlite& db,const RootBudgetRecord& b,std::int64_t calls,std::int64_t held=0){
    if(calls<1||held<0||held>b.parent_calls_held||b.model_calls_reserved+b.parent_calls_held+calls-held>b.spec.max_model_calls)throw RootBudgetExhausted("Funded context and inference exceed physical root allowance");
    if(b.revision>=9007199254740991)throw RootBudgetExhausted("Root budget revision exhausted");
    changed_one(db.execute("UPDATE agent_execution_budgets SET model_calls_reserved=model_calls_reserved+?,parent_calls_held=parent_calls_held-?,revision=revision+1 WHERE root_run_id=? AND revision=?",{calls,held,b.root_run_id,b.revision}));
}
void context_logical_admission(Repository& repo,XlangSqlite& db,const std::string& root,const std::string& owner,ModelCallRole role,std::int64_t consumed_hold=0){
    const auto b=repo.root_budget(root);const auto count=integer(db.execute("SELECT count(DISTINCT COALESCE(inference_step_id,attempt_id)) FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=? AND purpose='inference'",{root,owner}).rows.at(0).at(0));
    if(count+(role==ModelCallRole::parent?b.parent_calls_held-consumed_hold:0)>=context_logical_limit(repo,root,owner,role))throw RootBudgetExhausted("Context follow-up exceeds logical owner allowance");
}
ModelCallReservation read_context_attempt(XlangSqlite& db,const std::string& root,const std::string& attempt){const auto rows=db.execute("SELECT owner_run_id,role,state,purpose,COALESCE(inference_step_id,'') FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{root,attempt}).rows;if(rows.empty())throw NotFound("Context physical attempt is absent");const auto& r=rows[0];return {root,attempt,text(r[0]),text(r[1])=="parent"?ModelCallRole::parent:ModelCallRole::leaf,text(r[2]),text(r[3])=="inference"?ModelCallPurpose::inference:ModelCallPurpose::context_compaction,text(r[4])};}
void insert_pending_inference(XlangSqlite& db,const InferenceStepSpec& s,bool attach_existing=false){
    changed_one(db.execute("INSERT INTO inference_steps(id,root_run_id,owner_run_id,original_attempt_id,role,state,planning_call_id,spec_json) VALUES(?,?,?,?,?,'reserved',?,?)",{s.id,s.root_run_id,s.owner_run_id,s.attempt_id,std::string(call_role(s.role)),s.planning_call_id?SqlValue(*s.planning_call_id):SqlValue(nullptr),inference_identity(s).dump()}));
    if(attach_existing)changed_one(db.execute("UPDATE agent_model_call_reservations SET inference_step_id=? WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND purpose='inference' AND inference_step_id IS NULL AND state='reserved'",{s.id,s.root_run_id,s.attempt_id,s.owner_run_id}));
    else changed_one(db.execute("INSERT INTO agent_model_call_reservations(root_run_id,attempt_id,owner_run_id,role,state,purpose,inference_step_id) VALUES(?,?,?,?,'reserved','inference',?)",{s.root_run_id,s.attempt_id,s.owner_run_id,std::string(call_role(s.role)),s.id}));
    changed_one(db.execute("INSERT INTO inference_attempt_members(step_id,root_run_id,attempt_id,ordinal,binding_json,input_binding) VALUES(?,?,?,0,'null','')",{s.id,s.root_run_id,s.attempt_id}));
}
struct SavedCompaction {ContextScope scope;ContextBinding binding;ContextAdmission admission;std::string id,attempt,state,snapshot,policy,input,input_hash,source,tail,user;std::int64_t head=0,watermark=0,covered=0,protected_from=0,opened=0;std::vector<std::int64_t> users;std::optional<std::string> manual;};
SavedCompaction read_compaction(XlangSqlite& db,const std::string& id){const auto rows=db.execute("SELECT maintenance_attempt_id,state,scope_kind,scope_id,binding_json,admission_json,snapshot_json,policy_json,input_json,input_binding,source_binding,tail_binding,user_binding,head_revision,source_watermark,covered_ordinal,protected_from,opened_ms,protected_users_json,manual_request_id FROM context_compactions WHERE id=?",{id}).rows;if(rows.empty())throw NotFound("Context maintenance is absent");const auto& r=rows[0];SavedCompaction s;s.id=id;s.attempt=text(r[0]);s.state=text(r[1]);s.scope={text(r[2])=="session"?ContextScopeKind::session:ContextScopeKind::execution,text(r[3])};s.binding=decode_context_binding(text(r[4]));s.admission=context_admission(text(r[5]));s.snapshot=text(r[6]);s.policy=text(r[7]);s.input=text(r[8]);s.input_hash=text(r[9]);s.source=text(r[10]);s.tail=text(r[11]);s.user=text(r[12]);s.head=integer(r[13]);s.watermark=integer(r[14]);s.covered=integer(r[15]);s.protected_from=integer(r[16]);s.opened=integer(r[17]);s.users=Json::parse(text(r[18])).get<std::vector<std::int64_t>>();if(!std::holds_alternative<std::nullptr_t>(r[19]))s.manual=text(r[19]);return s;}
std::string compaction_owner(const SavedCompaction& s){return std::visit([](const auto& a){using T=std::decay_t<decltype(a)>;if constexpr(std::is_same_v<T,IdleContextAdmission>)return a.owner_id;else return a.owner_run_id;},s.admission);}
void context_journal(XlangSqlite& db,const SavedCompaction& s,const std::string& kind,const Json& payload){
    const auto owner=compaction_owner(s);if(std::holds_alternative<IdleContextAdmission>(s.admission))changed_one(db.execute("INSERT INTO context_events(scope_kind,scope_id,owner_id,kind,payload) VALUES(?,?,?,?,?)",{std::string(context_scope_name(s.scope.kind)),s.scope.id,owner,kind,payload.dump()}));
    else changed_one(db.execute("INSERT INTO events(run_id,kind,payload) VALUES(?,?,?)",{owner,kind,payload.dump()}));
}
void validate_compaction_source(XlangSqlite& db,const SavedCompaction& s){const auto current=read_context_snapshot(db,s.scope,s.binding,{});if(context_snapshot_json(current).dump()!=s.snapshot||context_source_binding(current,s.covered)!=s.source||context_tail_binding(current,s.protected_from)!=s.tail||context_user_binding(current,s.users)!=s.user)throw ContextSourceChanged("Context head or source changed before installation");}
void validate_compaction_owner(Repository& repo,XlangSqlite& db,const SavedCompaction& s){
    const auto elapsed=now_ms()-s.opened;const auto p=Json::parse(s.policy);if(elapsed<0||elapsed>p.at("maintenance_deadline_ms").get<std::int64_t>())throw ContextUnavailable("Context maintenance deadline expired");
    std::visit([&](const auto& a){using T=std::decay_t<decltype(a)>;if constexpr(std::is_same_v<T,IdleContextAdmission>){const auto row=db.execute("SELECT state,session_id,opened_ms,wall_limit_ms FROM context_idle_owners WHERE id=?",{a.owner_id}).rows;if(row.empty()||text(row[0][0])!="active"||text(row[0][1])!=s.scope.id||now_ms()-integer(row[0][2])>integer(row[0][3])||!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{s.scope.id}).rows.empty())throw ContextUnavailable("Idle context owner changed");}else context_owner(repo,a.root_run_id,a.owner_run_id,std::is_same_v<T,PlanningContextAdmission>?ModelCallRole::parent:[&]{if constexpr(std::is_same_v<T,PlanningContextAdmission>)return ModelCallRole::parent;else return a.role;}(),s.scope,s.binding);},s.admission);
}
ContextStepReservation compaction_reservation(XlangSqlite& db,const SavedCompaction& s){ContextStepReservation out{s.id,s.attempt,s.state,s.scope,s.admission,{}};std::visit([&](const auto& a){using T=std::decay_t<decltype(a)>;if constexpr(!std::is_same_v<T,IdleContextAdmission>){const auto attempt=[&]{if constexpr(std::is_same_v<T,PlanningContextAdmission>)return a.held_attempt_id;else if constexpr(std::is_same_v<T,RebuildContextAdmission>)return a.retry_attempt_id;else return a.inference_attempt_id;}();out.inference=read_context_attempt(db,a.root_run_id,attempt);}},s.admission);return out;}
ContextProjection read_observed_projection(XlangSqlite& db,const SavedCompaction& s){const auto rows=db.execute("SELECT projection_json,actual_response_json,actual_usage_json,provider_response_id,provider_created_at,visible_summary,measured_elapsed_ms FROM context_checkpoints WHERE compaction_id=?",{s.id}).rows;if(rows.empty())throw NotFound("Observed maintenance receipt is absent");const auto& r=rows[0];ContextProjection p;p.head={s.scope,s.binding,s.head,s.covered,{}};if(s.state=="committed"){p.head.revision=s.head+1;p.head.checkpoint_id=s.id;}p.compaction_id=s.id;p.source_manifest_binding=s.source;p.projection_json=text(r[0]);p.actual_response_json=text(r[1]);p.actual_usage_json=text(r[2]);if(!std::holds_alternative<std::nullptr_t>(r[3]))p.provider_response_id=text(r[3]);if(!std::holds_alternative<std::nullptr_t>(r[4]))p.provider_created_at=integer(r[4]);if(!std::holds_alternative<std::nullptr_t>(r[5]))p.visible_summary=text(r[5]);p.measured_elapsed_ms=integer(r[6]);p.protected_user_ordinals=s.users;p.protected_user_binding=s.user;const auto preparation=db.execute("SELECT preparation_elapsed_ms FROM context_compactions WHERE id=?",{s.id}).rows.at(0).at(0);if(!std::holds_alternative<std::nullptr_t>(preparation))p.measured_preparation_elapsed_ms=integer(preparation);if(s.state!="committed")p.head.covered_through_ordinal=Json::parse(s.snapshot).at("covered").get<std::int64_t>();return p;}
Json measure_request_json(const ContextMeasureRequestSpec& s){return {{"id",s.id},{"owner",s.owner_id},{"payload",s.payload_binding},{"model",s.model_id},{"root",s.root_run_id?Json(*s.root_run_id):Json(nullptr)},{"compaction",s.compaction_id?Json(*s.compaction_id):Json(nullptr)},{"scope_kind",context_scope_name(s.scope.kind)},{"scope_id",s.scope.id},{"binding",context_binding_json(s.binding)},{"head",s.head_revision},{"watermark",s.source_watermark},{"bytes",s.serialized_bytes},{"max_requests",s.max_requests},{"deadline",s.deadline_ms},{"input",s.input_json},{"count_binding",s.count_request_binding}};}
ContextMeasureRequestSpec decode_measure(const std::string& raw){const auto j=Json::parse(raw);ContextMeasureRequestSpec s;s.id=j["id"];s.owner_id=j["owner"];s.payload_binding=j["payload"];s.model_id=j["model"];if(!j["root"].is_null())s.root_run_id=j["root"].get<std::string>();if(!j["compaction"].is_null())s.compaction_id=j["compaction"].get<std::string>();s.scope={j["scope_kind"]=="session"?ContextScopeKind::session:ContextScopeKind::execution,j["scope_id"].get<std::string>()};s.binding=decode_context_binding(j["binding"].dump());s.head_revision=j["head"];s.source_watermark=j["watermark"];s.serialized_bytes=j["bytes"];s.max_requests=j["max_requests"];s.deadline_ms=j["deadline"];s.input_json=j["input"];s.count_request_binding=j["count_binding"];return s;}
Json measure_result_json(const ContextInputMeasure& m){return {{"owner",m.owner_id},{"payload",m.payload_binding},{"model",m.model_id},{"scope_kind",context_scope_name(m.scope.kind)},{"scope_id",m.scope.id},{"binding",context_binding_json(m.binding)},{"head",m.head_revision},{"watermark",m.source_watermark},{"semantics",m.semantics==ContextMeasureSemantics::provider_exact?"provider_exact":m.semantics==ContextMeasureSemantics::provider_estimate?"provider_estimate":"serialized_bytes"},{"value",m.value},{"bytes",m.serialized_bytes},{"elapsed",m.measured_elapsed_ms},{"response",m.actual_response_json}};}
}
void Repository::record_context_history_replay(const ContextHistoryReplaySpec& spec){
    auto& db=impl_->database;Transaction tx(db);context_hash(spec.projection_binding);
    context_owner(*this,spec.root_run_id,spec.owner_run_id,spec.role,spec.source.head.scope,spec.source.head.binding);
    const auto loaded=read_context_payloads(db,spec.source,spec.ordinals,{},true);
    const auto workspace=root_budget(spec.root_run_id).spec.workspace_identity;
    std::size_t groups=0,messages=0;bool model_changed=false;
    for(const auto& payload:loaded)if(payload.reproject_provider_items){
        if(payload.producing_workspace_identity!=workspace)
            throw ContextBindingChanged("History replay belongs to another execution workspace");
        ++groups;
        model_changed|=payload.model_changed;
        for(const auto& original:payload.source.originals)if(original.role=="assistant"){
            const auto dto=Json::parse(original.json);
            if(dto.contains("provider_items")&&!dto.at("provider_items").empty())++messages;
        }
    }
    if(!groups||!messages)throw ContextBindingChanged("History replay has no changed producing authority");
    impl_->event(spec.owner_run_id,"context.history_reprojected",Json{
        {"reason",model_changed?"model_changed":"authority_changed"},{"scope_kind",context_scope_name(spec.source.head.scope.kind)},
        {"scope_id",spec.source.head.scope.id},{"head_revision",spec.source.head.revision},
        {"source_watermark",spec.source.source_watermark},{"source_binding",context_source_binding(spec.source,spec.source.source_watermark)},
        {"authority_identity",spec.source.head.binding.authority_identity},{"projection_binding",spec.projection_binding},
        {"groups",groups},{"messages",messages}}.dump());
    tx.commit();
}
namespace {
ProviderHistorySnapshot read_provider_history(Repository& repo,XlangSqlite& db,const ProviderHistoryOwner& owner,const ProviderHistorySelection& selection){
    context_hash(owner.authority_identity);const auto target=Json::parse(provider_context(Json::parse(owner.provider_identity_json)));
    history_owner(repo,owner.root_run_id,owner.owner_run_id,owner.role,owner.scope,owner.provider_identity_json,owner.authority_identity);
    const auto workspace=repo.root_budget(owner.root_run_id).spec.workspace_identity;
    const auto& bound=selection.bound;context_read_bound(bound);backfill_context_groups(db,owner.scope,bound);
    std::vector<ConversationGroup> groups;ProviderHistorySnapshot result;
    const auto kind=std::string(context_scope_name(owner.scope.kind));Json source=Json::object();
    if(selection.context){
        const auto& saved=*selection.context;
        if(saved.head.scope!=owner.scope||Json::parse(saved.head.binding.provider_identity_json)!=target||saved.head.binding.authority_identity!=owner.authority_identity)
            throw ContextBindingChanged("Provider history selection differs from its actual owner");
        const auto current=read_context_snapshot(db,owner.scope,saved.head.binding,bound);
        if(context_snapshot_json(current)!=context_snapshot_json(saved))throw ContextSourceChanged("Provider history source or head changed");
        if(selection.ordinals.empty()||selection.ordinals.size()>bound.max_groups)throw std::invalid_argument("Invalid provider history selection");
        std::set<std::int64_t> seen;
        for(const auto ordinal:selection.ordinals){
            if(!seen.insert(ordinal).second)throw std::invalid_argument("Duplicate provider history group");
            const ConversationGroup* found=nullptr;
            for(const auto& group:current.groups)if(group.ordinal==ordinal)found=&group;
            for(const auto& group:current.protected_users)if(group.ordinal==ordinal)found=&group;
            if(!found)throw NotFound("Provider history group is outside the captured source");groups.push_back(*found);
        }
        result.source_watermark=current.source_watermark;source["context"]=context_snapshot_json(current);source["ordinals"]=selection.ordinals;
    }else{
        if(!selection.ordinals.empty())throw std::invalid_argument("Original provider history does not accept partial selection");
        result.source_watermark=integer(db.execute("SELECT next_ordinal-1 FROM context_scopes WHERE kind=? AND id=?",{kind,owner.scope.id}).rows.at(0).at(0));
        for(const auto& row:db.execute(std::string("SELECT ")+context_group_columns+" FROM conversation_groups WHERE scope_kind=? AND scope_id=? ORDER BY ordinal LIMIT ?",{kind,owner.scope.id,static_cast<std::int64_t>(bound.max_groups+1)}).rows){
            if(groups.size()>=bound.max_groups)throw ContextCapacityExceeded("Provider history group count exceeds its bound");groups.push_back(decode_context_group(owner.scope,row));
        }
    }
    Json metadata=Json::array();std::size_t bytes=0,metadata_bytes=0,messages=0;
    for(const auto& group:groups){
        const auto group_metadata=context_group_json(group);metadata_bytes+=group_metadata.dump().size();
        if(metadata_bytes>bound.max_metadata_bytes)throw ContextCapacityExceeded("Provider history metadata exceeds its bound");metadata.push_back(group_metadata);
        ContextHistoryPayload payload;payload.source.group=group;
        for(const auto& row:db.execute("SELECT m.seq,m.role,m.payload FROM context_group_members g JOIN messages m ON m.seq=g.message_seq WHERE g.scope_kind=? AND g.scope_id=? AND g.ordinal=? ORDER BY g.position",{kind,owner.scope.id,group.ordinal}).rows){
            const auto size=text(row[2]).size();if(size>bound.max_payload_bytes-bytes||++messages>8192)throw ContextCapacityExceeded("Provider history payload exceeds its bound");bytes+=size;payload.source.originals.push_back({integer(row[0]),text(row[1]),text(row[2])});
        }
        if(payload.source.originals.size()!=static_cast<std::size_t>(group.message_count)||context_messages_digest(payload.source.originals)!=group.source_binding||validate_context_group(payload.source.originals)!=group.kind)
            throw DatabaseError("Provider history differs from its immutable original group");
        for(const auto& original:payload.source.originals){
            const auto proof=prove_history_receipt(db,group,original);if(!proof.has_items)continue;
            if(proof.workspace!=workspace)throw ContextBindingChanged("Provider history belongs to another execution workspace");
            const auto producing=Json::parse(provider_context(Json::parse(proof.provider_identity)));
            const auto dto=Json::parse(original.json);
            const auto captured=db.execute("SELECT payload FROM messages m JOIN task_messages t ON t.message_seq=m.seq WHERE t.run_id=? AND m.role='user' ORDER BY m.seq LIMIT 1",{*group.execution_owner}).rows;
            if(captured.size()!=1||!dto.contains("provider_context")||Json::parse(prompt_context(text(captured[0][0])))!=producing||!dto.contains("model")||dto.at("model")!=producing.at("model_id"))
                throw ContextBindingChanged("Provider history lacks its exact captured profile and model");
            bool changed=producing!=target;
            if(proof.binding){
                if(selection.context)changed|=*proof.binding!=selection.context->head.binding;
                else changed|=proof.binding->authority_identity!=owner.authority_identity;
            }else if(selection.context)changed=true; // No proof for native Responses continuation reuse.
            if(changed){
                payload.reproject_provider_items=true;payload.producing_workspace_identity=proof.workspace;
                auto from_profile=producing,to_profile=target;from_profile.erase("model_id");to_profile.erase("model_id");
                payload.provider_changed|=from_profile!=to_profile;payload.model_changed|=producing.at("model_id")!=target.at("model_id");
            }
        }
        result.groups.push_back(std::move(payload));
    }
    source["scope_kind"]=kind;source["scope_id"]=owner.scope.id;source["watermark"]=result.source_watermark;source["groups"]=std::move(metadata);
    source["root"]=owner.root_run_id;source["owner"]=owner.owner_run_id;source["role"]=call_role(owner.role);source["target"]=target;source["authority"]=owner.authority_identity;
    result.source_binding=context_digest(source.dump());return result;
}
}
ProviderHistorySnapshot Repository::provider_history(const ProviderHistoryOwner& owner,const ProviderHistorySelection& selection){
    auto& db=impl_->database;Transaction tx(db);auto result=read_provider_history(*this,db,owner,selection);tx.commit();return result;
}
void Repository::record_provider_history_replay(const ProviderHistoryReplaySpec& spec){
    auto& db=impl_->database;Transaction tx(db);context_hash(spec.source_binding);context_hash(spec.projection_binding);
    const auto current=read_provider_history(*this,db,spec.owner,spec.selection);
    if(current.source_binding!=spec.source_binding)throw ContextSourceChanged("Provider history changed before projection publication");
    std::size_t groups=0,messages=0;bool provider_changed=false,model_changed=false;
    for(const auto& payload:current.groups)if(payload.reproject_provider_items){
        ++groups;provider_changed|=payload.provider_changed;model_changed|=payload.model_changed;
        for(const auto& original:payload.source.originals)if(original.role=="assistant"){const auto dto=Json::parse(original.json);if(dto.contains("provider_items")&&!dto.at("provider_items").empty())++messages;}
    }
    if(!groups||!messages)throw ContextBindingChanged("Provider history has no changed continuation to project");
    Json event={{"reason",provider_changed?"provider_changed":model_changed?"model_changed":"authority_changed"},{"scope_kind",context_scope_name(spec.owner.scope.kind)},
        {"scope_id",spec.owner.scope.id},{"source_watermark",current.source_watermark},{"source_binding",current.source_binding},{"projection_binding",spec.projection_binding},{"groups",groups},{"messages",messages}};
    if(spec.selection.context)event["head_revision"]=spec.selection.context->head.revision;
    impl_->event(spec.owner.owner_run_id,"context.history_reprojected",event.dump());tx.commit();
}
ContextManualRequest Repository::request_context_compaction(const ContextManualRequestSpec& spec){
    bounded_identity(spec.id);bounded_identity(spec.actor);if(spec.expected_head_revision<0)throw std::invalid_argument("Invalid manual context head");(void)context_binding_json(spec.binding);auto& db=impl_->database;Transaction tx(db);
    const auto prior=db.execute("SELECT actor,scope_kind,scope_id,binding_json,expected_head_revision,state,owner_id,compaction_id FROM context_manual_requests WHERE id=?",{spec.id}).rows;ContextManualRequest result{spec.id,spec.actor,"pending",spec.scope,spec.binding,spec.expected_head_revision,{},{}};
    if(!prior.empty()){const auto& r=prior[0];if(text(r[0])!=spec.actor||text(r[1])!=context_scope_name(spec.scope.kind)||text(r[2])!=spec.scope.id||text(r[3])!=context_binding_json(spec.binding).dump()||integer(r[4])!=spec.expected_head_revision)throw Conflict("Authenticated manual request identity changed");result.state=text(r[5]);if(!std::holds_alternative<std::nullptr_t>(r[6]))result.owner_id=text(r[6]);if(!std::holds_alternative<std::nullptr_t>(r[7]))result.compaction_id=text(r[7]);tx.commit();return result;}
    const auto snapshot=read_context_snapshot(db,spec.scope,spec.binding,{});if(snapshot.head.revision!=spec.expected_head_revision)throw ContextSourceChanged("Manual context head changed");
    if(!db.execute("SELECT id FROM context_manual_requests WHERE scope_kind=? AND scope_id=? AND binding_key=? AND state IN ('pending','claimed')",{std::string(context_scope_name(spec.scope.kind)),spec.scope.id,context_binding_key(spec.binding)}).rows.empty())throw Conflict("Context already has an authenticated outstanding request");
    changed_one(db.execute("INSERT INTO context_manual_requests(id,actor,scope_kind,scope_id,binding_key,binding_json,expected_head_revision,state) VALUES(?,?,?,?,?,?,?,'pending')",{spec.id,spec.actor,std::string(context_scope_name(spec.scope.kind)),spec.scope.id,context_binding_key(spec.binding),context_binding_json(spec.binding).dump(),spec.expected_head_revision}));changed_one(db.execute("INSERT INTO context_events(scope_kind,scope_id,owner_id,kind,payload) VALUES(?,?,?,'context.manual.requested',?)",{std::string(context_scope_name(spec.scope.kind)),spec.scope.id,spec.actor,Json{{"request_id",spec.id},{"head_revision",spec.expected_head_revision}}.dump()}));tx.commit();return result;
}
ContextManualRequest Repository::context_manual_request(const std::string& id,const ContextBinding& binding){bounded_identity(id);const auto encoded=context_binding_json(binding).dump();const auto rows=impl_->database.execute("SELECT actor,state,scope_kind,scope_id,binding_json,expected_head_revision,owner_id,compaction_id FROM context_manual_requests WHERE id=?",{id}).rows;if(rows.empty())throw NotFound("Authenticated manual request is absent");const auto& r=rows[0];if(text(r[4])!=encoded)throw ContextBindingChanged("Manual request selected authority differs");ContextManualRequest out{id,text(r[0]),text(r[1]),{text(r[2])=="session"?ContextScopeKind::session:ContextScopeKind::execution,text(r[3])},binding,integer(r[5]),{}, {}};if(!std::holds_alternative<std::nullptr_t>(r[6]))out.owner_id=text(r[6]);if(!std::holds_alternative<std::nullptr_t>(r[7]))out.compaction_id=text(r[7]);return out;}
std::optional<ContextManualRequest> Repository::context_current_manual_request(const ContextScope& scope,const ContextBinding& binding){bounded_identity(scope.id);const auto encoded=context_binding_json(binding).dump();const auto rows=impl_->database.execute("SELECT id FROM context_manual_requests WHERE scope_kind=? AND scope_id=? AND binding_key=? AND state IN ('pending','claimed') ORDER BY rowid",{std::string(context_scope_name(scope.kind)),scope.id,context_binding_key(binding)}).rows;if(rows.empty())return {};if(rows.size()!=1)throw DatabaseError("Context has multiple outstanding authenticated requests");auto result=context_manual_request(text(rows[0][0]),binding);if(result.scope.kind!=scope.kind||result.scope.id!=scope.id||context_binding_json(result.binding).dump()!=encoded)throw ContextBindingChanged("Current manual request differs from selected scope");return result;}
ContextManualRequest Repository::retire_context_request(const std::string& id,const ContextBinding& binding,ContextFailureCode failure){
    auto& db=impl_->database;Transaction tx(db);const auto rows=db.execute("SELECT actor,scope_kind,scope_id,binding_json,expected_head_revision,state,owner_id,compaction_id,failure_code FROM context_manual_requests WHERE id=?",{id}).rows;if(rows.empty())throw NotFound("Authenticated manual request is absent");const auto& r=rows[0];if(text(r[3])!=context_binding_json(binding).dump()||!std::holds_alternative<std::nullptr_t>(r[7]))throw ContextBindingChanged("Manual retirement does not match an unclaimed exact binding");if(!std::holds_alternative<std::nullptr_t>(r[6]))throw Conflict("Preclaimed manual request requires its idle owner retirement");const auto code=std::string(context_failure(failure));if(text(r[5])=="failed"){if(text(r[8])!=code)throw Conflict("Manual request failure changed");}else {if(text(r[5])!="pending")throw Conflict("Manual request is already claimed or retired");changed_one(db.execute("UPDATE context_manual_requests SET state='failed',failure_code=? WHERE id=? AND state='pending' AND compaction_id IS NULL",{code,id}));changed_one(db.execute("INSERT INTO context_events(scope_kind,scope_id,owner_id,kind,payload) VALUES(?,?,?,'context.manual.failed',?)",{text(r[1]),text(r[2]),text(r[0]),Json{{"request_id",id},{"code",code}}.dump()}));}ContextManualRequest out{id,text(r[0]),"failed",{text(r[1])=="session"?ContextScopeKind::session:ContextScopeKind::execution,text(r[2])},binding,integer(r[4]),{},{}};tx.commit();return out;
}
IdleContextOwnerRecord Repository::idle_context_owner(const std::string& id){
    const auto rows=impl_->database.execute("SELECT session_id,manual_request_id,binding_json,head_revision,state,max_calls,calls_reserved,wall_limit_ms,opened_ms,elapsed_ms FROM context_idle_owners WHERE id=?",{id}).rows;if(rows.empty())throw NotFound("Idle context lease is absent");const auto& r=rows[0];IdleContextOwnerRecord out;out.spec={id,text(r[1]),{ContextScopeKind::session,text(r[0])},decode_context_binding(text(r[2])),integer(r[3]),integer(r[5]),integer(r[7])};out.state=text(r[4]);out.opened_unix_ms=integer(r[8]);out.model_calls_reserved=integer(r[6]);const auto spent=std::holds_alternative<std::nullptr_t>(r[9])?(out.state=="active"?std::max<std::int64_t>(0,now_ms()-out.opened_unix_ms):out.spec.wall_limit_ms):integer(r[9]);out.remaining_active_ms=std::max<std::int64_t>(0,out.spec.wall_limit_ms-spent);return out;
}
IdleContextOwnerRecord Repository::claim_idle_context_owner(const IdleContextOwnerSpec& spec){
    bounded_identity(spec.id);bounded_identity(spec.manual_request_id);if(spec.scope.kind!=ContextScopeKind::session||spec.expected_head_revision<0||spec.max_model_calls<1||spec.max_model_calls>4||spec.wall_limit_ms<1||spec.wall_limit_ms>120000)throw std::invalid_argument("Invalid idle context lease policy");const auto encoded=context_binding_json(spec.binding).dump();auto& db=impl_->database;Transaction tx(db);
    if(!db.execute("SELECT id FROM context_idle_owners WHERE id=?",{spec.id}).rows.empty()){const auto old=idle_context_owner(spec.id);if(old.spec.manual_request_id!=spec.manual_request_id||old.spec.scope!=spec.scope||old.spec.binding!=spec.binding||old.spec.expected_head_revision!=spec.expected_head_revision||old.spec.max_model_calls!=spec.max_model_calls||old.spec.wall_limit_ms!=spec.wall_limit_ms)throw Conflict("Idle context lease identity changed");tx.commit();return old;}
    refresh_admission(db);const auto snapshot=read_context_snapshot(db,spec.scope,spec.binding,{});if(snapshot.head.revision!=spec.expected_head_revision)throw ContextSourceChanged("Idle context lease head changed");const auto pending=db.execute("SELECT binding_json,expected_head_revision,state,owner_id,compaction_id FROM context_manual_requests WHERE id=? AND scope_kind='session' AND scope_id=?",{spec.manual_request_id,spec.scope.id}).rows;if(pending.size()!=1||text(pending[0][0])!=encoded||integer(pending[0][1])!=spec.expected_head_revision||text(pending[0][2])!="pending"||!std::holds_alternative<std::nullptr_t>(pending[0][3])||!std::holds_alternative<std::nullptr_t>(pending[0][4]))throw ContextBindingChanged("Idle lease does not match unclaimed authenticated request");
    if(!db.execute("SELECT id FROM runs WHERE id=? OR (session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused'))",{spec.id,spec.scope.id}).rows.empty()||!db.execute("SELECT id FROM context_idle_owners WHERE session_id=? AND state='active'",{spec.scope.id}).rows.empty()||!db.execute("SELECT id FROM context_compactions WHERE scope_kind='session' AND scope_id=? AND state IN ('reserved','started','received')",{spec.scope.id}).rows.empty()||!db.execute("SELECT id FROM context_measures WHERE scope_kind='session' AND scope_id=? AND state IN ('reserved','started')",{spec.scope.id}).rows.empty())throw Conflict("Session already owns inference or context work");
    changed_one(db.execute("INSERT INTO context_idle_owners(id,session_id,manual_request_id,binding_json,head_revision,state,max_calls,calls_reserved,wall_limit_ms,opened_ms) VALUES(?,?,?,?,?,'active',?,0,?,?)",{spec.id,spec.scope.id,spec.manual_request_id,encoded,spec.expected_head_revision,spec.max_model_calls,spec.wall_limit_ms,now_ms()}));changed_one(db.execute("UPDATE context_manual_requests SET owner_id=? WHERE id=? AND state='pending' AND owner_id IS NULL AND compaction_id IS NULL",{spec.id,spec.manual_request_id}));changed_one(db.execute("INSERT INTO context_events(scope_kind,scope_id,owner_id,kind,payload) VALUES('session',?,?,'context.idle.claimed',?)",{spec.scope.id,spec.id,Json{{"request_id",spec.manual_request_id},{"head_revision",spec.expected_head_revision}}.dump()}));const auto result=idle_context_owner(spec.id);tx.commit();return result;
}
void Repository::retire_idle_context_owner(const std::string& id,ContextFailureCode failure,std::int64_t elapsed) try {
    if(elapsed<0||elapsed>3600000)throw std::invalid_argument("Invalid idle context elapsed time");auto& db=impl_->database;Transaction tx(db);const auto old=idle_context_owner(id);const auto code=std::string(context_failure(failure)),state=code=="interrupted"?std::string("interrupted"):std::string("failed");if(old.state!="active"){const auto prior=db.execute("SELECT failure_code,elapsed_ms FROM context_idle_owners WHERE id=?",{id}).rows[0];if(old.state!=state||std::holds_alternative<std::nullptr_t>(prior[0])||text(prior[0])!=code||integer(prior[1])!=elapsed)throw Conflict("Retired idle context outcome changed");tx.commit();return;}
    if(!db.execute("SELECT id FROM context_compactions WHERE idle_owner_id=?",{id}).rows.empty()||!db.execute("SELECT id FROM context_measures WHERE owner_id=? AND state IN ('reserved','started')",{id}).rows.empty())throw Conflict("Idle lease still owns actual maintenance or counting; use its dedicated retirement");changed_one(db.execute("UPDATE context_idle_owners SET state=?,elapsed_ms=?,failure_code=? WHERE id=? AND state='active'",{state,elapsed,code,id}));changed_one(db.execute("UPDATE context_manual_requests SET state=?,failure_code=? WHERE id=? AND owner_id=? AND state='pending' AND compaction_id IS NULL",{state,code,old.spec.manual_request_id,id}));changed_one(db.execute("INSERT INTO context_events(scope_kind,scope_id,owner_id,kind,payload) VALUES('session',?,?,'context.idle.retired',?)",{old.spec.scope.id,id,Json{{"code",code},{"elapsed_ms",elapsed}}.dump()}));tx.commit();
}catch(const DatabaseError&){throw ContextOutcomeUnrecorded("Idle context retirement could not be durably recorded");}
namespace {
void validate_measure_owner(Repository& repo,XlangSqlite& db,const ContextMeasureRequestSpec& request,std::int64_t opened=0){
    if(opened&&(now_ms()<opened||now_ms()-opened>request.deadline_ms))throw ContextUnavailable("Context count deadline expired");
    const auto snapshot=read_context_snapshot(db,request.scope,request.binding,{});if(snapshot.head.revision!=request.head_revision||snapshot.source_watermark!=request.source_watermark)throw ContextSourceChanged("Input count source or head changed");
    const auto active=db.execute("SELECT id FROM context_compactions WHERE scope_kind=? AND scope_id=? AND state IN ('reserved','started','received')",{std::string(context_scope_name(request.scope.kind)),request.scope.id}).rows;
    if(request.compaction_id){const auto c=read_compaction(db,*request.compaction_id);if((c.state!="reserved"&&c.state!="started"&&c.state!="received")||c.scope!=request.scope||c.binding!=request.binding||compaction_owner(c)!=request.owner_id||c.head!=request.head_revision||c.watermark!=request.source_watermark)throw ContextBindingChanged("Input count must belong to its exact active compaction");validate_compaction_owner(repo,db,c);}
    else if(!active.empty())throw Conflict("Parallel count cannot enter an owned compaction");
    if(request.root_run_id)context_owner(repo,*request.root_run_id,request.owner_id,*request.root_run_id==request.owner_id?ModelCallRole::parent:ModelCallRole::leaf,request.scope,request.binding);
    else {if(request.scope.kind!=ContextScopeKind::session)throw Conflict("Idle count requires a real exclusive maintenance owner");const auto lease=repo.idle_context_owner(request.owner_id);if(lease.state!="active"||lease.spec.scope!=request.scope||lease.spec.binding!=request.binding||lease.spec.expected_head_revision!=request.head_revision||lease.remaining_active_ms<1)throw ContextBindingChanged("Count cannot invent or reset an idle execution");if(request.compaction_id){const auto c=read_compaction(db,*request.compaction_id);const auto* a=std::get_if<IdleContextAdmission>(&c.admission);if(!a||a->owner_id!=request.owner_id)throw Conflict("Count does not belong to its idle compaction");}}
}
}
ContextMeasureReservation Repository::begin_context_measure(const ContextMeasureRequestSpec& request){
    bounded_identity(request.id);bounded_identity(request.owner_id);context_hash(request.payload_binding);context_hash(request.count_request_binding);if(request.model_id.empty()||request.model_id.size()>256||request.serialized_bytes<1||request.serialized_bytes>8*1024*1024||request.head_revision<0||request.source_watermark<0||request.max_requests<1||request.max_requests>32||request.deadline_ms<1||request.deadline_ms>120000)throw std::invalid_argument("Invalid bounded input count request");(void)object_json(request.input_json,8*1024*1024);if(context_digest(request.input_json)!=request.count_request_binding||Json::parse(request.binding.provider_identity_json).at("model_id")!=request.model_id)throw ContextBindingChanged("Count model or exact request binding differs");
    auto& db=impl_->database;Transaction tx(db);const auto previous=db.execute("SELECT request_json,state FROM context_measures WHERE id=?",{request.id}).rows;if(!previous.empty()){if(text(previous[0][0])!=measure_request_json(request).dump())throw Conflict("Input count identity changed");tx.commit();return {request,text(previous[0][1])};}refresh_admission(db);validate_measure_owner(*this,db,request);
    if(!db.execute("SELECT id FROM context_measures WHERE owner_id=? AND state IN ('reserved','started')",{request.owner_id}).rows.empty())throw Conflict("Input count owner already has a physical request");const auto count=request.root_run_id?integer(db.execute("SELECT count(*) FROM context_measures WHERE root_run_id=?",{*request.root_run_id}).rows.at(0).at(0)):integer(db.execute("SELECT count(*) FROM context_measures WHERE root_run_id IS NULL AND owner_id=?",{request.owner_id}).rows.at(0).at(0));if(count>=request.max_requests)throw ContextCapacityExceeded("Shared durable input count request allowance exhausted");
    changed_one(db.execute("INSERT INTO context_measures(id,owner_id,root_run_id,scope_kind,scope_id,binding_key,request_json,state,opened_ms) VALUES(?,?,?,?,?,?,?,'reserved',?)",{request.id,request.owner_id,request.root_run_id?SqlValue(*request.root_run_id):SqlValue(nullptr),std::string(context_scope_name(request.scope.kind)),request.scope.id,context_binding_key(request.binding),measure_request_json(request).dump(),now_ms()}));tx.commit();return {request,"reserved"};
}
ContextMeasureReservation Repository::start_context_measure(const std::string& id){auto& db=impl_->database;Transaction tx(db);const auto rows=db.execute("SELECT request_json,state,opened_ms FROM context_measures WHERE id=?",{id}).rows;if(rows.empty())throw NotFound("Input count reservation is absent");const auto request=decode_measure(text(rows[0][0]));if(text(rows[0][1])!="reserved")throw Conflict("Input count was already started or retired");validate_measure_owner(*this,db,request,integer(rows[0][2]));changed_one(db.execute("UPDATE context_measures SET state='started' WHERE id=? AND state='reserved'",{id}));tx.commit();return {request,"started"};}
void Repository::finish_context_measure(const ContextInputMeasure& result) try {
    if(result.semantics!=ContextMeasureSemantics::provider_exact||result.value<0||result.measured_elapsed_ms<0||result.measured_elapsed_ms>3600000)throw std::invalid_argument("Registered Responses count requires an actual exact result");const auto count=parse_responses_input_tokens_response(result.actual_response_json);if(count.input_tokens!=result.value)throw Conflict("Input count does not match its actual provider response");
    auto& db=impl_->database;Transaction tx(db);const auto rows=db.execute("SELECT request_json,state,result_json FROM context_measures WHERE id=?",{result.id}).rows;if(rows.empty())throw NotFound("Input count reservation is absent");const auto request=decode_measure(text(rows[0][0]));if(request.owner_id!=result.owner_id||request.scope!=result.scope||request.binding!=result.binding||request.head_revision!=result.head_revision||request.source_watermark!=result.source_watermark||request.payload_binding!=result.payload_binding||request.model_id!=result.model_id||request.serialized_bytes!=result.serialized_bytes)throw ContextBindingChanged("Acknowledged count does not match original admitted request");const auto encoded=measure_result_json(result).dump();
    if(text(rows[0][1])=="finished"){if(text(rows[0][2])!=encoded)throw Conflict("Input count result changed after acknowledgement");tx.commit();return;}if(text(rows[0][1])!="started")throw Conflict("Undispatched count cannot acquire an actual result");changed_one(db.execute("UPDATE context_measures SET state='finished',result_json=?,elapsed_ms=? WHERE id=? AND state='started'",{encoded,result.measured_elapsed_ms,result.id}));tx.commit();
}catch(const DatabaseError&){throw ContextOutcomeUnrecorded("Acknowledged input count could not be durably recorded");}
void Repository::retire_context_measure(const std::string& id,ContextFailureCode failure,std::int64_t elapsed) try {if(elapsed<0||elapsed>3600000)throw std::invalid_argument("Invalid count retirement timing");auto& db=impl_->database;Transaction tx(db);const auto rows=db.execute("SELECT state,failure_code FROM context_measures WHERE id=?",{id}).rows;if(rows.empty())throw NotFound("Input count reservation is absent");const auto code=std::string(context_failure(failure)),state=text(rows[0][0]);if(state=="failed"||state=="interrupted"){if(text(rows[0][1])!=code)throw Conflict("Input count failure changed");tx.commit();return;}if(state!="reserved"&&state!="started")throw Conflict("Acknowledged input count cannot be discarded");changed_one(db.execute("UPDATE context_measures SET state=?,failure_code=?,elapsed_ms=? WHERE id=? AND state=?",{code=="interrupted"?std::string("interrupted"):std::string("failed"),code,elapsed,id,state}));tx.commit();}catch(const DatabaseError&){throw ContextOutcomeUnrecorded("Input count retirement could not be durably recorded");}
ContextStepReservation Repository::begin_context_compaction(const ContextCompactionSpec& spec){
    bounded_identity(spec.id);bounded_identity(spec.maintenance_attempt_id);context_hash(spec.input_binding);const auto policy=context_policy_json(spec.policy);(void)object_json(spec.input_json,spec.policy.max_request_bytes);if(context_digest(spec.input_json)!=spec.input_binding)throw std::invalid_argument("Maintenance input digest differs from exact POST bytes");
    auto& db=impl_->database;Transaction tx(db);const auto current=read_context_snapshot(db,spec.snapshot.head.scope,spec.snapshot.head.binding,{spec.policy.max_groups,1024*1024,spec.policy.max_request_bytes});
    if(context_snapshot_json(current)!=context_snapshot_json(spec.snapshot))throw ContextSourceChanged("Maintenance source or head changed");const auto selection=select_context(current,spec.policy);if(!selection||selection->covered_through_ordinal!=spec.selection.covered_through_ordinal||selection->protected_from_ordinal!=spec.selection.protected_from_ordinal||selection->source_manifest_binding!=spec.selection.source_manifest_binding||selection->protected_tail_binding!=spec.selection.protected_tail_binding||selection->protected_user_ordinals!=spec.selection.protected_user_ordinals||selection->protected_user_binding!=spec.selection.protected_user_binding)throw ContextSourceChanged("Maintenance complete-group selection differs");
    const auto prior=db.execute("SELECT id FROM context_compactions WHERE id=?",{spec.id}).rows;if(!prior.empty()){const auto saved=read_compaction(db,spec.id);if(saved.attempt!=spec.maintenance_attempt_id||saved.input!=spec.input_json||saved.policy!=policy.dump()||saved.snapshot!=context_snapshot_json(current).dump()||context_admission_json(saved.admission)!=context_admission_json(spec.admission)||saved.manual!=spec.manual_request_id)throw Conflict("Maintenance identity changed");tx.commit();return compaction_reservation(db,saved);}
    if(!db.execute("SELECT id FROM context_compactions WHERE scope_kind=? AND scope_id=? AND state IN ('reserved','started','received')",{std::string(context_scope_name(current.head.scope.kind)),current.head.scope.id}).rows.empty())throw Conflict("Context already has an active maintenance owner");
    refresh_admission(db);
    std::optional<std::string> root,owner,idle,step,followup;
    std::visit([&](const auto& a){using T=std::decay_t<decltype(a)>;
        if constexpr(std::is_same_v<T,IdleContextAdmission>){
            bounded_identity(a.owner_id);if(current.head.scope.kind!=ContextScopeKind::session||!spec.manual_request_id||a.max_model_calls<1||a.max_model_calls>spec.policy.max_maintenance_calls||a.wall_limit_ms<1||a.wall_limit_ms>spec.policy.maintenance_deadline_ms)throw std::invalid_argument("Idle maintenance needs exact authenticated request and policy");
            const auto existing=db.execute("SELECT id FROM context_idle_owners WHERE id=?",{a.owner_id}).rows;
            if(existing.empty()){if(!db.execute("SELECT id FROM runs WHERE id=?",{a.owner_id}).rows.empty())throw Conflict("Idle owner cannot reuse a run identity");changed_one(db.execute("INSERT INTO context_idle_owners(id,session_id,manual_request_id,binding_json,head_revision,state,max_calls,calls_reserved,wall_limit_ms,opened_ms) VALUES(?,?,?,?,?,'active',?,0,?,?)",{a.owner_id,current.head.scope.id,*spec.manual_request_id,context_binding_json(current.head.binding).dump(),current.head.revision,a.max_model_calls,a.wall_limit_ms,now_ms()}));changed_one(db.execute("UPDATE context_manual_requests SET owner_id=? WHERE id=? AND state='pending' AND owner_id IS NULL AND compaction_id IS NULL",{a.owner_id,*spec.manual_request_id}));}
            const auto lease=idle_context_owner(a.owner_id);if(lease.state!="active"||lease.spec.manual_request_id!=*spec.manual_request_id||lease.spec.scope!=current.head.scope||lease.spec.binding!=current.head.binding||lease.spec.expected_head_revision!=current.head.revision||lease.spec.max_model_calls!=a.max_model_calls||lease.spec.wall_limit_ms!=a.wall_limit_ms||lease.remaining_active_ms<1)throw ContextBindingChanged("Compaction must adopt its exact original idle lease");if(lease.model_calls_reserved>=lease.spec.max_model_calls)throw RootBudgetExhausted("Idle maintenance allowance exhausted");changed_one(db.execute("UPDATE context_idle_owners SET calls_reserved=calls_reserved+1 WHERE id=? AND state='active' AND calls_reserved=?",{a.owner_id,lease.model_calls_reserved}));idle=a.owner_id;
        }else{
            bounded_identity(a.inference_step_id);root=a.root_run_id;owner=a.owner_run_id;step=a.inference_step_id;
            const auto role=[&]{if constexpr(std::is_same_v<T,PlanningContextAdmission>)return ModelCallRole::parent;else return a.role;}();
            context_owner(*this,*root,*owner,role,current.head.scope,current.head.binding);const auto budget=root_budget(*root);if(budget.revision!=a.expected_budget_revision)throw ContextBudgetChanged("Context budget admission changed");
            const auto maintenance_count=integer(db.execute("SELECT count(*) FROM agent_model_call_reservations WHERE root_run_id=? AND purpose='context_compaction'",{*root}).rows.at(0).at(0));if(maintenance_count>=spec.policy.max_maintenance_calls)throw RootBudgetExhausted("Shared root context maintenance allowance exhausted");
            InferenceStepSpec pending;pending.id=*step;pending.root_run_id=*root;pending.owner_run_id=*owner;pending.role=role;pending.scope=current.head.scope;pending.binding=current.head.binding;
            if constexpr(std::is_same_v<T,PlanningContextAdmission>){
                followup=a.held_attempt_id;pending.attempt_id=*followup;pending.planning_call_id=a.plan_call_id;const auto call=dynamic_plan_call(a.plan_call_id);if(call.root_run_id!=*root||call.state!="turn_committed"||call.continuation_attempt_id!=*followup||*owner!=*root)throw Conflict("Maintenance must preserve original signed held continuation");const auto actual=read_context_attempt(db,*root,*followup);if(actual.owner_run_id!=*owner||actual.role!=ModelCallRole::parent||actual.state!="reserved"||actual.purpose!=ModelCallPurpose::inference)throw Conflict("Held continuation is no longer reserved");
                const auto existing=db.execute("SELECT spec_json FROM inference_steps WHERE id=?",{*step}).rows;if(existing.empty()){if(!actual.inference_step_id.empty())throw Conflict("Held step membership changed");insert_pending_inference(db,pending,true);}else if(text(existing[0][0])!=inference_identity(pending).dump()||actual.inference_step_id!=*step)throw Conflict("Held step identity changed");context_charge(db,budget,1);
            }else if constexpr(std::is_same_v<T,RebuildContextAdmission>){
                if(!spec.policy.automatic)throw ContextUnavailable("Automatic rebuild is disabled");followup=a.retry_attempt_id;bounded_identity(*followup);const auto original=inference_step(*step);if(original.root_run_id!=*root||original.owner_run_id!=*owner||original.original_attempt_id!=a.failed_attempt_id||original.scope!=current.head.scope||original.binding!=current.head.binding||original.state!="context_overflow"||original.rebuild_count!=0||original.successful_attempt_id||original.planning_call_id!=a.planning_call_id)throw Conflict("Rebuild is not bound to original proved overflow");
                const auto rejected=read_context_attempt(db,*root,a.failed_attempt_id);if(rejected.state!="finished"||rejected.inference_step_id!=*step||!db.execute("SELECT event_seq FROM inference_output_evidence WHERE root_run_id=? AND attempt_id=?",{*root,a.failed_attempt_id}).rows.empty())throw Conflict("Rejected inference has output or unsettled ownership");
                context_charge(db,budget,2);changed_one(db.execute("UPDATE inference_steps SET rebuild_count=1,state='reserved' WHERE id=? AND state='context_overflow' AND rebuild_count=0",{*step}));changed_one(db.execute("INSERT INTO agent_model_call_reservations(root_run_id,attempt_id,owner_run_id,role,state,purpose,inference_step_id) VALUES(?,?,?,?,'reserved','inference',?)",{*root,*followup,*owner,std::string(call_role(role)),*step}));changed_one(db.execute("INSERT INTO inference_attempt_members(step_id,root_run_id,attempt_id,ordinal,binding_json,input_binding) VALUES(?,?,?,1,'null','')",{*step,*root,*followup}));
            }else{
                followup=a.inference_attempt_id;bounded_identity(*followup);pending.attempt_id=*followup;if(!db.execute("SELECT id FROM inference_steps WHERE id=?",{*step}).rows.empty())throw Conflict("New active inference step already exists");
                const auto signed_holds=integer(db.execute("SELECT count(*) FROM dynamic_plan_calls WHERE root_run_id=? AND state IN ('accepted','report_ready','turn_committed') AND continuation_attempt_id IS NULL",{*root}).rows.at(0).at(0));if(role==ModelCallRole::parent&&signed_holds)throw Conflict("Maintenance cannot replace signed planning continuation");const auto held=role==ModelCallRole::parent&&budget.parent_calls_held>signed_holds?1:0;
                context_logical_admission(*this,db,*root,*owner,role,held);context_charge(db,budget,2,held);insert_pending_inference(db,pending);
            }
            changed_one(db.execute("INSERT INTO agent_model_call_reservations(root_run_id,attempt_id,owner_run_id,role,state,purpose) VALUES(?,?,?,?,'reserved','context_compaction')",{*root,spec.maintenance_attempt_id,*owner,std::string(call_role(role))}));
            impl_->event(*owner,"budget.model_call.reserved",Json{{"root_run_id",*root},{"attempt_id",spec.maintenance_attempt_id},{"role",call_role(role)},{"purpose","context_compaction"}}.dump());
            if constexpr(!std::is_same_v<T,PlanningContextAdmission>)impl_->event(*owner,"budget.model_call.reserved",Json{{"root_run_id",*root},{"attempt_id",*followup},{"role",call_role(role)}}.dump());
        }
    },spec.admission);
    if(spec.manual_request_id){const auto r=db.execute("SELECT actor,state,binding_json,expected_head_revision FROM context_manual_requests WHERE id=? AND scope_kind=? AND scope_id=?",{*spec.manual_request_id,std::string(context_scope_name(current.head.scope.kind)),current.head.scope.id}).rows;if(r.empty()||text(r[0][1])!="pending"||text(r[0][2])!=context_binding_json(current.head.binding).dump()||integer(r[0][3])!=current.head.revision)throw ContextBindingChanged("Authenticated manual request no longer matches this head");}
    changed_one(db.execute("INSERT INTO context_compactions(id,maintenance_attempt_id,scope_kind,scope_id,binding_key,binding_json,state,head_revision,source_watermark,covered_ordinal,protected_from,source_binding,tail_binding,user_binding,protected_users_json,input_json,input_binding,snapshot_json,policy_json,admission_json,root_run_id,owner_run_id,idle_owner_id,inference_step_id,inference_attempt_id,manual_request_id,opened_ms) VALUES(?,?,?,?,?,?,'reserved',?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",{spec.id,spec.maintenance_attempt_id,std::string(context_scope_name(current.head.scope.kind)),current.head.scope.id,context_binding_key(current.head.binding),context_binding_json(current.head.binding).dump(),current.head.revision,current.source_watermark,selection->covered_through_ordinal,selection->protected_from_ordinal,selection->source_manifest_binding,selection->protected_tail_binding,selection->protected_user_binding,Json(selection->protected_user_ordinals).dump(),spec.input_json,spec.input_binding,context_snapshot_json(current).dump(),policy.dump(),context_admission_json(spec.admission).dump(),root?SqlValue(*root):SqlValue(nullptr),owner?SqlValue(*owner):SqlValue(nullptr),idle?SqlValue(*idle):SqlValue(nullptr),step?SqlValue(*step):SqlValue(nullptr),followup?SqlValue(*followup):SqlValue(nullptr),spec.manual_request_id?SqlValue(*spec.manual_request_id):SqlValue(nullptr),now_ms()}));
    if(spec.manual_request_id)changed_one(db.execute("UPDATE context_manual_requests SET state='claimed',owner_id=?,compaction_id=? WHERE id=? AND state='pending'",{owner?*owner:*idle,spec.id,*spec.manual_request_id}));const auto saved=read_compaction(db,spec.id);context_journal(db,saved,"context.compaction.reserved",Json{{"compaction_id",spec.id},{"maintenance_attempt_id",spec.maintenance_attempt_id}});const auto result=compaction_reservation(db,saved);tx.commit();return result;
}
ContextStepReservation Repository::start_context_compaction(const std::string& id,const std::string& attempt){
    auto& db=impl_->database;Transaction tx(db);auto s=read_compaction(db,id);if(s.attempt!=attempt||s.state!="reserved")throw Conflict("Maintenance is already dispatched or retired");validate_compaction_source(db,s);validate_compaction_owner(*this,db,s);
    if(!std::holds_alternative<IdleContextAdmission>(s.admission)){const auto reservation=compaction_reservation(db,s);const auto& inference=*reservation.inference;changed_one(db.execute("UPDATE agent_model_call_reservations SET state='started' WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND state='reserved' AND purpose='context_compaction'",{inference.root_run_id,attempt,inference.owner_run_id}));impl_->event(inference.owner_run_id,"budget.model_call.started",Json{{"root_run_id",inference.root_run_id},{"attempt_id",attempt},{"role",call_role(inference.role)},{"purpose","context_compaction"}}.dump());}
    changed_one(db.execute("UPDATE context_compactions SET state='started' WHERE id=? AND state='reserved'",{id}));s.state="started";context_journal(db,s,"context.compaction.started",Json{{"compaction_id",id},{"maintenance_attempt_id",attempt}});tx.commit();return compaction_reservation(db,s);
}
ContextProjection Repository::record_context_compaction_response(const ContextCompactionCommit& result) try {
    auto& db=impl_->database;Transaction tx(db);auto s=read_compaction(db,result.compaction_id);if(s.attempt!=result.maintenance_attempt_id||s.binding!=result.binding||s.head!=result.expected_head_revision||s.watermark!=result.source_watermark||s.source!=result.source_manifest_binding||s.tail!=result.protected_tail_binding||result.kind!=ContextProjectionKind::responses_canonical||result.visible_summary||result.before_measure_id||result.after_measure_id||!result.prospective_input_binding.empty()||result.measured_preparation_elapsed_ms||result.measured_elapsed_ms<0||result.measured_elapsed_ms>3600000)throw Conflict("Observed maintenance identity changed");
    const auto actual=parse_responses_compaction_response(result.actual_response_json);validate_responses_compaction_result(s.input,actual);if(actual.window.items_json()!=result.projection_json||actual.usage_json!=result.actual_usage_json||result.provider_response_id!=std::optional<std::string>(actual.response_id)||result.provider_created_at!=actual.created_at)throw Conflict("Observed maintenance result does not match the actual provider envelope");
    if(s.state=="received"||s.state=="committed"){const auto stored=read_observed_projection(db,s);if(stored.actual_response_json!=result.actual_response_json||stored.projection_json!=result.projection_json||stored.measured_elapsed_ms!=result.measured_elapsed_ms)throw Conflict("Observed maintenance receipt changed");tx.commit();return stored;}
    if(s.state!="started")throw Conflict("Undispatched or interrupted maintenance cannot acquire a receipt");
    // Preserve the actual acknowledgement before any prospective count or
    // head publication. A later source/capacity rejection retains this receipt.
    changed_one(db.execute("INSERT INTO context_checkpoints(id,compaction_id,projection_json,actual_response_json,actual_usage_json,provider_response_id,provider_created_at,visible_summary,measured_elapsed_ms,kind) VALUES(?,?,?,?,?,?,?,NULL,?,'responses_canonical')",{s.id,s.id,result.projection_json,result.actual_response_json,result.actual_usage_json,result.provider_response_id?SqlValue(*result.provider_response_id):SqlValue(nullptr),result.provider_created_at?SqlValue(*result.provider_created_at):SqlValue(nullptr),result.measured_elapsed_ms}));
    changed_one(db.execute("UPDATE context_compactions SET state='received',elapsed_ms=? WHERE id=? AND state='started'",{result.measured_elapsed_ms,s.id}));s.state="received";
    if(!std::holds_alternative<IdleContextAdmission>(s.admission)){const auto r=compaction_reservation(db,s);const auto& inference=*r.inference;changed_one(db.execute("UPDATE agent_model_call_reservations SET state='finished' WHERE root_run_id=? AND attempt_id=? AND state='started' AND purpose='context_compaction'",{inference.root_run_id,s.attempt}));impl_->event(inference.owner_run_id,"budget.model_call.finished",Json{{"root_run_id",inference.root_run_id},{"attempt_id",s.attempt},{"role",call_role(inference.role)},{"purpose","context_compaction"},{"was_started",true}}.dump());}
    context_journal(db,s,"context.compaction.received",Json{{"compaction_id",s.id},{"maintenance_attempt_id",s.attempt},{"elapsed_ms",result.measured_elapsed_ms}});const auto out=read_observed_projection(db,s);tx.commit();return out;
}catch(const DatabaseError&){throw ContextOutcomeUnrecorded("Acknowledged maintenance receipt could not be durably recorded");}
ContextProjection Repository::commit_context_compaction(const ContextCompactionCommit& result) try {
    auto& db=impl_->database;Transaction tx(db);auto s=read_compaction(db,result.compaction_id);if(s.state!="received"&&s.state!="committed")throw Conflict("Checkpoint publication requires its observed actual receipt");const auto observed=read_observed_projection(db,s);
    if(s.attempt!=result.maintenance_attempt_id||s.binding!=result.binding||s.head!=result.expected_head_revision||s.watermark!=result.source_watermark||s.source!=result.source_manifest_binding||s.tail!=result.protected_tail_binding||result.kind!=observed.kind||result.actual_response_json!=observed.actual_response_json||result.projection_json!=observed.projection_json||result.actual_usage_json!=observed.actual_usage_json||result.provider_response_id!=observed.provider_response_id||result.provider_created_at!=observed.provider_created_at||result.measured_elapsed_ms!=observed.measured_elapsed_ms||result.visible_summary!=observed.visible_summary||!result.before_measure_id||!result.after_measure_id||*result.before_measure_id==*result.after_measure_id)throw Conflict("Checkpoint differs from immutable observed maintenance receipt");context_hash(result.prospective_input_binding);
    const auto policy=Json::parse(s.policy);if(result.projection_json.size()>policy.at("max_checkpoint_bytes").get<std::size_t>())throw ContextCapacityExceeded("Observed checkpoint exceeds registered publication limit");if(result.measured_preparation_elapsed_ms&&(*result.measured_preparation_elapsed_ms<result.measured_elapsed_ms||*result.measured_preparation_elapsed_ms>3600000))throw std::invalid_argument("Invalid preparation observation");
    auto count=[&](const std::string& id){const auto r=db.execute("SELECT request_json,result_json,state FROM context_measures WHERE id=?",{id}).rows;if(r.empty()||text(r[0][2])!="finished"||std::holds_alternative<std::nullptr_t>(r[0][1]))throw Conflict("Checkpoint requires completed actual input counts");const auto request=decode_measure(text(r[0][0]));const auto value=Json::parse(text(r[0][1]));if(request.owner_id!=compaction_owner(s)||request.scope!=s.scope||request.binding!=s.binding||request.head_revision!=s.head||request.source_watermark!=s.watermark||value["semantics"]!="provider_exact")throw ContextBindingChanged("Count receipt belongs to another context or owner");return std::pair{request,value};};
    const auto before=count(*result.before_measure_id),after=count(*result.after_measure_id);if(before.first.model_id!=after.first.model_id||after.first.compaction_id!=s.id||(before.first.compaction_id&&before.first.compaction_id!=s.id)||after.first.payload_binding!=result.prospective_input_binding||after.second["value"]>=before.second["value"]||after.first.serialized_bytes>policy.at("max_request_bytes").get<std::int64_t>())throw ContextCapacityExceeded("Actual compatible counts do not prove bounded context reduction");
    if(s.state=="committed"){const auto stored=db.execute("SELECT before_measure_id,after_measure_id,prospective_input_binding,preparation_elapsed_ms FROM context_compactions WHERE id=?",{s.id}).rows.at(0);if(text(stored[0])!=*result.before_measure_id||text(stored[1])!=*result.after_measure_id||text(stored[2])!=result.prospective_input_binding||(result.measured_preparation_elapsed_ms?std::holds_alternative<std::nullptr_t>(stored[3])||integer(stored[3])!=*result.measured_preparation_elapsed_ms:!std::holds_alternative<std::nullptr_t>(stored[3])))throw Conflict("Checkpoint publication binding changed");tx.commit();return observed;}
    validate_compaction_source(db,s);validate_compaction_owner(*this,db,s);if(s.head>=9007199254740991)throw ContextCapacityExceeded("Context head revision exhausted");
    changed_one(db.execute("UPDATE context_compactions SET state='committed',preparation_elapsed_ms=?,before_measure_id=?,after_measure_id=?,prospective_input_binding=? WHERE id=? AND state='received'",{result.measured_preparation_elapsed_ms?SqlValue(*result.measured_preparation_elapsed_ms):SqlValue(nullptr),*result.before_measure_id,*result.after_measure_id,result.prospective_input_binding,s.id}));
    changed_one(db.execute("UPDATE context_heads SET revision=revision+1,covered_ordinal=?,checkpoint_id=? WHERE scope_kind=? AND scope_id=? AND binding_key=? AND revision=?",{s.covered,s.id,std::string(context_scope_name(s.scope.kind)),s.scope.id,context_binding_key(s.binding),s.head}));s.state="committed";
    if(s.manual)changed_one(db.execute("UPDATE context_manual_requests SET state='completed' WHERE id=? AND compaction_id=? AND state='claimed'",{*s.manual,s.id}));if(const auto* idle=std::get_if<IdleContextAdmission>(&s.admission)){const auto lease=idle_context_owner(idle->owner_id);changed_one(db.execute("UPDATE context_idle_owners SET state='completed',elapsed_ms=? WHERE id=? AND state='active'",{std::max<std::int64_t>(0,now_ms()-lease.opened_unix_ms),idle->owner_id}));}
    context_journal(db,s,"context.compaction.committed",Json{{"compaction_id",s.id},{"head_revision",s.head+1}});auto out=read_observed_projection(db,s);out.measured_preparation_elapsed_ms=result.measured_preparation_elapsed_ms;tx.commit();return out;
}catch(const DatabaseError&){throw ContextOutcomeUnrecorded("Observed checkpoint publication could not be durably recorded");}
void Repository::retire_context_compaction(const ContextCompactionFailure& failure) try {
    if(failure.measured_elapsed_ms<0||failure.measured_elapsed_ms>3600000)throw std::invalid_argument("Invalid maintenance retirement timing");auto& db=impl_->database;Transaction tx(db);auto s=read_compaction(db,failure.compaction_id);if(s.attempt!=failure.maintenance_attempt_id)throw Conflict("Maintenance retirement identity differs");const auto code=std::string(context_failure(failure.code));if(s.state=="failed"||s.state=="interrupted"){const auto row=db.execute("SELECT failure_code FROM context_compactions WHERE id=?",{s.id}).rows;if(text(row[0][0])!=code)throw Conflict("Retired maintenance outcome differs");tx.commit();return;}if(s.state=="committed")throw Conflict("Installed checkpoint cannot be retired");
    const auto old=s.state;changed_one(db.execute("UPDATE context_compactions SET state=?,failure_code=?,elapsed_ms=COALESCE(elapsed_ms,?) WHERE id=? AND state=?",{code=="interrupted"?std::string("interrupted"):std::string("failed"),code,failure.measured_elapsed_ms,s.id,old}));
    if(const auto* idle=std::get_if<IdleContextAdmission>(&s.admission)){const auto lease=idle_context_owner(idle->owner_id);changed_one(db.execute("UPDATE context_idle_owners SET state=?,failure_code=?,elapsed_ms=? WHERE id=? AND state='active'",{code=="interrupted"?std::string("interrupted"):std::string("failed"),code,std::max<std::int64_t>(0,now_ms()-lease.opened_unix_ms),idle->owner_id}));}
    else {const auto r=compaction_reservation(db,s);const auto& inference=*r.inference;for(const auto& id:{s.attempt,inference.attempt_id}){const auto a=read_context_attempt(db,inference.root_run_id,id);if(a.state=="reserved"||a.state=="started"){if(id==inference.attempt_id&&a.state!="reserved")throw Conflict("Maintenance cannot retire dispatched inference");changed_one(db.execute("UPDATE agent_model_call_reservations SET state='finished' WHERE root_run_id=? AND attempt_id=? AND state=?",{a.root_run_id,id,a.state}));impl_->event(a.owner_run_id,"budget.model_call.finished",Json{{"root_run_id",a.root_run_id},{"attempt_id",id},{"role",call_role(a.role)},{"purpose",context_purpose(a.purpose)},{"was_started",a.state=="started"}}.dump());if(a.purpose==ModelCallPurpose::inference){changed_one(db.execute("UPDATE inference_attempt_members SET outcome='failed',failure_code=?,elapsed_ms=0 WHERE step_id=? AND attempt_id=? AND outcome IS NULL",{code,a.inference_step_id,id}));changed_one(db.execute("UPDATE inference_steps SET state='failed' WHERE id=? AND successful_attempt_id IS NULL",{a.inference_step_id}));}}}}
    if(s.manual)changed_one(db.execute("UPDATE context_manual_requests SET state=?,failure_code=? WHERE id=? AND compaction_id=? AND state='claimed'",{code=="interrupted"?std::string("interrupted"):std::string("failed"),code,*s.manual,s.id}));context_journal(db,s,"context.compaction.failed",Json{{"compaction_id",s.id},{"code",code},{"receipt_retained",old=="received"}});tx.commit();
}catch(const DatabaseError&){throw ContextOutcomeUnrecorded("Maintenance retirement could not be durably recorded");}
InferenceStepRecord Repository::inference_step(const std::string& id){
    const auto rows=impl_->database.execute("SELECT root_run_id,owner_run_id,original_attempt_id,state,planning_call_id,successful_attempt_id,rebuild_count,spec_json FROM inference_steps WHERE id=?",{id}).rows;if(rows.empty())throw NotFound("Logical inference step is absent");const auto& r=rows[0];InferenceStepRecord out{id,text(r[0]),text(r[1]),text(r[2]),text(r[3]),{},{},integer(r[6]),{}};const auto identity=Json::parse(text(r[7]));out.scope={identity["scope_kind"]=="session"?ContextScopeKind::session:ContextScopeKind::execution,identity["scope_id"].get<std::string>()};out.binding=decode_context_binding(identity["binding"].dump());if(!std::holds_alternative<std::nullptr_t>(r[4]))out.planning_call_id=text(r[4]);if(!std::holds_alternative<std::nullptr_t>(r[5]))out.successful_attempt_id=text(r[5]);for(const auto& m:impl_->database.execute("SELECT attempt_id FROM inference_attempt_members WHERE step_id=? ORDER BY ordinal",{id}).rows)out.physical_attempts.push_back(read_context_attempt(impl_->database,out.root_run_id,text(m[0])));return out;
}
InferenceStepRecord Repository::reserve_inference_step(const InferenceStepSpec& s){
    bounded_identity(s.id);bounded_identity(s.attempt_id);context_hash(s.input_binding);if(s.expected_budget_revision<1||s.head_revision<0||s.source_watermark<0)throw std::invalid_argument("Invalid inference binding admission");
    auto& db=impl_->database;Transaction tx(db);context_owner(*this,s.root_run_id,s.owner_run_id,s.role,s.scope,s.binding);const auto snap=read_context_snapshot(db,s.scope,s.binding,{});if(snap.head.revision!=s.head_revision||snap.source_watermark!=s.source_watermark)throw ContextUnavailable("Inference projection source changed");
    const auto existing=db.execute("SELECT spec_json,state FROM inference_steps WHERE id=?",{s.id}).rows;
    if(existing.empty()){
        const auto b=root_budget(s.root_run_id);if(b.revision!=s.expected_budget_revision)throw ContextBudgetChanged("Inference budget changed");
        const auto prior=db.execute("SELECT state,owner_run_id,role,purpose,inference_step_id FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{s.root_run_id,s.attempt_id}).rows;
        if(!prior.empty()){
            if(!s.planning_call_id||text(prior[0][0])!="reserved"||text(prior[0][1])!=s.owner_run_id||text(prior[0][2])!=call_role(s.role)||text(prior[0][3])!="inference"||!std::holds_alternative<std::nullptr_t>(prior[0][4]))throw Conflict("Only an exact reserved original held attempt can acquire a step");
            const auto call=dynamic_plan_call(*s.planning_call_id);if(call.root_run_id!=s.root_run_id||call.state!="turn_committed"||call.continuation_attempt_id!=s.attempt_id||s.owner_run_id!=s.root_run_id||s.role!=ModelCallRole::parent)throw Conflict("Inference does not match its signed held continuation");insert_pending_inference(db,s,true);
        }else{
            if(s.planning_call_id)throw Conflict("Signed continuation must use its original funded attempt");
            const auto signed_holds=integer(db.execute("SELECT count(*) FROM dynamic_plan_calls WHERE root_run_id=? AND state IN ('accepted','report_ready','turn_committed') AND continuation_attempt_id IS NULL",{s.root_run_id}).rows.at(0).at(0));
            if(s.role==ModelCallRole::parent&&signed_holds)throw Conflict("Parent must preserve its exact held planning call");const auto held=s.role==ModelCallRole::parent&&b.parent_calls_held>signed_holds?1:0;
            context_logical_admission(*this,db,s.root_run_id,s.owner_run_id,s.role,held);context_charge(db,b,1,held);insert_pending_inference(db,s);impl_->event(s.owner_run_id,"budget.model_call.reserved",Json{{"root_run_id",s.root_run_id},{"attempt_id",s.attempt_id},{"role",call_role(s.role)}}.dump());
        }
    }else if(text(existing[0][0])!=inference_identity(s).dump())throw Conflict("Logical inference identity changed");
    auto out=inference_step(s.id);if(out.original_attempt_id!=s.attempt_id||out.state!="reserved")throw Conflict("Inference step was dispatched or retired");
    const auto member=db.execute("SELECT binding_json,input_binding,head_revision,source_watermark FROM inference_attempt_members WHERE step_id=? AND attempt_id=? AND ordinal=0",{s.id,s.attempt_id}).rows;if(member.empty())throw DatabaseError("Original inference membership is missing");
    if(text(member[0][1]).empty()&&!existing.empty()&&root_budget(s.root_run_id).revision!=s.expected_budget_revision)throw ContextBudgetChanged("Funded inference binding budget changed");if(text(member[0][1]).empty())changed_one(db.execute("UPDATE inference_attempt_members SET binding_json=?,input_binding=?,head_revision=?,source_watermark=? WHERE step_id=? AND ordinal=0 AND input_binding=''",{context_binding_json(s.binding).dump(),s.input_binding,s.head_revision,s.source_watermark,s.id}));
    else if(text(member[0][0])!=context_binding_json(s.binding).dump()||text(member[0][1])!=s.input_binding||integer(member[0][2])!=s.head_revision||integer(member[0][3])!=s.source_watermark)throw Conflict("Finalized inference projection changed");
    tx.commit();return out;
}
InferenceStepRecord Repository::finish_inference_attempt(const InferenceAttemptResult& result) try {
    if(result.measured_elapsed_ms<0||result.measured_elapsed_ms>3600000)throw std::invalid_argument("Invalid inference timing");if(result.actual_assistant_json)(void)object_json(*result.actual_assistant_json,8*1024*1024);
    if(result.outcome==InferenceAttemptOutcome::context_overflow?result.fixed_provider_error_code!="context_length_exceeded":!result.fixed_provider_error_code.empty())throw std::invalid_argument("Inference failure code is not registered");
    const auto outcome=result.outcome==InferenceAttemptOutcome::completed?"completed":result.outcome==InferenceAttemptOutcome::context_overflow?"context_overflow":result.outcome==InferenceAttemptOutcome::cancelled?"cancelled":"failed";
    auto& db=impl_->database;Transaction tx(db);auto step=inference_step(result.step_id);const auto rows=db.execute("SELECT outcome,failure_code,elapsed_ms FROM inference_attempt_members WHERE step_id=? AND attempt_id=?",{step.id,result.attempt_id}).rows;if(rows.empty())throw NotFound("Physical inference does not belong to this step");const auto attempt=read_context_attempt(db,step.root_run_id,result.attempt_id);
    const auto actual=db.execute("SELECT actual_assistant_json FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{step.root_run_id,result.attempt_id}).rows.at(0).at(0);
    if(!std::holds_alternative<std::nullptr_t>(rows[0][0])){if(text(rows[0][0])!=outcome||text(rows[0][1])!=result.fixed_provider_error_code||integer(rows[0][2])!=result.measured_elapsed_ms||(result.actual_assistant_json&&(std::holds_alternative<std::nullptr_t>(actual)||text(actual)!=*result.actual_assistant_json)))throw Conflict("Inference outcome changed after retirement");tx.commit();return step;}
    if(attempt.purpose!=ModelCallPurpose::inference||attempt.inference_step_id!=step.id||(attempt.state!="reserved"&&attempt.state!="started"&&attempt.state!="finished"))throw Conflict("Inference ownership was interrupted");
    if(result.outcome==InferenceAttemptOutcome::completed){if(!result.actual_assistant_json||attempt.state=="reserved"||(attempt.state=="finished"&&std::holds_alternative<std::nullptr_t>(actual))||(!std::holds_alternative<std::nullptr_t>(actual)&&text(actual)!=*result.actual_assistant_json))throw Conflict("Completed inference requires its exact actual response");}
    else if(result.actual_assistant_json||!std::holds_alternative<std::nullptr_t>(actual))throw Conflict("Noncompleted inference cannot discard an actual response");
    if(result.outcome==InferenceAttemptOutcome::context_overflow){const auto started=db.execute("SELECT seq FROM events WHERE run_id=? AND kind='budget.model_call.started' AND json_extract(payload,'$.root_run_id')=? AND json_extract(payload,'$.attempt_id')=?",{step.owner_run_id,step.root_run_id,result.attempt_id}).rows;if(result.fixed_provider_error_code!="context_length_exceeded"||attempt.state=="reserved"||started.size()!=1||!db.execute("SELECT event_seq FROM inference_output_evidence WHERE root_run_id=? AND attempt_id=?",{step.root_run_id,result.attempt_id}).rows.empty()||!db.execute("SELECT e.seq FROM events e WHERE e.run_id=? AND e.kind IN ('tool.started','operation.executing','operation.succeeded','operation.uncertain') AND e.seq>?",{step.owner_run_id,integer(started[0][0])}).rows.empty())throw Conflict("Context overflow requires proved pre-output no-effect rejection");}
    if(attempt.state!="finished"){
        if(result.actual_assistant_json)changed_one(db.execute("UPDATE agent_model_call_reservations SET state='finished',actual_assistant_json=? WHERE root_run_id=? AND attempt_id=? AND state='started'",{*result.actual_assistant_json,step.root_run_id,result.attempt_id}));
        else changed_one(db.execute("UPDATE agent_model_call_reservations SET state='finished' WHERE root_run_id=? AND attempt_id=? AND state=?",{step.root_run_id,result.attempt_id,attempt.state}));
        impl_->event(step.owner_run_id,"budget.model_call.finished",Json{{"root_run_id",step.root_run_id},{"attempt_id",result.attempt_id},{"role",call_role(attempt.role)},{"was_started",attempt.state=="started"}}.dump());
    }
    changed_one(db.execute("UPDATE inference_attempt_members SET outcome=?,failure_code=?,elapsed_ms=? WHERE step_id=? AND attempt_id=? AND outcome IS NULL",{std::string(outcome),result.fixed_provider_error_code,result.measured_elapsed_ms,step.id,result.attempt_id}));
    if(result.outcome==InferenceAttemptOutcome::completed)changed_one(db.execute("UPDATE inference_steps SET state='completed',successful_attempt_id=? WHERE id=? AND successful_attempt_id IS NULL",{result.attempt_id,step.id}));
    else changed_one(db.execute("UPDATE inference_steps SET state=? WHERE id=? AND successful_attempt_id IS NULL",{std::string(result.outcome==InferenceAttemptOutcome::context_overflow?"context_overflow":outcome),step.id}));
    tx.commit();return inference_step(step.id);
}catch(const DatabaseError&){throw ContextOutcomeUnrecorded("Actual inference outcome could not be durably recorded");}
InferenceStepRecord Repository::reserve_inference_rebuild(const InferenceRebuildSpec& s){
    context_hash(s.input_binding);auto& db=impl_->database;Transaction tx(db);auto step=inference_step(s.step_id);const auto c=read_compaction(db,s.compaction_id);const auto* a=std::get_if<RebuildContextAdmission>(&c.admission);
    if(!a||c.state!="committed"||a->inference_step_id!=s.step_id||a->failed_attempt_id!=s.failed_attempt_id||a->retry_attempt_id!=s.retry_attempt_id||step.rebuild_count!=1||step.original_attempt_id!=s.failed_attempt_id||step.successful_attempt_id||c.binding!=s.binding)throw Conflict("Rebuild must use its exact funded original logical group");
    context_owner(*this,step.root_run_id,step.owner_run_id,a->role,c.scope,s.binding);const auto snapshot=read_context_snapshot(db,c.scope,s.binding,{});if(snapshot.head.revision!=s.head_revision||snapshot.source_watermark!=s.source_watermark||snapshot.head.checkpoint_id!=s.compaction_id)throw ContextSourceChanged("Rebuild projection changed");if(root_budget(step.root_run_id).revision!=s.expected_budget_revision)throw ContextBudgetChanged("Rebuild budget changed");
    const auto row=db.execute("SELECT input_binding,binding_json,head_revision,source_watermark FROM inference_attempt_members WHERE step_id=? AND ordinal=1 AND attempt_id=?",{step.id,s.retry_attempt_id}).rows;if(row.empty())throw DatabaseError("Funded retry physical member is absent");if(text(row[0][0]).empty())changed_one(db.execute("UPDATE inference_attempt_members SET binding_json=?,input_binding=?,head_revision=?,source_watermark=? WHERE step_id=? AND ordinal=1 AND input_binding=''",{context_binding_json(s.binding).dump(),s.input_binding,s.head_revision,s.source_watermark,step.id}));else if(text(row[0][0])!=s.input_binding||text(row[0][1])!=context_binding_json(s.binding).dump()||integer(row[0][2])!=s.head_revision||integer(row[0][3])!=s.source_watermark)throw Conflict("Retry projection has already been finalized differently");
    tx.commit();return inference_step(step.id);
}
RootBudgetRecord Repository::root_budget(const std::string& id){
    const auto current=run(id);if(!current.parent_id.empty())throw std::invalid_argument("Execution budget requires its root owner");
    const auto rows=impl_->database.execute("SELECT policy_id,policy_revision,workspace_identity,provider_identity_json,max_children,max_parallel,max_model_calls,wall_limit_ms,children_admitted,model_calls_reserved,parent_calls_held,revision FROM agent_execution_budgets WHERE root_run_id=?",{id}).rows;
    if(rows.empty())throw NotFound("Execution budget not found");const auto& r=rows[0];
    RootBudgetRecord result{id,{text(r[0]),integer(r[1]),text(r[2]),text(r[3]),integer(r[4]),integer(r[5]),integer(r[6]),integer(r[7])},integer(r[8]),integer(r[9]),integer(r[10]),integer(r[11])};
    result.planned_children_reserved=integer(impl_->database.execute("SELECT planned_children_reserved FROM agent_execution_budgets WHERE root_run_id=?",{id}).rows.at(0).at(0));
    result.parent_model_calls_reserved=integer(impl_->database.execute("SELECT count(DISTINCT COALESCE(inference_step_id,attempt_id)) FROM agent_model_call_reservations WHERE root_run_id=? AND role='parent' AND purpose='inference'",{id}).rows.at(0).at(0));
    if(current.graph_root&&result.spec.policy_id!="native.graph-context")throw Conflict("Graph has no registered context budget");
    (void)budget_provider_identity(result.spec);return result;
}
ModelCallReservation Repository::reserve_model_call(const std::string& root,const std::string& owner,const std::string& attempt,ModelCallRole role,const AgentWorkerLeaseSpec* worker){
    bounded_identity(attempt);const auto role_name=std::string(call_role(role));auto& db=impl_->database;Transaction transaction(db);
    if(worker){worker_current_scope(*this,db,*worker);if(root!=worker->root_run_id||owner!=worker->owner_run_id||role!=(owner==root?ModelCallRole::parent:ModelCallRole::leaf))throw Conflict("Worker funding owner differs from captured admission");}
    const auto budget=root_budget(root);const auto parent=run(root),actor=run(owner);
    if(worker&&!((owner==root&&(budget.spec.policy_id=="native.direct-agent"||budget.spec.policy_id=="native.delegation"))||worker_delegated_owner(*this,*worker)||worker_uncounted_graph_owner(*this,*worker)))throw Conflict("Worker reserve requires ordinary direct-agent admission");
    if(worker)worker_require_tool_turns(db,*worker);
    if(parent.state!=RunState::running || actor.state!=RunState::running)throw Conflict("Model attempt requires its running execution owners");
    if(role==ModelCallRole::parent){if(owner!=root||parent.graph_root)throw Conflict("Parent model attempt owner differs");}
    else {const auto admitted=child_admission(owner);const bool graph=admitted.kind==ChildAdmissionKind::graph_agent&&budget.spec.policy_id=="native.graph-context";if((!graph&&admitted.kind!=ChildAdmissionKind::delegated_leaf&&admitted.kind!=ChildAdmissionKind::dynamic_agent)||admitted.root_run_id!=root)throw Conflict("Leaf model attempt is not owned by this root");
        auto limit=std::int64_t(4);if(graph){if(impl_->graph_lineage(actor.parent_id,true).first!=root)throw Conflict("Graph model attempt ancestry differs from its running root");const auto capture=graph_context_owner(root);if(!capture.segment||capture.segment->state!="open")throw Conflict("Registered graph attempt lacks its open owner clock");limit=graph_context_agent(capture.authority,admitted.graph_node_path).turn_limit;}if(admitted.kind==ChildAdmissionKind::dynamic_agent){const auto p=dynamic_plan(admitted.plan_id);const auto n=std::find_if(p.nodes.begin(),p.nodes.end(),[&](const auto& v){return v.child_run_id==owner;});limit=dynamic_preset(p.capabilities,n->definition).turn_limit;}const auto calls=integer(db.execute("SELECT count(DISTINCT COALESCE(inference_step_id,attempt_id)) FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=? AND purpose='inference'",{root,owner}).rows.at(0).at(0));if(calls>=limit&&db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{root,attempt}).rows.empty())throw RootBudgetExhausted("Durable child-turn allowance exhausted");}
    const auto previous=db.execute("SELECT owner_run_id,role,state,purpose,COALESCE(inference_step_id,'') FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{root,attempt}).rows;
    if(!previous.empty()){if(text(previous[0][0])!=owner||text(previous[0][1])!=role_name||text(previous[0][3])!="inference")throw Conflict("Model attempt identity or purpose changed");if(worker&&(text(previous[0][2])!="reserved"||worker_attempt_generation(db,root,owner,attempt)!=std::optional<std::string>{worker->generation}))throw Conflict("Worker cannot adopt an existing started, retired or foreign-generation attempt");transaction.commit();return {root,attempt,owner,role,text(previous[0][2]),ModelCallPurpose::inference,text(previous[0][4])};}
    const auto dynamic=budget.spec.policy_id=="native.dynamic-plan";
    if(dynamic){const auto caps=dynamic_capabilities(root);if(!caps.catalogue_finalized)throw DynamicPlanUnavailable("Dynamic catalogue is not finalized");
        if(role==ModelCallRole::parent&&!db.execute("SELECT id FROM dynamic_plan_calls WHERE root_run_id=? AND state IN ('accepted','report_ready') OR (root_run_id=? AND state='turn_committed' AND continuation_attempt_id IS NULL)",{root,root}).rows.empty())throw Conflict("Parent must use its exact dynamic continuation");}
    const auto signed_holds=integer(db.execute("SELECT count(*) FROM dynamic_plan_calls WHERE root_run_id=? AND state IN ('accepted','report_ready','turn_committed') AND continuation_attempt_id IS NULL",{root}).rows.at(0).at(0));
    const auto held=role==ModelCallRole::parent&&budget.parent_calls_held>signed_holds?1:0;
    if(role==ModelCallRole::parent&&budget.parent_model_calls_reserved+budget.parent_calls_held-held>=(dynamic?dynamic_capabilities(root).max_parent_turns:32))throw RootBudgetExhausted("Durable parent-turn allowance exhausted");
    if(budget.model_calls_reserved+budget.parent_calls_held-held>=budget.spec.max_model_calls)throw RootBudgetExhausted("Root model-call allowance exhausted");
    if(budget.revision>=9007199254740991)throw std::overflow_error("Root budget revision exhausted");
    changed_one(db.execute("UPDATE agent_execution_budgets SET model_calls_reserved=model_calls_reserved+1,parent_calls_held=parent_calls_held-?,revision=revision+1 WHERE root_run_id=? AND revision=?",{static_cast<std::int64_t>(held),root,budget.revision}));
    changed_one(db.execute("INSERT INTO agent_model_call_reservations(root_run_id,attempt_id,owner_run_id,role,state) VALUES(?,?,?,?,'reserved')",{root,attempt,owner,role_name}));
    Json funding={{"root_run_id",root},{"attempt_id",attempt},{"role",role_name}};if(worker)funding["worker_generation"]=worker->generation;
    impl_->event(owner,"budget.model_call.reserved",funding.dump());transaction.commit();return {root,attempt,owner,role,"reserved"};
}
ModelCallReservation Repository::start_model_call(const std::string& root,const std::string& owner,const std::string& attempt,const AgentWorkerLeaseSpec* worker){
    auto& db=impl_->database;Transaction transaction(db);(void)root_budget(root);const auto parent=run(root),actor=run(owner);
    if(worker){worker_current_scope(*this,db,*worker);if(root!=worker->root_run_id||owner!=worker->owner_run_id||worker_attempt_generation(db,root,owner,attempt)!=std::optional<std::string>{worker->generation})throw Conflict("Worker cannot start an attempt funded by another authority");}
    if(worker&&!((owner==root&&(root_budget(root).spec.policy_id=="native.direct-agent"||root_budget(root).spec.policy_id=="native.delegation"))||worker_delegated_owner(*this,*worker)||worker_uncounted_graph_owner(*this,*worker)||worker_planning_attempt(*this,db,*worker,attempt)))throw Conflict("Worker start requires exact ordinary or planning-continuation admission");
    if(worker)worker_require_tool_turns(db,*worker);
    if(parent.state!=RunState::running||actor.state!=RunState::running)throw Conflict("Model attempt owners are not running");
    const auto rows=db.execute("SELECT role,state,purpose,inference_step_id FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=? AND owner_run_id=?",{root,attempt,owner}).rows;
    if(rows.empty())throw NotFound("Owned model attempt not found");if(text(rows[0][1])!="reserved")throw Conflict("Model attempt has already started or retired");
    const auto role=text(rows[0][0])=="parent"?ModelCallRole::parent:ModelCallRole::leaf;
    if(text(rows[0][2])!="inference")throw Conflict("Maintenance requires its typed start");
    if(!std::holds_alternative<std::nullptr_t>(rows[0][3])&&db.execute("SELECT step_id FROM inference_attempt_members WHERE step_id=? AND attempt_id=? AND input_binding!='' AND binding_json!='null'",{text(rows[0][3]),attempt}).rows.empty())throw Conflict("Funded inference projection is not finalized");
    if(role==ModelCallRole::parent){if(root!=owner||parent.graph_root)throw Conflict("Invalid parent attempt owner");}else {const auto admitted=child_admission(owner);if((admitted.kind!=ChildAdmissionKind::delegated_leaf&&admitted.kind!=ChildAdmissionKind::dynamic_agent&&!(admitted.kind==ChildAdmissionKind::graph_agent&&root_budget(root).spec.policy_id=="native.graph-context"))||admitted.root_run_id!=root)throw Conflict("Invalid leaf attempt owner");}
    changed_one(db.execute("UPDATE agent_model_call_reservations SET state='started' WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND state='reserved'",{root,attempt,owner}));
    if(!std::holds_alternative<std::nullptr_t>(rows[0][3])){
        const auto step=inference_step(text(rows[0][3]));const auto member=db.execute("SELECT binding_json,head_revision,source_watermark FROM inference_attempt_members WHERE step_id=? AND attempt_id=? AND outcome IS NULL",{step.id,attempt}).rows;if(member.empty())throw Conflict("Inference member is already retired");const auto binding=decode_context_binding(text(member[0][0]));const auto source=read_context_snapshot(db,step.scope,binding,{});if(source.head.revision!=integer(member[0][1])||source.source_watermark!=integer(member[0][2]))throw ContextSourceChanged("Approved inference projection changed before dispatch");changed_one(db.execute("UPDATE inference_steps SET state='started' WHERE id=? AND state='reserved'",{step.id}));
    }
    impl_->event(owner,"budget.model_call.started",Json{{"root_run_id",root},{"attempt_id",attempt},{"role",call_role(role)}}.dump());transaction.commit();return {root,attempt,owner,role,"started",ModelCallPurpose::inference,std::holds_alternative<std::nullptr_t>(rows[0][3])?std::string{}:text(rows[0][3])};
}
ModelCallReservation Repository::finish_model_call(const std::string& root,const std::string& owner,const std::string& attempt,std::optional<std::string> actual_assistant_json,const AgentWorkerLeaseSpec* worker){
    if(actual_assistant_json)(void)object_json(*actual_assistant_json,8*1024*1024);
    auto& db=impl_->database;Transaction transaction(db);const auto rows=db.execute("SELECT role,state,purpose,COALESCE(inference_step_id,'') FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=? AND owner_run_id=?",{root,attempt,owner}).rows;
    if(worker){if(root!=worker->root_run_id||owner!=worker->owner_run_id||actual_assistant_json)throw Conflict("Worker cannot supply a completed assistant response");actual_assistant_json=worker_assistant_receipt(*this,db,*worker,attempt,false);}
    if(rows.empty())throw NotFound("Owned model attempt not found");const auto role=text(rows[0][0])=="parent"?ModelCallRole::parent:ModelCallRole::leaf;const auto state=text(rows[0][1]);
    if(text(rows[0][2])!="inference")throw Conflict("Maintenance requires its dedicated receipt ledger");
    if(state=="finished"){const auto previous=db.execute("SELECT COALESCE(actual_assistant_json,'') FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=?",{root,attempt}).rows;if(actual_assistant_json&&text(previous[0][0])!=*actual_assistant_json)throw Conflict("Actual completed model response differs");transaction.commit();return {root,attempt,owner,role,state,ModelCallPurpose::inference,text(rows[0][3]),text(previous[0][0])};}
    if(state!="reserved"&&state!="started")throw Conflict("Interrupted attempt cannot be adopted");
    if(actual_assistant_json){if(state!="started")throw Conflict("Undispatched reservation cannot acquire a response");changed_one(db.execute("UPDATE agent_model_call_reservations SET state='finished',actual_assistant_json=? WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND state='started'",{*actual_assistant_json,root,attempt,owner}));}
    else changed_one(db.execute("UPDATE agent_model_call_reservations SET state='finished' WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND state=?",{root,attempt,owner,state}));
    impl_->event(owner,"budget.model_call.finished",Json{{"root_run_id",root},{"attempt_id",attempt},{"role",call_role(role)},{"was_started",state=="started"}}.dump());transaction.commit();return {root,attempt,owner,role,"finished",ModelCallPurpose::inference,text(rows[0][3]),actual_assistant_json.value_or("")};
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
        if(!requested.is_object()||(requested.size()!=3&&requested.size()!=4)||!requested.contains("id")||requested["id"]!=task.task_id||!requested.contains("objective")||requested["objective"]!=task.objective||!requested.contains("preset")||requested["preset"]!=spec.preset_id||(requested.contains("agent_id")&&(!requested["agent_id"].is_string()||requested["agent_id"].get_ref<const std::string&>().empty()||requested["agent_id"].get_ref<const std::string&>().size()>64||requested["agent_id"].get_ref<const std::string&>().find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)))throw std::invalid_argument("Delegated child does not match the requested preset, objective, or profile");
        for(auto it=requested.begin();it!=requested.end();++it)if(it.key()!="id"&&it.key()!="objective"&&it.key()!="preset"&&it.key()!="agent_id")throw std::invalid_argument("Unknown delegated task field");
        const auto prompt=Json::parse(object_json(task.prompt_json));
        const bool has_profile=prompt.contains("agent_profile");if(has_profile!=requested.contains("agent_id"))throw std::invalid_argument("Delegated child profile differs from the requested agent identity");
        if(!prompt.contains("content")||prompt["content"]!=task.objective||prompt.size()!=(provider.size()==6?2:1)+(has_profile?1:0))throw std::invalid_argument("Delegated child prompt is not its bounded objective");
        if(provider.size()==6){auto expected_provider=provider;if(has_profile&&prompt["agent_profile"].is_object()&&prompt["agent_profile"].contains("model_id")&&prompt["agent_profile"]["model_id"].is_string()&&!prompt["agent_profile"]["model_id"].get_ref<const std::string&>().empty())expected_provider["model_id"]=prompt["agent_profile"]["model_id"];if(!prompt.contains("provider_context")||prompt["provider_context"]!=expected_provider)throw std::invalid_argument("Delegated child provider identity differs");}
        else if(prompt.contains("provider_context"))throw std::invalid_argument("Unexpected delegated provider context");
        if(has_profile){const auto& profile=prompt["agent_profile"];if(!profile.is_object()||profile.size()<5||profile.size()>7||!profile.contains("id")||!profile["id"].is_string()||profile["id"]!=requested["agent_id"]||profile["id"].get_ref<const std::string&>().find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||!profile.contains("revision")||!profile["revision"].is_number_integer()||profile["revision"]<1||profile["revision"]>9007199254740991LL||!profile.contains("model_id")||!profile["model_id"].is_string()||profile["model_id"].get_ref<const std::string&>().size()>256||!profile.contains("instructions")||!profile["instructions"].is_string()||profile["instructions"].get_ref<const std::string&>().empty()||profile["instructions"].get_ref<const std::string&>().size()>32768||!profile.contains("mode")||profile["mode"]!="subagent"||(profile.contains("tools")&&!profile["tools"].is_array())||(profile.contains("permissions")&&!profile["permissions"].is_array()))throw std::invalid_argument("Invalid pinned delegated agent profile");for(auto it=profile.begin();it!=profile.end();++it)if(it.key()!="id"&&it.key()!="revision"&&it.key()!="model_id"&&it.key()!="instructions"&&it.key()!="mode"&&it.key()!="tools"&&it.key()!="permissions")throw std::invalid_argument("Unknown pinned delegated agent profile field");if(profile.contains("tools")){std::optional<std::vector<std::string>> tools;tools.emplace();for(const auto& item:profile["tools"]){if(!item.is_string())throw std::invalid_argument("Invalid delegated agent tool allowlist");tools->push_back(item.get<std::string>());}validate_agent_tool_allowlist(tools);}if(profile.contains("permissions")){std::vector<AgentPermissionRule> rules;for(const auto& item:profile["permissions"]){if(!item.is_object()||item.size()!=3||!item.contains("action")||!item["action"].is_string()||!item.contains("resource")||!item["resource"].is_string()||!item.contains("effect")||!item["effect"].is_string())throw std::invalid_argument("Invalid delegated agent permission rule");rules.push_back({item["action"].get<std::string>(),item["resource"].get<std::string>(),item["effect"].get<std::string>()});}validate_agent_permission_rules(rules);}}
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
    const auto parent=run(child.parent_id);if(parent.session_id!=child.session_id||(!parent.parent_id.empty()&&!parent.graph_root))throw Conflict("Owned child boundary differs");
    if(parent.graph_root){
        const auto graph_owner=impl_->graph_lineage(parent.id).first;
        const auto graph=Json::parse(graph_run(parent.id).specification_json);for(const auto& node:graph["nodes"])if(node["id"]==child.node_id){
            if(node["type"]=="agent"){if(child.graph_root)throw Conflict("Graph agent child has an invocation record");ChildAdmissionRecord result{ChildAdmissionKind::graph_agent,graph_owner,{},{},0};result.graph_node_path=impl_->graph_node_path(parent.id,child.node_id,graph_owner);if(!impl_->database.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{graph_owner}).rows.empty()){const auto capture=graph_context_owner(graph_owner);result.backend_identity=graph_context_agent(capture.authority,result.graph_node_path).backend_identity;}return result;}
            if(node["type"]=="tool"){if(child.graph_root)throw Conflict("Graph tool child has an invocation record");ChildAdmissionRecord result{ChildAdmissionKind::graph_tool,graph_owner,{},{},0};result.graph_node_path=impl_->graph_node_path(parent.id,child.node_id,graph_owner);return result;}
            if(node["type"]=="graph"){
                if(!child.graph_root)throw Conflict("Graph invocation has no owned graph record");
                const auto owned=graph_run(id);
                if(owned.graph_id!=node.at("graph_id").get<std::string>()||owned.graph_revision!=node.at("graph_revision").get<std::int64_t>()||owned.specification_json!=node.at("graph_spec").dump())throw Conflict("Graph invocation differs from its parent's immutable snapshot");
                (void)impl_->graph_lineage(id);ChildAdmissionRecord result{ChildAdmissionKind::graph_invocation,graph_owner,{},{},0};result.graph_node_path=impl_->graph_node_path(parent.id,child.node_id,graph_owner);return result;
            }
        }throw Conflict("Graph child is not its declared executable node");
    }
    const auto dynamic=impl_->database.execute("SELECT n.plan_id,n.label,n.claim_id,n.definition_revision,n.claim_revision,n.backend_node_id FROM dynamic_plan_nodes n JOIN dynamic_plans p ON p.id=n.plan_id WHERE n.child_run_id=? AND p.root_run_id=?",{id,parent.id}).rows;
    if(!dynamic.empty()){const auto& r=dynamic[0];auto p=dynamic_plan(text(r[0]));const auto n=std::find_if(p.nodes.begin(),p.nodes.end(),[&](const auto& v){return v.child_run_id==id;});if(n==p.nodes.end()||(n->definition.kind!=DynamicNodeKind::agent&&n->definition.kind!=DynamicNodeKind::tool)||n->backend_node_id!=child.node_id||n->claim_revision!=n->definition_revision||n->claim_id.empty()||text(r[5])!=child.node_id)throw Conflict("Dynamic child differs from its accepted claim");
        (void)root_budget(parent.id);ChildAdmissionRecord a;a.kind=n->definition.kind==DynamicNodeKind::tool?ChildAdmissionKind::dynamic_tool:ChildAdmissionKind::dynamic_agent;a.root_run_id=parent.id;a.plan_id=p.id;a.node_label=n->definition.label;a.claim_id=n->claim_id;a.definition_revision=n->definition_revision;a.claim_revision=n->claim_revision;
        if(n->definition.kind==DynamicNodeKind::agent){const auto& preset=dynamic_preset(p.capabilities,n->definition);private_identity(preset.backend_identity);a.preset_id=preset.id;a.preset_revision=preset.revision;a.backend_identity=preset.backend_identity;}
        return a;}
    const auto rows=impl_->database.execute("SELECT b.id,b.root_run_id,b.preset_id,b.preset_revision,b.state,t.node_id FROM delegation_tasks t JOIN delegation_batches b ON b.id=t.batch_id WHERE t.child_run_id=? AND b.parent_run_id=?",{id,parent.id}).rows;
    if(rows.empty())throw Conflict("Child has no accepted native delegation");const auto& r=rows[0];
    if(text(r[1])!=parent.id||text(r[5])!=child.node_id||text(r[2])!="workspace.inspect"||integer(r[3])!=1)throw Conflict("Delegated child ownership or preset differs");
    (void)root_budget(parent.id);return {ChildAdmissionKind::delegated_leaf,parent.id,text(r[0]),text(r[2]),integer(r[3])};
}
std::vector<OwnedChildRecord> Repository::owned_children(const std::string& id){
    const auto parent=run(id);if(!parent.parent_id.empty()){if(!parent.graph_root)throw std::invalid_argument("Owned child observation requires a graph owner or root parent");(void)impl_->graph_lineage(id);}std::vector<OwnedChildRecord> result;
    for(const auto& row:impl_->database.execute("SELECT id FROM runs WHERE parent_run_id=? ORDER BY rowid",{id}).rows){const auto child=run(text(row[0]));const auto admission=child_admission(child.id);std::string label;
        if(admission.kind==ChildAdmissionKind::delegated_leaf){const auto task=impl_->database.execute("SELECT task_id FROM delegation_tasks WHERE child_run_id=?",{child.id}).rows;label=text(task.at(0).at(0));}
        const auto kind=admission.kind==ChildAdmissionKind::dynamic_agent?"dynamic_agent":admission.kind==ChildAdmissionKind::dynamic_tool?"dynamic_tool":admission.kind==ChildAdmissionKind::delegated_leaf?"delegated_leaf":admission.kind==ChildAdmissionKind::graph_agent?"graph_agent":admission.kind==ChildAdmissionKind::graph_invocation?"graph_invocation":"graph_tool";
        result.push_back({child,kind,admission.batch_id,label,admission.preset_id,admission.preset_revision,admission.plan_id,admission.node_label,admission.claim_id,admission.definition_revision,admission.claim_revision});
    }return result;
}
std::vector<Message> Repository::owned_child_history(const std::string& parent,const std::string& child){
    const auto root=run(parent),owned=run(child);if(!root.parent_id.empty()||owned.parent_id.empty()||owned.session_id!=root.session_id)throw NotFound("Child does not belong to this root");if(child_admission(child).root_run_id!=parent)throw NotFound("Child does not belong to this root");return run_history(child);
}
std::vector<Event> Repository::tree_events(const std::string& id,std::int64_t after,std::size_t count){
    if(after<0||count<1||count>256)throw std::invalid_argument("Invalid execution tree cursor");const auto parent=run(id);if(!parent.parent_id.empty())throw std::invalid_argument("Tree events require a root parent");std::vector<Event> result;
    for(const auto& row:impl_->database.execute("WITH RECURSIVE subtree(id,depth) AS (SELECT ?,0 UNION ALL SELECT r.id,s.depth+1 FROM runs r JOIN subtree s ON r.parent_run_id=s.id WHERE s.depth<4) SELECT e.seq,e.run_id,e.kind,e.payload FROM events e JOIN subtree s ON s.id=e.run_id JOIN runs r ON r.id=e.run_id WHERE r.session_id=? AND e.seq>? ORDER BY e.seq LIMIT ?",{id,parent.session_id,after,static_cast<std::int64_t>(count)}).rows)result.push_back({integer(row[0]),text(row[1]),text(row[2]),text(row[3])});return result;
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
    if(!db.execute("SELECT id FROM context_idle_owners WHERE session_id=? AND state='active'",{session_id}).rows.empty())throw Conflict("Session has an exclusive maintenance owner");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND state IN ('queued','running','paused')",{session_id}).rows.empty()) throw Conflict("Session has an active root run");
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,json}));transaction.commit();
}
Run Repository::start_graph_run(const std::string& id,const std::string& session_id,const std::string& graph_id,std::int64_t revision,const GraphPlan& plan,const std::string& prompt,std::optional<RootBudgetSpec> budget,std::optional<GraphContextSpec> context,std::optional<WorkspaceMembershipSnapshot> membership){
    identifier(id);if(graph_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos || graph_id.empty())throw std::invalid_argument("Invalid graph identity");if(graph_id.size()>64 || revision<1 || revision>9007199254740991)throw std::invalid_argument("Invalid graph identity or revision");object_json(prompt);
    if(budget.has_value()!=context.has_value())throw std::invalid_argument("Registered graph context requires its real budget and authority together");std::string capture;if(context){capture=graph_context_json(*context).dump();if(budget->policy_id!="native.graph-context"||budget->max_children!=8||budget->max_parallel<1||budget->max_parallel>2||budget->max_model_calls!=32)throw std::invalid_argument("Registered graph context policy differs");std::set<std::string> declared;for(const auto& n:graph_agent_paths(plan)){declared.insert(n.node_path);const auto& a=graph_context_agent(*context,n.node_path);if(!n.model_id.empty()&&n.model_id!=a.model_id)throw std::invalid_argument("Registered graph model override differs from capture");}if(declared.size()!=context->agents.size())throw std::invalid_argument("Registered graph context contains missing or extra agent authority");}
    const auto provider=prompt_context(prompt);auto& db=impl_->database;Transaction transaction(db);session(session_id);refresh_admission(db);
    if(!db.execute("SELECT id FROM context_idle_owners WHERE session_id=? AND state='active'",{session_id}).rows.empty())throw Conflict("Session has an exclusive maintenance owner");
    if(!db.execute("SELECT id FROM runs WHERE id=?",{id}).rows.empty())throw Conflict("Run already exists");
    if(!db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused')",{session_id}).rows.empty())throw Conflict("Session already has an active root run");
    changed_one(db.execute("INSERT INTO runs(id,session_id,state) VALUES(?,?,'queued')",{id,session_id}));
    changed_one(db.execute("INSERT INTO graph_roots(run_id,graph_id,graph_revision,specification,checkpoint,input) VALUES(?,?,?,?,?,?)",{id,graph_id,revision,plan.json(),GraphCoordinator(plan).checkpoint(),prompt}));
    changed_one(db.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,'user',?)",{session_id,prompt}));
    changed_one(db.execute("INSERT INTO task_messages(message_seq,run_id) VALUES(last_insert_rowid(),?)",{id}));
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));if(budget){impl_->initialize_budget(id,prompt,*budget);changed_one(db.execute("INSERT INTO graph_context_owners(root_run_id,backend_identity,authority_json) VALUES(?,?,?)",{id,context->backend_identity,capture}));}
    if(membership){
        if(membership->workspace_id.empty()||membership->revision<1||membership->folders.empty()||membership->folders.size()>256||membership->folders.front().folder_id!=membership->workspace_id)
            throw std::invalid_argument("Invalid graph workspace membership admission");
        Json folders=Json::array();for(const auto& folder:membership->folders){if(folder.folder_id.empty()||folder.root.empty())throw std::invalid_argument("Invalid graph workspace folder binding");folders.push_back({{"folder_id",folder.folder_id},{"root",folder.root}});}
        impl_->event(id,"agent.workspace_membership",Json{{"workspace_id",membership->workspace_id},{"revision",membership->revision},{"primary_folder_id",membership->folders.front().folder_id},{"folders",std::move(folders)},{"snapshot","graph_admission"}}.dump());
    }
    impl_->event(id,"run.queued",Json{{"kind","graph"},{"graph_id",graph_id},{"graph_revision",revision}}.dump());transaction.commit();return {id,session_id,RunState::queued,{},{},true,provider};
}
GraphContextOwnerRecord Repository::graph_context_owner(const std::string& root){
    const auto graph=graph_run(root);const auto rows=impl_->database.execute("SELECT backend_identity,authority_json FROM graph_context_owners WHERE root_run_id=?",{root}).rows;if(rows.empty())throw NotFound("Registered graph has no captured context owner");GraphContextOwnerRecord out{root,decode_graph_context(text(rows[0][1])),{}};if(out.authority.backend_identity!=text(rows[0][0])||root_budget(root).spec.policy_id!="native.graph-context")throw DatabaseError("Registered graph budget and authority differ");if(!impl_->database.execute("SELECT id FROM agent_budget_segments WHERE root_run_id=?",{root}).rows.empty())out.segment=dynamic_budget_segment(root);if(graph.run.state==RunState::paused){if(!out.segment)throw ContextBindingChanged("Paused registered graph has no clock");graph_context_closed(impl_->database,root,*out.segment);}return out;
}
GraphContextOpenRecord Repository::open_graph_context_owner(const GraphContextOpenSpec& s){
    bounded_identity(s.segment_id);auto& db=impl_->database;Transaction tx(db);const auto capture=graph_context_owner(s.root_run_id);auto graph=graph_run(s.root_run_id);const auto budget=root_budget(s.root_run_id);if(capture.authority.backend_identity!=s.backend_identity)throw ContextBindingChanged("Registered graph settings changed");if(graph.checkpoint_revision!=s.expected_checkpoint_revision)throw ContextSourceChanged("Registered graph checkpoint changed");if(budget.revision!=s.expected_budget_revision)throw ContextBudgetChanged("Registered graph budget changed");if(graph.run.state!=RunState::queued||capture.segment)throw Conflict("Initial graph clock requires its queued captured owner and no prior segment");changed_one(db.execute("UPDATE runs SET state='running' WHERE id=? AND state='queued'",{s.root_run_id}));impl_->event(s.root_run_id,"run.running",R"({"reason":"graph_context_owner_activation"})");graph.run.state=RunState::running;const auto e=impl_->event(s.root_run_id,"budget.segment.opened",Json{{"segment_id",s.segment_id},{"ordinal",1},{"remaining_active_ms",budget.spec.wall_limit_ms}}.dump());changed_one(db.execute("INSERT INTO agent_budget_segments(root_run_id,id,ordinal,state,remaining_active_ms,opened_event_seq) VALUES(?,?,1,'open',?,?)",{s.root_run_id,s.segment_id,budget.spec.wall_limit_ms,e.sequence}));changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=? AND revision=?",{s.root_run_id,budget.revision}));GraphContextOpenRecord out{graph,dynamic_budget_segment(s.root_run_id)};tx.commit();return out;
}
GraphContextOpenRecord Repository::resume_graph_context_owner(const GraphContextOpenSpec& s){
    bounded_identity(s.segment_id);auto& db=impl_->database;Transaction tx(db);const auto capture=graph_context_owner(s.root_run_id);auto graph=graph_run(s.root_run_id);const auto budget=root_budget(s.root_run_id);if(capture.authority.backend_identity!=s.backend_identity)throw ContextBindingChanged("Registered graph resume settings changed");if(graph.checkpoint_revision!=s.expected_checkpoint_revision)throw ContextSourceChanged("Registered graph resume checkpoint changed");if(budget.revision!=s.expected_budget_revision)throw ContextBudgetChanged("Registered graph resume budget changed");if(graph.run.state!=RunState::paused||!capture.segment)throw Conflict("Registered graph is not a verified closed paused owner");graph_context_closed(db,s.root_run_id,*capture.segment);std::set<std::string> runnable;
    if(!db.execute("SELECT run_id FROM graph_invocations WHERE root_run_id=? LIMIT 1",{s.root_run_id}).rows.empty()){
        const auto closed=impl_->closed_paused_graph_tree(s.root_run_id,true,&runnable);
        if(!closed.contains(s.root_run_id)||!runnable.contains(s.root_run_id))throw Conflict("Human input has not made a verified graph ancestry ready");
    }else{const auto d=GraphCoordinator(GraphPlan(graph.specification_json),graph.checkpoint_json,GraphRestoreMode::live).inspect();if(d.halted||(!d.finished&&d.ready.empty()&&d.skippable.empty()))throw Conflict("Human input has not made graph work or final join ready");}
    for(const auto& id:runnable)if(id!=s.root_run_id){changed_one(db.execute("UPDATE runs SET state='running' WHERE id=? AND state='paused'",{id}));impl_->event(id,"run.running",Json{{"reason","graph_context_tree_resume"},{"root_run_id",s.root_run_id}}.dump());}
    if(capture.segment->ordinal>=9007199254740991)throw RootBudgetExhausted("Graph segment identity exhausted");changed_one(db.execute("UPDATE runs SET state='running' WHERE id=? AND state='paused'",{s.root_run_id}));impl_->event(s.root_run_id,"run.running",R"({"reason":"graph_context_human_resume"})");const auto e=impl_->event(s.root_run_id,"budget.segment.opened",Json{{"segment_id",s.segment_id},{"ordinal",capture.segment->ordinal+1},{"remaining_active_ms",capture.segment->remaining_active_ms}}.dump());changed_one(db.execute("INSERT INTO agent_budget_segments(root_run_id,id,ordinal,state,remaining_active_ms,opened_event_seq) VALUES(?,?,?,'open',?,?)",{s.root_run_id,s.segment_id,capture.segment->ordinal+1,capture.segment->remaining_active_ms,e.sequence}));changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=? AND revision=?",{s.root_run_id,budget.revision}));changed_one(db.execute("UPDATE graph_context_owners SET paused_segment_id=NULL,paused_pins_json=NULL WHERE root_run_id=?",{s.root_run_id}));graph.run.state=RunState::running;GraphContextOpenRecord out{graph,dynamic_budget_segment(s.root_run_id)};tx.commit();return out;
}
GraphRootRecord Repository::suspend_graph_context_owner(const GraphContextBoundarySpec& s){
    auto& db=impl_->database;Transaction tx(db);const auto capture=graph_context_owner(s.root_run_id);auto graph=graph_run(s.root_run_id);const auto budget=root_budget(s.root_run_id);if(capture.authority.backend_identity!=s.backend_identity)throw ContextBindingChanged("Registered graph pause settings changed");if(graph.checkpoint_revision!=s.expected_checkpoint_revision)throw ContextSourceChanged("Registered graph pause checkpoint changed");if(budget.revision!=s.expected_budget_revision)throw ContextBudgetChanged("Registered graph pause budget changed");if(graph.run.state!=RunState::running||!capture.segment||capture.segment->id!=s.segment_id||capture.segment->state!="open")throw Conflict("Registered graph pause does not own its open clock");
    if(!db.execute("SELECT run_id FROM graph_invocations WHERE root_run_id=? LIMIT 1",{s.root_run_id}).rows.empty()){
        graph_context_drained(db,s.root_run_id,true,"'running','paused'");
        if(s.measured_active_elapsed_ms>=capture.segment->remaining_active_ms)throw RootBudgetExhausted("Registered graph tree pause has no remaining active time");
        const auto pins=graph_context_pins(db,s.root_run_id);
        if(!impl_->pause_graph_tree(s.root_run_id,true))throw Conflict("Registered graph tree has runnable or unresolved work");
        impl_->close_dynamic_segment(s.root_run_id,s.segment_id,s.measured_active_elapsed_ms);
        changed_one(db.execute("UPDATE graph_context_owners SET paused_segment_id=?,paused_pins_json=? WHERE root_run_id=?",{s.segment_id,pins.dump(),s.root_run_id}));
        changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=? AND revision=?",{s.root_run_id,budget.revision}));
        graph=graph_run(s.root_run_id);tx.commit();return graph;
    }
    const auto coordinator=GraphCoordinator(GraphPlan(graph.specification_json),graph.checkpoint_json,GraphRestoreMode::live);if(!impl_->graph_waiting_only(coordinator))throw Conflict("Registered graph does not have a waiting-only frontier");graph_context_drained(db,s.root_run_id,true);if(s.measured_active_elapsed_ms>=capture.segment->remaining_active_ms)throw RootBudgetExhausted("Registered graph pause has no remaining active time");const auto pins=graph_context_pins(db,s.root_run_id);impl_->close_dynamic_segment(s.root_run_id,s.segment_id,s.measured_active_elapsed_ms);impl_->root_boundary(graph.run,RunState::paused);changed_one(db.execute("UPDATE graph_context_owners SET paused_segment_id=?,paused_pins_json=? WHERE root_run_id=?",{s.segment_id,pins.dump(),s.root_run_id}));changed_one(db.execute("UPDATE runs SET state='paused' WHERE id=? AND state='running'",{s.root_run_id}));changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=? AND revision=?",{s.root_run_id,budget.revision}));impl_->event(s.root_run_id,"run.paused",R"({"reason":"graph_context_human_wait"})");graph.run.state=RunState::paused;tx.commit();return graph;
}
Run Repository::complete_graph_context_owner(const GraphContextBoundarySpec& s,const std::string& join){
    (void)object_json(join,8*1024*1024);auto& db=impl_->database;Transaction tx(db);const auto capture=graph_context_owner(s.root_run_id);auto graph=graph_run(s.root_run_id);const auto budget=root_budget(s.root_run_id);if(capture.authority.backend_identity!=s.backend_identity)throw ContextBindingChanged("Registered graph completion settings changed");if(graph.checkpoint_revision!=s.expected_checkpoint_revision)throw ContextSourceChanged("Registered graph completion checkpoint changed");if(budget.revision!=s.expected_budget_revision)throw ContextBudgetChanged("Registered graph completion budget changed");if(graph.run.state!=RunState::running||!capture.segment||capture.segment->id!=s.segment_id||capture.segment->state!="open")throw Conflict("Registered graph completion has no open owner clock");graph_context_drained(db,s.root_run_id,true);impl_->close_dynamic_segment(s.root_run_id,s.segment_id,s.measured_active_elapsed_ms);impl_->root_boundary(graph.run,RunState::completed);impl_->message(graph.run,"assistant",join);changed_one(db.execute("UPDATE runs SET state='completed' WHERE id=? AND state='running'",{s.root_run_id}));changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=? AND revision=?",{s.root_run_id,budget.revision}));impl_->event(s.root_run_id,"conversation.assistant",join);impl_->event(s.root_run_id,"run.completed","{}");graph.run.state=RunState::completed;tx.commit();return graph.run;
}
Run Repository::retire_graph_context_owner(const std::string& root,RunState terminal_state,const std::string& reason,const std::string& segment,std::int64_t elapsed){
    if(terminal_state!=RunState::failed&&terminal_state!=RunState::cancelled)throw std::invalid_argument("Registered graph retirement requires failure or cancellation");(void)object_json(reason);auto& db=impl_->database;Transaction tx(db);const auto capture=graph_context_owner(root);auto graph=graph_run(root);if(terminal(graph.run.state)){if(graph.run.state!=terminal_state)throw Conflict("Registered graph retired state differs");tx.commit();return graph.run;}if(!db.execute("SELECT run_id FROM graph_invocations WHERE root_run_id=? LIMIT 1",{root}).rows.empty()){
        graph_context_drained(db,root,false,"'queued','running','paused'");
        const auto tree=db.execute("WITH RECURSIVE tree(id,depth) AS (SELECT ?,0 UNION ALL SELECT r.id,t.depth+1 FROM runs r JOIN tree t ON r.parent_run_id=t.id WHERE t.depth<4) SELECT r.id FROM tree t JOIN runs r ON r.id=t.id ORDER BY t.depth DESC,r.id",{root}).rows;
        std::vector<std::string> owners;
        for(const auto& row:tree){const auto current=run(text(row[0]));if(terminal(current.state))continue;
            if(!current.graph_root||impl_->graph_lineage(current.id).first!=root)throw Conflict("Context tree retirement still owns executable descendants");owners.push_back(current.id);
        }
        if(capture.segment&&capture.segment->state=="open"){if(segment.empty())throw Conflict("Context tree retirement requires measured active closure");impl_->close_dynamic_segment(root,segment,elapsed);}else if(!segment.empty())throw Conflict("Context tree retirement has no matching open clock");
        for(const auto& id:owners){auto owner=graph_run(id);impl_->root_boundary(owner.run,terminal_state);GraphCoordinator coordinator(GraphPlan(owner.specification_json),owner.checkpoint_json,GraphRestoreMode::live);auto settled=Json::array();
            for(const auto& node:coordinator.inspect().running){const auto children=db.execute("SELECT id FROM runs WHERE parent_run_id=? AND node_id=?",{id,node}).rows;
                if(children.size()!=1)throw Conflict("Context tree retirement has no unique admitted child");const auto child=run(text(children[0][0]));if(!terminal(child.state))throw Conflict("Context tree retirement has an unsettled child");
                if(child.state==RunState::completed){const auto history=run_history(child.id);if(history.empty()||history.back().role!="assistant")throw DatabaseError("Completed context tree child has no assistant output");try{auto output=Json::parse(history.back().json);output.erase("provider_items");output.erase("provider_context");coordinator.complete(node,output.dump());}catch(const std::invalid_argument&){coordinator.fail(node,false);}}
                else coordinator.fail(node,impl_->has_operations(child.id,"'executing','uncertain'")||!db.execute("SELECT seq FROM events WHERE run_id=? AND kind='run.failed' AND json_extract(payload,'$.reason')='server_restart'",{child.id}).rows.empty());
                settled.push_back({{"child_run_id",child.id},{"node_id",node},{"child_state",state_name(child.state)},{"reason","context_tree_retirement"}});
            }
            coordinator.cancel_pending();impl_->graph_checkpoint(owner,coordinator,owner.checkpoint_revision);
            for(auto& receipt:settled){receipt["checkpoint_revision"]=owner.checkpoint_revision;impl_->event(id,"graph.child.settled",receipt.dump());}
            changed_one(db.execute("UPDATE runs SET state=? WHERE id=? AND state=?",{state_name(terminal_state),id,state_name(owner.run.state)}));impl_->event(id,"run."+state_name(terminal_state),reason);
        }
        for(const auto& row:tree)impl_->cancel_waiting(text(row[0]));
        changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=?",{root}));
        const auto result=run(root);tx.commit();return result;
    }
    graph_context_drained(db,root,false);if(capture.segment&&capture.segment->state=="open"){if(segment.empty())throw Conflict("Registered graph retirement requires measured active closure");impl_->close_dynamic_segment(root,segment,elapsed);}else if(!segment.empty())throw Conflict("Registered graph retirement has no matching open clock");impl_->root_boundary(graph.run,terminal_state);GraphCoordinator coordinator(GraphPlan(graph.specification_json),graph.checkpoint_json,GraphRestoreMode::live);coordinator.cancel_pending();impl_->graph_checkpoint(graph,coordinator);changed_one(db.execute("UPDATE runs SET state=? WHERE id=? AND state=?",{state_name(terminal_state),root,state_name(graph.run.state)}));changed_one(db.execute("UPDATE agent_execution_budgets SET revision=revision+1 WHERE root_run_id=?",{root}));impl_->cancel_waiting(root);for(const auto& row:db.execute("SELECT id FROM runs WHERE parent_run_id=?",{root}).rows)impl_->cancel_waiting(text(row[0]));impl_->event(root,"run."+state_name(terminal_state),reason);graph.run.state=terminal_state;tx.commit();return graph.run;
}
GraphRootRecord Repository::graph_run(const std::string& id){
    const auto current=run(id);if(!current.graph_root)throw Conflict("Run is not a graph root");const auto row=impl_->database.execute("SELECT graph_id,graph_revision,specification,checkpoint_revision,checkpoint,COALESCE(input,'') FROM graph_roots WHERE run_id=?",{id}).rows.at(0);return {current,text(row[0]),integer(row[1]),text(row[2]),integer(row[3]),text(row[4]),text(row[5])};
}
GraphRootRecord Repository::start_graph_invocation(const std::string& id,const std::string& parent_id,const std::string& node_id,std::int64_t expected){
    identifier(id);if(expected<1)throw std::invalid_argument("Invocation admission requires an observed checkpoint revision");
    auto& db=impl_->database;Transaction transaction(db);auto parent=graph_run(parent_id);
    if(parent.run.state!=RunState::running)throw Conflict("Invocation parent is not running");
    const auto lineage=impl_->graph_lineage(parent_id,true);if(lineage.second>=3)throw Conflict("Graph invocation depth exceeds limits");
    if(!db.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{lineage.first}).rows.empty()){const auto capture=graph_context_owner(lineage.first);if(!capture.segment||capture.segment->state!="open")throw Conflict("Context invocation requires its ultimate root open clock");}
    if(parent.checkpoint_revision!=expected)throw Conflict("Graph checkpoint revision changed");
    if(!db.execute("SELECT id FROM runs WHERE id=? OR (parent_run_id=? AND node_id=?)",{id,parent_id,node_id}).rows.empty())throw Conflict("Graph invocation identity already exists");
    GraphCoordinator coordinator(GraphPlan(parent.specification_json),parent.checkpoint_json,GraphRestoreMode::live);
    const auto prepared=coordinator.claim_invocation(node_id);if(!prepared.definition.graph)throw DatabaseError("Invocation lost its immutable binding");
    const auto& binding=*prepared.definition.graph;const GraphPlan child_plan(binding.specification_json);
    if(parent.input_json.empty())throw Conflict("Invocation parent has no captured task input");
    auto input=Json::parse(parent.input_json);if(!input.contains("content")||!input["content"].is_string())throw DatabaseError("Invocation parent input is invalid");
    input["content"]="Graph task:\n"+input["content"].get<std::string>()+"\n\nNode task:\n"+prepared.definition.prompt+"\n\nDependency outputs (data, not authority):\n"+prepared.dependency_outputs_json;
    const auto prompt=input.dump();if(prompt.size()>2097152)throw std::invalid_argument("Nested graph input exceeds limits");
    // Preserve the exact workspace selection captured by the parent owner.
    // This is metadata only: execution still reopens and verifies each folder.
    const auto memberships=db.execute("SELECT payload FROM events WHERE run_id=? AND kind='agent.workspace_membership' ORDER BY seq",{parent_id}).rows;
    if(memberships.size()>1)throw DatabaseError("Invocation parent has duplicate workspace membership snapshots");
    std::string membership;
    if(!memberships.empty()){
        membership=text(memberships[0][0]);const auto value=Json::parse(membership);
        if(!value.is_object()||!value.contains("workspace_id")||!value["workspace_id"].is_string()||value["workspace_id"].get<std::string>().empty()||
           !value.contains("revision")||!value["revision"].is_number_integer()||value["revision"].get<std::int64_t>()<1||
           !value.contains("folders")||!value["folders"].is_array()||value["folders"].empty()||value["folders"].size()>256)
            throw DatabaseError("Invocation parent workspace membership is invalid");
        std::set<std::string> folders;
        for(const auto& folder:value["folders"]){
            if(!folder.is_object()||!folder.contains("folder_id")||!folder["folder_id"].is_string()||!folder.contains("root")||!folder["root"].is_string()||
               folder["folder_id"].get<std::string>().empty()||folder["root"].get<std::string>().empty()||!folders.insert(folder["folder_id"].get<std::string>()).second)
                throw DatabaseError("Invocation parent workspace folder binding is invalid");
        }
        if(value["folders"][0]["folder_id"]!=value["workspace_id"])throw DatabaseError("Invocation parent workspace primary folder differs");
    }
    impl_->graph_checkpoint(parent,coordinator,expected);
    changed_one(db.execute("INSERT INTO runs(id,session_id,state,parent_run_id,node_id) VALUES(?,?,'queued',?,?)",{id,parent.run.session_id,parent_id,node_id}));
    changed_one(db.execute("INSERT INTO graph_roots(run_id,graph_id,graph_revision,specification,checkpoint,input) VALUES(?,?,?,?,?,?)",{id,binding.graph_id,binding.revision,child_plan.json(),GraphCoordinator(child_plan).checkpoint(),prompt}));
    changed_one(db.execute("INSERT INTO graph_invocations(run_id,root_run_id,parent_run_id,depth) VALUES(?,?,?,?)",{id,lineage.first,parent_id,lineage.second+1}));
    const Run child{id,parent.run.session_id,RunState::queued,parent_id,node_id,true,prompt_context(prompt)};impl_->message(child,"user",prompt);
    changed_one(db.execute("INSERT INTO task_history_owners(run_id) VALUES(?)",{id}));
    impl_->event(id,"run.queued",Json{{"kind","graph_invocation"},{"parent_run_id",parent_id},{"node_id",node_id},{"graph_id",binding.graph_id},{"graph_revision",binding.revision}}.dump());
    if(!membership.empty())impl_->event(id,"agent.workspace_membership",membership);
    impl_->event(parent_id,"graph.invocation.queued",Json{{"child_run_id",id},{"node_id",node_id},{"graph_id",binding.graph_id},{"graph_revision",binding.revision}}.dump());
    const auto result=graph_run(id);transaction.commit();return result;
}
Run Repository::start_graph_child(const std::string& id,const std::string& parent_id,const std::string& node_id,const std::string& prompt,std::int64_t expected){
    identifier(id);if(node_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos || node_id.empty())throw std::invalid_argument("Invalid graph node identity");if(node_id.size()>64)throw std::invalid_argument("Graph node ID exceeds limits");object_json(prompt);const auto provider=prompt_context(prompt);auto& db=impl_->database;Transaction transaction(db);auto parent=graph_run(parent_id);
    if(parent.run.state!=RunState::running)throw Conflict("Graph parent is not running");
    const auto lineage=impl_->graph_lineage(parent_id,true);
    const auto specification=Json::parse(parent.specification_json);bool executable=false,agent=false;for(const auto& node:specification.at("nodes"))if(node.at("id")==node_id && (node.at("type")=="agent" || node.at("type")=="tool")){executable=true;agent=node.at("type")=="agent";}if(!executable)throw std::invalid_argument("Child must name a declared executable node");
    if(!db.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{lineage.first}).rows.empty()){
        const auto capture=graph_context_owner(lineage.first);if(!capture.segment||capture.segment->state!="open")throw Conflict("Registered graph child requires its ultimate root open clock");
        if(agent){
            (void)graph_context_agent(capture.authority,impl_->graph_node_path(parent_id,node_id,lineage.first));const auto budget=root_budget(lineage.first);
            const auto active=integer(db.execute("WITH RECURSIVE tree(id,depth) AS (SELECT ?,0 UNION ALL SELECT r.id,t.depth+1 FROM runs r JOIN tree t ON r.parent_run_id=t.id WHERE t.depth<4) SELECT count(*) FROM tree t JOIN runs r ON r.id=t.id WHERE r.state IN ('queued','running','paused') AND EXISTS(SELECT 1 FROM graph_roots g JOIN json_each(g.specification,'$.nodes') n WHERE g.run_id=r.parent_run_id AND json_extract(n.value,'$.id')=r.node_id AND json_extract(n.value,'$.type')='agent')",{lineage.first}).rows[0][0]);
            if(budget.children_admitted>=budget.spec.max_children||active>=budget.spec.max_parallel)throw RootBudgetExhausted("Registered graph tree child allowance exhausted");
            changed_one(db.execute("UPDATE agent_execution_budgets SET children_admitted=children_admitted+1,revision=revision+1 WHERE root_run_id=? AND revision=?",{lineage.first,budget.revision}));
        }
    }
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
    identifier(actor);if(actor.size()>256 || expected<1)throw std::invalid_argument("Invalid graph controller input");object_json(input);auto& db=impl_->database;Transaction transaction(db);auto root=graph_run(id);if(!root.run.parent_id.empty())throw Conflict("Nested graph input requires its observed ultimate root scope");if(root.run.state!=RunState::running && root.run.state!=RunState::paused)throw Conflict("Graph cannot receive human input");const bool contextual=!db.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{id}).rows.empty();if(contextual){if(root.run.state!=RunState::paused)throw Conflict("Registered graph human input requires its closed owner");(void)graph_context_owner(id);}GraphCoordinator coordinator(GraphPlan(root.specification_json),root.checkpoint_json,GraphRestoreMode::live);coordinator.provide_human(node,input);impl_->graph_checkpoint(root,coordinator,expected);impl_->event(id,"graph.human.input",Json{{"node_id",node},{"actor",actor},{"input",Json::parse(input)},{"checkpoint_revision",root.checkpoint_revision}}.dump());if(!contextual&&root.run.state==RunState::paused && !impl_->graph_waiting_only(coordinator)&&impl_->closed_paused_graph_tree(id).empty()){changed_one(db.execute("UPDATE runs SET state='running' WHERE id=? AND state='paused'",{id}));impl_->event(id,"run.running",R"({"reason":"graph_human_input"})");root.run.state=RunState::running;}transaction.commit();return root;
}
GraphRootRecord Repository::input_graph_tree_human(const std::string& root_id,const std::string& owner_id,const std::string& node,const std::string& input,const std::string& actor,std::int64_t expected_root,std::int64_t expected_owner){
    identifier(actor);if(actor.size()>256||expected_root<1||expected_owner<1||owner_id==root_id)throw std::invalid_argument("Nested human input requires distinct owner and root observations");object_json(input);
    auto& db=impl_->database;Transaction transaction(db);auto root=graph_run(root_id),owner=graph_run(owner_id);
    if(!root.run.parent_id.empty()||owner.run.parent_id.empty()||owner.run.session_id!=root.run.session_id||impl_->graph_lineage(owner_id).first!=root_id)throw Conflict("Nested human input owner differs from its root");
    if((root.run.state!=RunState::paused&&root.run.state!=RunState::running)||owner.run.state!=RunState::paused)throw Conflict("Nested graph has no closed human owner");
    if(root.checkpoint_revision!=expected_root||owner.checkpoint_revision!=expected_owner)throw Conflict("Nested human input checkpoint observation changed");
    const bool contextual=!db.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{root_id}).rows.empty();
    if(contextual){if(root.run.state!=RunState::paused)throw Conflict("Contextual nested human input requires its closed ultimate owner");(void)graph_context_owner(root_id);}
    std::vector<Run> path;for(auto current=owner.run;;current=run(current.parent_id)){if(!current.graph_root||current.session_id!=root.run.session_id||(current.state!=RunState::paused&&current.state!=RunState::running))throw Conflict("Nested input ancestry is not resumable");path.push_back(current);if(current.id==root_id)break;if(path.size()>3)throw Conflict("Nested input ancestry exceeds limits");}
    GraphCoordinator coordinator(GraphPlan(owner.specification_json),owner.checkpoint_json,GraphRestoreMode::live);coordinator.provide_human(node,input);impl_->graph_checkpoint(owner,coordinator,expected_owner);
    GraphCoordinator parent(GraphPlan(root.specification_json),root.checkpoint_json,GraphRestoreMode::live);impl_->graph_checkpoint(root,parent,expected_root);
    const auto decision=coordinator.inspect();const bool ready=decision.finished||!decision.ready.empty()||!decision.skippable.empty();
    impl_->event(owner_id,"graph.human.input",Json{{"root_run_id",root_id},{"node_id",node},{"actor",actor},{"input",Json::parse(input)},{"checkpoint_revision",owner.checkpoint_revision},{"root_checkpoint_revision",root.checkpoint_revision}}.dump());
    if(ready&&!contextual)for(const auto& current:path)if(current.state==RunState::paused){changed_one(db.execute("UPDATE runs SET state='running' WHERE id=? AND state='paused'",{current.id}));impl_->event(current.id,"run.running",Json{{"reason","graph_tree_human_input"},{"root_run_id",root_id},{"owner_run_id",owner_id}}.dump());}
    const auto result=graph_run(root_id);transaction.commit();return result;
}
GraphRootRecord Repository::skip_graph_node(const std::string& id,const std::string& node,std::int64_t expected){if(expected<1)throw std::invalid_argument("A graph checkpoint revision is required");auto& db=impl_->database;Transaction transaction(db);auto root=graph_run(id);if(root.run.state!=RunState::running)throw Conflict("Graph is not running");GraphCoordinator coordinator(GraphPlan(root.specification_json),root.checkpoint_json,GraphRestoreMode::live);coordinator.skip(node);impl_->graph_checkpoint(root,coordinator,expected);impl_->event(id,"graph.node.skipped",Json{{"node_id",node},{"checkpoint_revision",root.checkpoint_revision}}.dump());impl_->graph_human_pause(root,coordinator);transaction.commit();return root;}
Run Repository::retire_graph_run(const std::string& id,RunState next,const std::string& reason){
    if(!impl_->database.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{id}).rows.empty())throw Conflict("Registered graph context retirement requires its typed owner operation");
    if(next!=RunState::failed && next!=RunState::cancelled)throw std::invalid_argument("Graph retirement requires a failed/cancelled outcome");object_json(reason);auto& db=impl_->database;Transaction transaction(db);auto root=graph_run(id);if(!allowed(root.run.state,next))throw Conflict("Graph cannot retire from its current state");
    if(root.run.state==RunState::paused&&!root.run.parent_id.empty())throw Conflict("Paused invocation retirement requires its ultimate root owner");
    if((root.run.state==RunState::paused||root.run.state==RunState::running)&&root.run.parent_id.empty()&&!db.execute("SELECT run_id FROM graph_invocations WHERE root_run_id=? LIMIT 1",{id}).rows.empty()){
        const auto tree=db.execute("WITH RECURSIVE tree(id,depth) AS (SELECT ?,0 UNION ALL SELECT r.id,t.depth+1 FROM runs r JOIN tree t ON r.parent_run_id=t.id WHERE t.depth<4) SELECT r.id FROM tree t JOIN runs r ON r.id=t.id ORDER BY t.depth DESC,r.id",{id}).rows;
        if(tree.size()>257)throw Conflict("Quiescent retirement tree exceeds expansion limits");
        std::vector<std::string> owners;
        for(const auto& row:tree){const auto current=run(text(row[0]));if(terminal(current.state))continue;
            if(!current.graph_root||(current.state!=RunState::paused&&current.state!=RunState::running&&current.state!=RunState::queued)||impl_->graph_lineage(current.id).first!=id)throw Conflict("Quiescent retirement tree still owns executable work");
            if(impl_->has_operations(current.id,"'proposed','awaiting_approval','approved','executing','uncertain'"))throw Conflict("Quiescent retirement tree has unresolved effects");owners.push_back(current.id);
        }
        if(!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND state IN ('reserved','started')",{id}).rows.empty())throw Conflict("Quiescent retirement tree still owns model transport");
        for(const auto& owner:owners){auto graph=graph_run(owner);impl_->root_boundary(graph.run,next);GraphCoordinator coordinator(GraphPlan(graph.specification_json),graph.checkpoint_json,GraphRestoreMode::live);
            for(const auto& node:coordinator.inspect().running){const auto children=db.execute("SELECT id,state FROM runs WHERE parent_run_id=? AND node_id=?",{owner,node}).rows;
                if(children.size()!=1||!terminal(state_value(text(children[0][1])))||text(children[0][1])=="completed")throw Conflict("Quiescent retirement has an unsettled child outcome");
                coordinator.fail(node,false);impl_->event(owner,"graph.child.settled",Json{{"child_run_id",text(children[0][0])},{"node_id",node},{"child_state",text(children[0][1])},{"reason","quiescent_tree_retirement"}}.dump());
            }
            coordinator.cancel_pending();impl_->graph_checkpoint(graph,coordinator,graph.checkpoint_revision);changed_one(db.execute("UPDATE runs SET state=? WHERE id=? AND state=?",{state_name(next),owner,state_name(graph.run.state)}));impl_->cancel_waiting(owner);impl_->event(owner,"run."+state_name(next),reason);
        }
        transaction.commit();root.run.state=next;return root.run;
    }
    impl_->root_boundary(root.run,next);GraphCoordinator coordinator(GraphPlan(root.specification_json),root.checkpoint_json,GraphRestoreMode::live);coordinator.cancel_pending();impl_->graph_checkpoint(root,coordinator);changed_one(db.execute("UPDATE runs SET state=? WHERE id=? AND state=?",{state_name(next),id,state_name(root.run.state)}));impl_->cancel_waiting(id);impl_->event(id,"run."+state_name(next),reason);transaction.commit();root.run.state=next;return root.run;
}
std::vector<Run> Repository::children(const std::string& id){graph_run(id);std::vector<Run> result;for(const auto& row:impl_->database.execute("SELECT id FROM runs WHERE parent_run_id=? ORDER BY rowid",{id}).rows)result.push_back(run(text(row[0])));return result;}
std::vector<Message> Repository::run_history(const std::string& id){const auto current=run(id);if(current.parent_id.empty())return history(current.session_id);std::vector<Message> result;for(const auto& row:impl_->database.execute("SELECT seq,role,payload FROM messages WHERE execution_run_id=? ORDER BY seq",{id}).rows)result.push_back({integer(row[0]),text(row[1]),text(row[2])});return result;}
std::vector<Event> Repository::graph_events(const std::string& id,std::int64_t after){if(after<0)throw std::invalid_argument("Negative cursor");graph_run(id);std::vector<Event> result;for(const auto& row:impl_->database.execute("WITH RECURSIVE subtree(id,depth) AS (SELECT ?,0 UNION ALL SELECT r.id,s.depth+1 FROM runs r JOIN subtree s ON r.parent_run_id=s.id WHERE s.depth<4) SELECT e.seq,e.run_id,e.kind,e.payload FROM events e JOIN subtree s ON s.id=e.run_id WHERE e.seq>? ORDER BY e.seq",{id,after}).rows)result.push_back({integer(row[0]),text(row[1]),text(row[2]),text(row[3])});return result;}
SkillSelections Repository::run_skills(const std::string& id){
    run(id);const auto rows=impl_->database.execute("SELECT workspace_id,selections_json FROM run_skills WHERE run_id=?",{id}).rows;if(rows.empty())throw NotFound("Run skills not initialized");
    auto result=skill_selections(text(rows[0][1]));if(result.workspace_id!=text(rows[0][0]))throw DatabaseError("Saved run skill workspace differs");return result;
}
SessionSkillState Repository::session_skills(const std::string& id,const std::string& workspace){
    session(id);SessionSkillState result{{workspace,{}}};validate_skill_selections(result.selections);auto& db=impl_->database;
    const auto rows=db.execute("SELECT selections_json,revision FROM session_skills WHERE session_id=? AND workspace_id=?",{id,workspace}).rows;
    if(!rows.empty()){result.selections=skill_selections(text(rows[0][0]));result.revision=integer(rows[0][1]);if(result.selections.workspace_id!=workspace)throw DatabaseError("Saved session skill workspace differs");}
    result.editable=db.execute("SELECT id FROM runs WHERE session_id=? AND state IN ('queued','running','paused') UNION ALL SELECT id FROM context_idle_owners WHERE session_id=? AND state='active' LIMIT 1",{id,id}).rows.empty();return result;
}
SessionSkillState Repository::replace_session_skills(const std::string& id,const SkillSelections& selections,std::int64_t expected){
    validate_skill_selections(selections);if(expected<0||expected>=9007199254740991LL)throw std::invalid_argument("Invalid session skill revision");
    const std::set<std::string> selected(selections.ids.begin(),selections.ids.end()),manual(selections.manual_ids.begin(),selections.manual_ids.end());
    if(selected!=manual)throw std::invalid_argument("User skill replacement requires explicit provenance for every selection");
    auto& db=impl_->database;Transaction tx(db);const auto previous=session_skills(id,selections.workspace_id);
    if(!previous.editable)throw Conflict("Session skills cannot change while execution or context maintenance owns the session");
    if(previous.revision!=expected)throw Conflict("Session skills changed; refresh before editing");
    const auto source=skill_selections_json(selections);
    if(expected==0)db.execute("INSERT INTO session_skills VALUES(?,?,?,1)",{id,selections.workspace_id,source});
    else changed_one(db.execute("UPDATE session_skills SET selections_json=?,revision=revision+1 WHERE session_id=? AND workspace_id=? AND revision=?",{source,id,selections.workspace_id,expected}));
    auto result=session_skills(id,selections.workspace_id);tx.commit();return result;
}
SessionAgentState Repository::session_agent(const std::string& id){
    session(id);SessionAgentState result;auto& db=impl_->database;
    const auto rows=db.execute("SELECT selection_json,revision FROM session_agents WHERE session_id=?",{id}).rows;
    if(!rows.empty()){
        const auto encoded=text(rows[0][0]);result.revision=integer(rows[0][1]);
        if(result.revision<1||result.revision>9007199254740991LL)throw DatabaseError("Invalid session agent revision");
        try{
            const auto value=Json::parse(object_json(encoded,65536));
            if(value.size()!=2||!value.contains("version")||!value["version"].is_number_integer()||value["version"]!=1||!value.contains("agent"))throw DatabaseError("Invalid saved session agent selection");
            if(!value["agent"].is_null()){
                const auto& saved=value["agent"];if(!saved.is_object())throw DatabaseError("Invalid saved session agent definition");
                for(auto it=saved.begin();it!=saved.end();++it)if(it.key()!="id"&&it.key()!="revision"&&it.key()!="model_id"&&it.key()!="instructions"&&it.key()!="tools"&&it.key()!="permissions"&&it.key()!="name"&&it.key()!="description"&&it.key()!="mode"&&it.key()!="hidden"&&it.key()!="default"&&it.key()!="placement")throw DatabaseError("Invalid saved session agent field");
                if(saved.size()<4||saved.size()>12||!saved.contains("id")||!saved["id"].is_string()||!saved.contains("revision")||!saved["revision"].is_number_integer()||!saved.contains("model_id")||!saved["model_id"].is_string()||!saved.contains("instructions")||!saved["instructions"].is_string()||(saved.contains("name")!=saved.contains("description"))||(saved.contains("tools")&&!saved["tools"].is_array())||(saved.contains("permissions")&&!saved["permissions"].is_array())||(saved.contains("hidden")&&!saved["hidden"].is_boolean())||(saved.contains("default")&&!saved["default"].is_boolean()))throw DatabaseError("Invalid saved session agent definition");
                AgentDefinition agent{saved["id"].get<std::string>(),saved["revision"].get<std::int64_t>(),saved["model_id"].get<std::string>(),saved["instructions"].get<std::string>()};
                agent.name=saved.contains("name")?saved["name"].get<std::string>():agent.id;agent.description=saved.contains("description")?saved["description"].get<std::string>():std::string{};
                agent.mode=saved.contains("mode")?saved["mode"].get<std::string>() :"all";agent.placement=saved.contains("placement")?saved["placement"].get<std::string>():std::string{};validate_agent_placement(agent);agent.hidden=saved.contains("hidden")&&saved["hidden"].get<bool>();agent.is_default=saved.contains("default")&&saved["default"].get<bool>();
                if(saved.contains("tools")){if(!saved["tools"].is_array())throw DatabaseError("Invalid saved session agent tool bindings");agent.tools.emplace();for(const auto& item:saved["tools"]){if(!item.is_string())throw DatabaseError("Invalid saved session agent tool binding");agent.tools->push_back(item.get<std::string>());}validate_agent_tool_allowlist(agent.tools);}
                if(saved.contains("permissions")){for(const auto& item:saved["permissions"]){if(!item.is_object()||item.size()!=3||!item.contains("action")||!item["action"].is_string()||!item.contains("resource")||!item["resource"].is_string()||!item.contains("effect")||!item["effect"].is_string())throw DatabaseError("Invalid saved session agent permission rule");agent.permissions.push_back({item["action"].get<std::string>(),item["resource"].get<std::string>(),item["effect"].get<std::string>()});}validate_agent_permission_rules(agent.permissions);}
                if(agent.id.empty()||agent.id.size()>64||agent.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||agent.revision<1||agent.revision>9007199254740991LL||agent.model_id.size()>256||agent.model_id.find('\0')!=std::string::npos||agent.instructions.empty()||agent.instructions.size()>32768||agent.instructions.find('\0')!=std::string::npos||agent.name.empty()||agent.name.size()>128||agent.name.find('\0')!=std::string::npos||agent.description.size()>2048||agent.description.find('\0')!=std::string::npos||(agent.mode!="all"&&agent.mode!="primary"&&agent.mode!="subagent"))throw DatabaseError("Invalid saved session agent definition values");
                result.selected=std::move(agent);
            }
        }catch(const DatabaseError&){throw;}catch(...){throw DatabaseError("Invalid saved session agent selection");}
    }
    result.editable=db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused') UNION ALL SELECT id FROM context_idle_owners WHERE session_id=? AND state='active' LIMIT 1",{id,id}).rows.empty();
    return result;
}
SessionAgentState Repository::replace_session_agent(const std::string& id,std::optional<AgentDefinition> selected,std::int64_t expected){
    if(expected<0||expected>=9007199254740991LL)throw std::invalid_argument("Invalid session agent revision");
    if(selected){const auto& agent=*selected;validate_agent_placement(agent);validate_agent_tool_allowlist(agent.tools);validate_agent_permission_rules(agent.permissions);if(agent.id.empty()||agent.id.size()>64||agent.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||agent.revision<1||agent.revision>9007199254740991LL||agent.model_id.size()>256||agent.model_id.find('\0')!=std::string::npos||agent.instructions.empty()||agent.instructions.size()>32768||agent.instructions.find('\0')!=std::string::npos||agent.name.size()>128||agent.name.find('\0')!=std::string::npos||agent.description.size()>2048||agent.description.find('\0')!=std::string::npos||(agent.mode!="all"&&agent.mode!="primary"&&agent.mode!="subagent")||(agent.is_default&&(agent.hidden||agent.mode=="subagent")))throw std::invalid_argument("Invalid selected agent definition");}
    auto& db=impl_->database;Transaction tx(db);const auto previous=session_agent(id);
    if(!previous.editable)throw Conflict("Conversation agent cannot change while execution or context maintenance owns the session");
    if(previous.revision!=expected)throw Conflict("Conversation agent changed; refresh before editing");
    Json snapshot=nullptr;if(selected){snapshot={{"id",selected->id},{"revision",selected->revision},{"model_id",selected->model_id},{"instructions",selected->instructions},{"name",selected->name.empty()?selected->id:selected->name},{"description",selected->description},{"mode",selected->mode},{"hidden",selected->hidden},{"default",selected->is_default}};if(!selected->placement.empty())snapshot["placement"]=selected->placement;if(selected->tools)snapshot["tools"]=*selected->tools;if(!selected->permissions.empty()){snapshot["permissions"]=Json::array();for(const auto& rule:selected->permissions)snapshot["permissions"].push_back(Json{{"action",rule.action},{"resource",rule.resource},{"effect",rule.effect}});}}
    const auto source=Json{{"version",1},{"agent",std::move(snapshot)}}.dump();
    if(expected==0)db.execute("INSERT INTO session_agents VALUES(?,?,1)",{id,source});
    else changed_one(db.execute("UPDATE session_agents SET selection_json=?,revision=revision+1 WHERE session_id=? AND revision=?",{source,id,expected}));
    if(selected){const auto previous_id=previous.selected?std::optional<std::string>(previous.selected->id):std::nullopt;db.execute("INSERT INTO session_agent_events(session_id,agent_id,previous_agent_id,created_ms) VALUES(?,?,?,?)",{id,selected->id,previous_id?SqlValue(*previous_id):SqlValue(nullptr),now_ms()});}
    auto result=session_agent(id);tx.commit();return result;
}
std::vector<SessionAgentEvent> Repository::session_agent_events(const std::string& id,std::int64_t after,std::size_t count){
    if(after<0||after>9007199254740991LL||count<1||count>256)throw std::invalid_argument("Invalid session agent event cursor or limit");session(id);std::vector<SessionAgentEvent> result;
    for(const auto& row:impl_->database.execute("SELECT sequence,agent_id,previous_agent_id,created_ms FROM session_agent_events WHERE session_id=? AND sequence>? ORDER BY sequence LIMIT ?",{id,after,static_cast<std::int64_t>(count)}).rows){std::optional<std::string> previous;if(!std::holds_alternative<std::nullptr_t>(row[2]))previous=text(row[2]);const auto sequence=integer(row[0]),created=integer(row[3]);if(sequence<1||sequence>9007199254740991LL||created<1)throw DatabaseError("Stored session agent event is invalid");result.push_back({sequence,id,text(row[1]),std::move(previous),created});}return result;
}
SessionModelState Repository::session_model(const std::string& id){
    session(id);auto& db=impl_->database;SessionModelState result;
    const auto rows=db.execute("SELECT selection_json,revision FROM session_models WHERE session_id=?",{id}).rows;
    if(!rows.empty()){
        result.revision=integer(rows[0][1]);
        if(result.revision<1||result.revision>9007199254740991LL)throw DatabaseError("Invalid session model revision");
        try{
            if(std::holds_alternative<std::nullptr_t>(rows[0][0])){result.selected.reset();}
            else {
            const auto value=Json::parse(object_json(text(rows[0][0]),2048));
            if(value.size()!=4||!value.contains("version")||!value["version"].is_number_integer()||value["version"]!=1||!value.contains("model_id")||!value["model_id"].is_string()||!value.contains("provider_profile_id")||!value["provider_profile_id"].is_string()||!value.contains("provider_route_id")||!value["provider_route_id"].is_string())throw DatabaseError("Invalid saved session model selection");
            SessionModelSelection selected{value["model_id"].get<std::string>(),value["provider_profile_id"].get<std::string>(),value["provider_route_id"].get<std::string>()};
            if(selected.model_id.empty()||selected.model_id.size()>256||selected.model_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos||selected.provider_profile_id.empty()||selected.provider_profile_id.size()>256||selected.provider_profile_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos||selected.provider_route_id.empty()||selected.provider_route_id.size()>256||selected.provider_route_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw DatabaseError("Invalid saved session model selection values");
            result.selected=std::move(selected);
            }
        }catch(const DatabaseError&){throw;}catch(...){throw DatabaseError("Invalid saved session model selection");}
    }
    result.editable=db.execute("SELECT id FROM runs WHERE session_id=? AND parent_run_id IS NULL AND state IN ('queued','running','paused') UNION ALL SELECT id FROM context_idle_owners WHERE session_id=? AND state='active' LIMIT 1",{id,id}).rows.empty();
    return result;
}
SessionModelState Repository::replace_session_model(const std::string& id,std::optional<SessionModelSelection> selected,std::int64_t expected){
    if(expected<0||expected>=9007199254740991LL)throw std::invalid_argument("Invalid session model revision");
    if(selected&&(selected->model_id.empty()||selected->model_id.size()>256||selected->model_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos||selected->provider_profile_id.empty()||selected->provider_profile_id.size()>256||selected->provider_profile_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos||selected->provider_route_id.empty()||selected->provider_route_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos))throw std::invalid_argument("Invalid selected session model");
    auto& db=impl_->database;Transaction tx(db);const auto previous=session_model(id);
    if(!previous.editable)throw Conflict("Conversation model cannot change while execution or context maintenance owns the session");
    if(previous.revision!=expected)throw Conflict("Conversation model changed; refresh before editing");
    const auto source=selected?SqlValue(Json{{"version",1},{"model_id",selected->model_id},{"provider_profile_id",selected->provider_profile_id},{"provider_route_id",selected->provider_route_id}}.dump()):SqlValue(nullptr);
    if(expected==0)db.execute("INSERT INTO session_models VALUES(?,?,1)",{id,source});
    else changed_one(db.execute("UPDATE session_models SET selection_json=?,revision=revision+1 WHERE session_id=? AND revision=?",{source,id,expected}));
    auto result=session_model(id);tx.commit();return result;
}
SkillSelections Repository::initialize_run_skills(const std::string& id,const std::string& workspace){
    SkillSelections initial{workspace,{}};validate_skill_selections(initial);auto& db=impl_->database;Transaction tx(db);const auto current=run(id);
    if(current.state!=RunState::running||current.graph_root)throw Conflict("Skills require a running agent owner");
    if(!db.execute("SELECT run_id FROM run_skills WHERE run_id=?",{id}).rows.empty()){auto saved=run_skills(id);if(saved.workspace_id!=workspace)throw Conflict("Saved run skills belong to another workspace");tx.commit();return saved;}
    bool inherited=false;
    if(!current.parent_id.empty()&&!db.execute("SELECT run_id FROM run_skills WHERE run_id=?",{current.parent_id}).rows.empty()){
        initial=run_skills(current.parent_id);if(initial.workspace_id!=workspace)throw Conflict("Parent skill workspace differs");inherited=true;
    }else{const auto rows=db.execute("SELECT selections_json FROM session_skills WHERE session_id=? AND workspace_id=?",{current.session_id,workspace}).rows;if(!rows.empty())initial=skill_selections(text(rows[0][0]));}
    db.execute("INSERT INTO run_skills VALUES(?,?,?,1)",{id,workspace,skill_selections_json(initial)});
    if(!initial.ids.empty())impl_->event(id,"skill.selection.restored",Json{{"workspace_id",workspace},{"ids",initial.ids},{"source",inherited?"parent_run":"session"}}.dump());
    tx.commit();return initial;
}
void Repository::Impl::apply_skill_turn(Repository& repository,const Run& current,const Json& actual,const std::vector<std::string>& tool_json,const std::optional<SkillSelections>& skills,const std::set<std::string>* activation_calls){
    auto& db=database;const auto& id=current.id;
    if(skills){
        const auto selected_json=skill_selections_json(*skills);const auto previous=repository.run_skills(id);if(previous.workspace_id!=skills->workspace_id)throw Conflict("Skill tool turn workspace changed");
        std::map<std::string,Json> results;for(const auto& raw:tool_json){const auto item=Json::parse(object_json(raw,8*1024*1024));if(!item.contains("tool_call_id")||!item["tool_call_id"].is_string()||!results.emplace(item["tool_call_id"].get<std::string>(),item).second)throw std::invalid_argument("Skill tool turn requires unique actual result identities");}
        if(!actual.contains("tool_calls")||!actual["tool_calls"].is_array()||actual["tool_calls"].size()!=results.size())throw std::invalid_argument("Skill tool turn requires all matched native calls and results");
        std::set<std::string> call_ids;for(const auto& call:actual["tool_calls"]){
            if(!call.is_object()||!call.contains("id")||!call["id"].is_string()||call["id"].get_ref<const std::string&>().empty()||!call_ids.insert(call["id"].get<std::string>()).second||!call.contains("name")||!call["name"].is_string()||!call.contains("arguments")||!call["arguments"].is_string()||!results.contains(call["id"].get<std::string>())||!results.at(call["id"].get<std::string>()).contains("content")||!results.at(call["id"].get<std::string>())["content"].is_string())throw std::invalid_argument("Invalid matched native skill tool batch");
        }
        auto expected=previous;
        if(actual.contains("tool_calls"))for(const auto& call:actual["tool_calls"])if(call.value("name",std::string{})=="load_skill"&&(!activation_calls||activation_calls->contains(call.at("id").get<std::string>()))){
            const auto found=results.find(call.at("id").get<std::string>());if(found==results.end()||!found->second.contains("content")||!found->second["content"].is_string())throw std::invalid_argument("Skill activation lacks its matching tool result");
            const auto output=Json::parse(object_json(found->second["content"].get<std::string>(),32768));
            if(output.contains("activation")){
                const auto arguments=Json::parse(object_json(call.at("arguments").get<std::string>(),4096));
                if(output["activation"]!="requested_for_next_model_request"||output.value("effect_permission",true)||arguments.size()!=1||!arguments.contains("id")||!arguments["id"].is_string()||!output.contains("skill")||!output["skill"].is_object()||output["skill"].value("id",std::string{})!=arguments["id"].get<std::string>()||!output["skill"].value("model_invocable",false))throw std::invalid_argument("Invalid native skill activation acknowledgement");
                const auto selected=arguments["id"].get<std::string>();if(std::find(expected.ids.begin(),expected.ids.end(),selected)==expected.ids.end())expected.ids.push_back(selected);
            }
        }
        if(skill_selections_json(expected)!=selected_json)throw Conflict("Selection change lacks a committed successful native activation");
        if(selected_json!=skill_selections_json(previous)){
            changed_one(db.execute("UPDATE run_skills SET selections_json=?,revision=revision+1 WHERE run_id=? AND workspace_id=?",{selected_json,id,skills->workspace_id}));
            if(current.parent_id.empty()){
                const auto rows=db.execute("SELECT selections_json,revision FROM session_skills WHERE session_id=? AND workspace_id=?",{current.session_id,skills->workspace_id}).rows;
                if(rows.empty()){if(!previous.ids.empty())throw Conflict("Session skill state disappeared");db.execute("INSERT INTO session_skills VALUES(?,?,?,1)",{current.session_id,skills->workspace_id,selected_json});}
                else{if(text(rows[0][0])!=skill_selections_json(previous))throw Conflict("Session skill state changed outside its running owner");changed_one(db.execute("UPDATE session_skills SET selections_json=?,revision=revision+1 WHERE session_id=? AND workspace_id=? AND revision=?",{selected_json,current.session_id,skills->workspace_id,integer(rows[0][1])}));}
            }
            event(id,"skill.selection.changed",Json{{"workspace_id",skills->workspace_id},{"ids",skills->ids},{"session_selection",current.parent_id.empty()}}.dump());
        }
    }
}
void Repository::record_tool_turn(const std::string& id,const std::string& assistant_json,const std::vector<std::string>& tool_json,const std::optional<SkillSelections>& skills) {
    if(tool_json.empty() || tool_json.size()>64) throw std::invalid_argument("Invalid tool result batch");
    auto& db=impl_->database;Transaction transaction(db);const auto current=run(id);
    if(current.state!=RunState::running) throw Conflict("Run is not running");
    if(!db.execute("SELECT id FROM dynamic_plan_calls WHERE root_run_id=? AND state IN ('accepted','report_ready')",{id}).rows.empty())throw Conflict("Signed planning turns require their typed commit");
    const auto actual=Json::parse(object_json(assistant_json,8*1024*1024));if(actual.contains("tool_calls"))for(const auto& call:actual["tool_calls"])if(call.value("name",std::string{})=="plan_tasks"||call.value("name",std::string{})=="revise_plan")throw Conflict("Planning conversation turns require their accepted signed ledger");
    impl_->apply_skill_turn(*this,current,actual,tool_json,skills);
    impl_->message(current,"assistant",assistant_json);
    for(const auto& json:tool_json) impl_->message(current,"tool",json);
    impl_->event(id,"conversation.tool_turn","{}");transaction.commit();
}
Run Repository::complete_run(const std::string& id,const std::string& supplied_assistant,const AgentWorkerLeaseSpec* worker,const std::string& worker_attempt) {
    auto& db=impl_->database;Transaction transaction(db);auto current=run(id);
    std::string assistant_json=supplied_assistant;
    if(worker){if(id!=worker->owner_run_id||!supplied_assistant.empty())throw Conflict("Worker cannot supply a final assistant response");assistant_json=worker_assistant_receipt(*this,db,*worker,worker_attempt,true);}
    std::optional<DynamicPlanRecord> child_plan;
    if(current.state!=RunState::running) throw Conflict("Run is not running");
    if(current.graph_root&&!db.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{id}).rows.empty())throw Conflict("Registered graph context completion requires measured typed closure");
    if(!current.parent_id.empty()){
        const auto admission=child_admission(id);
        if(admission.kind==ChildAdmissionKind::dynamic_agent){
            child_plan=dynamic_plan_for_owner(id);
            if(child_plan&&!inspect_dynamic_plan(*child_plan).finished)throw Conflict("Dynamic child cannot complete while its owned plan is unfinished");
        }
    }
    impl_->root_boundary(current,RunState::completed);
    if(impl_->has_unresolved_effects(id)) throw Conflict("Run has unresolved operations");
    impl_->message(current,"assistant",assistant_json);
    changed_one(db.execute("UPDATE runs SET state='completed' WHERE id=? AND state='running'",{id}));
    if(child_plan)changed_one(db.execute("UPDATE dynamic_plans SET state='completed',state_sequence=state_sequence+1 WHERE id=? AND state='active' AND EXISTS(SELECT 1 FROM dynamic_plan_owners WHERE plan_id=dynamic_plans.id AND owner_run_id=?)",{child_plan->id,id}));
    impl_->event(id,"conversation.assistant",assistant_json);impl_->event(id,"run.completed","{}");if(worker)impl_->retire_worker(worker->generation,"completed");transaction.commit();current.state=RunState::completed;return current;
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
Run Repository::transition(const std::string& id,RunState expected,RunState next,const std::string& json,const AgentWorkerLeaseSpec* worker) {
    if(!allowed(expected,next)) throw std::invalid_argument("Invalid run transition");
    auto& db=impl_->database; Transaction transaction(db); auto result=run(id);std::optional<DynamicPlanRecord> child_plan;
    if(worker){worker_current_scope(*this,db,*worker,true);if(id!=worker->owner_run_id||expected!=RunState::queued||next!=RunState::running||json!="{}"||!worker_agent_owner(*this,*worker))throw Conflict("Worker startup differs from captured ordinary-root admission");}
    if(result.graph_root&&!db.execute("SELECT root_run_id FROM graph_context_owners WHERE root_run_id=?",{id}).rows.empty())throw Conflict("Registered graph context lifecycle requires its typed owner operation");
    if(next==RunState::completed&&!result.parent_id.empty()){
        const auto admission=child_admission(id);
        if(admission.kind==ChildAdmissionKind::dynamic_agent){
            child_plan=dynamic_plan_for_owner(id);
            if(child_plan&&!inspect_dynamic_plan(*child_plan).finished)throw Conflict("Dynamic child cannot complete while its owned plan is unfinished");
        }
    }
    impl_->root_boundary(result,next);
    const auto executing=db.execute("SELECT tool,arguments FROM operations WHERE run_id=? AND state='executing'",{id}).rows;
    if(!executing.empty()) {
        for(const auto& operation:executing){bool background=false,persistent=false;try{const auto args=Json::parse(text(operation[1]));background=args.value("background",false);persistent=args.value("persistent",false);}catch(...){throw DatabaseError("Executing operation binding is invalid");}if(text(operation[0])!="run_process"||!background||!((next==RunState::failed||next==RunState::cancelled)||(next==RunState::completed&&persistent)))throw Conflict("Run has an executing non-background operation");}
    }
    if(next==RunState::completed && impl_->has_unresolved_effects(id)) throw Conflict("Run has unresolved operations");
    const auto changed=db.execute("UPDATE runs SET state=? WHERE id=? AND state=?",{state_name(next),id,state_name(expected)});
    if(changed.affected_rows==0) throw Conflict("Run state changed");
    changed_one(changed);
    if(child_plan)changed_one(db.execute("UPDATE dynamic_plans SET state='completed',state_sequence=state_sequence+1 WHERE id=? AND state='active' AND EXISTS(SELECT 1 FROM dynamic_plan_owners WHERE plan_id=dynamic_plans.id AND owner_run_id=?)",{child_plan->id,id}));
    if(terminal(next)) impl_->cancel_waiting(id);
    impl_->event(id,"run."+state_name(next),json);
    if(worker&&worker->root_run_id!=worker->owner_run_id)impl_->event(id,"agent.delegation.preset",Json{{"preset_id","workspace.inspect"},{"preset_revision",1},{"root_run_id",worker->root_run_id},{"tool_policy","read_only"}}.dump());
    transaction.commit(); result.state=next; return result;
}
Event Repository::append_event(const std::string& id,const std::string& kind,const std::string& json) {
    identifier(kind); if(kind.rfind("context.",0)==0 || kind.rfind("run.",0)==0 || kind.rfind("operation.",0)==0 || kind.rfind("delegation.",0)==0 || kind.rfind("budget.",0)==0 || kind.rfind("plan.",0)==0 || kind.rfind("skill.",0)==0 || kind.rfind("worker.",0)==0) throw std::invalid_argument("Lifecycle events require their owning repository operation");
    Transaction transaction(impl_->database);
    const auto owner_run=run(id);
    if(terminal(owner_run.state)) {
        Json payload;
        try{payload=Json::parse(object_json(json));}
        catch(...){throw std::invalid_argument("Invalid terminal run event");}
        if(kind=="process.output") {
            if(!payload.contains("operation_id")||!payload["operation_id"].is_string())throw std::invalid_argument("Terminal process output must name its operation");
            const auto operation=Repository::operation(payload["operation_id"].get<std::string>());
            if(operation.spec.run_id!=id||operation.spec.tool!="run_process"||operation.state!=OperationState::executing)
                throw Conflict("Terminal output is not owned by an executing process operation");
        } else if(kind=="a2a.task_event") {
            if(!payload.contains("operation_id")||!payload["operation_id"].is_string()||!payload.contains("event")||!payload["event"].is_object())
                throw std::invalid_argument("A terminal A2A observation must identify its task operation and event");
            const auto operation=Repository::operation(payload["operation_id"].get<std::string>());
            if(operation.spec.run_id!=id||operation.spec.tool!="a2a_delegate"||
               (operation.state!=OperationState::succeeded&&operation.state!=OperationState::uncertain))
                throw Conflict("Terminal A2A observation is not owned by an acknowledged or uncertain task");
            Json binding;try{binding=Json::parse(operation.spec.arguments_json);}catch(...){throw DatabaseError("Stored A2A task binding is invalid");}
            if(!binding.is_object()||!binding.contains("agent_id")||!binding["agent_id"].is_string()||
               !payload.contains("agent_id")||!payload["agent_id"].is_string()||payload["agent_id"].get<std::string>()!=binding["agent_id"].get<std::string>())
                throw Conflict("Terminal A2A observation does not match its registered task owner");
            Json receipt;try{receipt=Json::parse(operation.result_json);}catch(...){throw DatabaseError("Stored A2A task receipt is invalid");}
            if(!receipt.is_object()||!receipt.contains("task_id")||!receipt["task_id"].is_string()||receipt["task_id"].get_ref<const std::string&>().empty())
                throw Conflict("Terminal A2A observation has no recorded task identity");
            const Json* task_event=&payload["event"];
            for(const auto* field:{"task","statusUpdate","status_update","artifactUpdate","artifact_update","message"})
                if(task_event->contains(field)&&(*task_event)[field].is_object()){task_event=&(*task_event)[field];break;}
            std::string observed_task_id;
            if(task_event->contains("id")&&(*task_event)["id"].is_string())observed_task_id=(*task_event)["id"].get<std::string>();
            if(task_event->contains("taskId")&&(*task_event)["taskId"].is_string())observed_task_id=(*task_event)["taskId"].get<std::string>();
            if(observed_task_id.empty()||observed_task_id!=receipt["task_id"].get<std::string>())
                throw Conflict("Terminal A2A observation does not match its recorded task");
            if(payload.contains("event_id")&&(!payload["event_id"].is_string()||payload["event_id"].get_ref<const std::string&>().size()>1024||
               payload["event_id"].get_ref<const std::string&>().find_first_of("\r\n")!=std::string::npos||payload["event_id"].get_ref<const std::string&>().find('\0')!=std::string::npos))
                throw std::invalid_argument("Terminal A2A observation has an invalid event cursor");
        } else throw Conflict("Run is terminal");
    }
    auto result=impl_->event(id,kind,json); transaction.commit(); return result;
}
bool Repository::append_a2a_task_event(const std::string& run_id,const std::string& operation_id,const std::string& json) {
    identifier(run_id);identifier(operation_id);
    Json payload;try{payload=Json::parse(object_json(json));}catch(...){throw std::invalid_argument("Invalid A2A task event");}
    if(!payload.is_object()||payload.value("operation_id",std::string{})!=operation_id||!payload.contains("agent_id")||!payload["agent_id"].is_string()||
       ((!payload.contains("event")||!payload["event"].is_object())&&(!payload.contains("data")||!payload["data"].is_object()))||
       (payload.contains("event")&&payload.contains("data")&&payload["event"]!=payload["data"]))
        throw std::invalid_argument("A2A task event does not identify its owner and event");
    const Json& task_event=payload.contains("event")?payload["event"]:payload["data"];
    std::string event_id;
    if(payload.contains("event_id")){
        if(!payload["event_id"].is_string())throw std::invalid_argument("A2A peer event cursor must be text");
        event_id=payload["event_id"].get<std::string>();
        if(event_id.empty()||event_id.size()>1024||event_id.find_first_of("\r\n")!=std::string::npos||event_id.find('\0')!=std::string::npos)
            throw std::invalid_argument("A2A peer event cursor is invalid");
    }
    auto& db=impl_->database;Transaction transaction(db);
    const auto op=operation(operation_id);
    if(op.spec.run_id!=run_id||op.spec.tool!="a2a_delegate"||
       (op.state!=OperationState::executing&&op.state!=OperationState::succeeded&&op.state!=OperationState::uncertain))
        throw Conflict("A2A task event is not owned by an active or acknowledged delegation");
    Json binding;try{binding=Json::parse(op.spec.arguments_json);}catch(...){throw DatabaseError("Stored A2A task binding is invalid");}
    if(!binding.is_object()||!binding.contains("agent_id")||!binding["agent_id"].is_string()||binding["agent_id"]!=payload["agent_id"])
        throw Conflict("A2A task event does not match its registered peer");
    const auto owner=run(run_id);
    if(terminal(owner.state)&&op.state!=OperationState::succeeded&&op.state!=OperationState::uncertain)
        throw Conflict("Terminal A2A observations require an acknowledged or uncertain task");
    std::string task_id;
    if(op.state==OperationState::succeeded||op.state==OperationState::uncertain){
        Json receipt;try{receipt=Json::parse(op.result_json);}catch(...){throw DatabaseError("Stored A2A task receipt is invalid");}
        if(!receipt.is_object()||!receipt.contains("task_id")||!receipt["task_id"].is_string())throw Conflict("A2A task event has no recorded task identity");
        task_id=receipt["task_id"].get<std::string>();
    }
    const Json* observed=&task_event;
    for(const auto* field:{"task","statusUpdate","status_update","artifactUpdate","artifact_update","message"})
        if(observed->contains(field)&&(*observed)[field].is_object()){observed=&(*observed)[field];break;}
    std::string observed_task;
    if(observed->contains("id")&&(*observed)["id"].is_string())observed_task=(*observed)["id"].get<std::string>();
    if(observed->contains("taskId")&&(*observed)["taskId"].is_string())observed_task=(*observed)["taskId"].get<std::string>();
    if(!task_id.empty()&&(observed_task.empty()||observed_task!=task_id))throw Conflict("A2A task event does not match its recorded task");
    if(!event_id.empty()){
        const auto rows=db.execute("SELECT payload FROM events WHERE run_id=? AND kind='a2a.task_event' AND json_extract(payload,'$.operation_id')=? AND json_extract(payload,'$.event_id')=? ORDER BY seq LIMIT 1",{run_id,operation_id,event_id}).rows;
        if(!rows.empty()){
            Json prior;try{prior=Json::parse(text(rows.front()[0]));}catch(...){throw DatabaseError("Stored A2A task event is invalid");}
            if(!prior.is_object())throw DatabaseError("Stored A2A task event is invalid");
            const Json* prior_event=prior.contains("event")?&prior["event"]:(prior.contains("data")?&prior["data"]:nullptr);
            if(!prior_event||*prior_event!=task_event)throw Conflict("A2A peer reused an event cursor for different task content");
            transaction.commit();return false;
        }
    }
    impl_->event(run_id,"a2a.task_event",json);transaction.commit();return true;
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
std::optional<std::string> Repository::latest_a2a_event_id(const std::string& run_id,const std::string& operation_id) {
    identifier(run_id);identifier(operation_id);const auto op=operation(operation_id);
    if(op.spec.run_id!=run_id||op.spec.tool!="a2a_delegate")throw NotFound("A2A task operation not found");
    const auto rows=impl_->database.execute("SELECT json_extract(payload,'$.event_id') FROM events WHERE run_id=? AND kind='a2a.task_event' AND json_extract(payload,'$.operation_id')=? AND json_type(payload,'$.event_id')='text' ORDER BY seq DESC LIMIT 1",{run_id,operation_id}).rows;
    if(rows.empty())return std::nullopt;const auto value=text(rows.front()[0]);if(value.empty()||value.size()>1024||value.find_first_of("\r\n")!=std::string::npos||value.find('\0')!=std::string::npos)throw DatabaseError("Stored A2A event cursor is invalid");return value;
}
ProcessOutputArtifact Repository::begin_process_output(const std::string& operation_id,const std::string& run_id){
    identifier(operation_id);identifier(run_id);auto& db=impl_->database;Transaction transaction(db);const auto op=operation(operation_id);
    if(op.spec.run_id!=run_id||op.spec.tool!="run_process"||op.state!=OperationState::executing)throw Conflict("Process output requires its claimed run_process operation");
    const auto owner=run(run_id);if(terminal(owner.state)){Json args;try{args=Json::parse(op.spec.arguments_json);}catch(...){throw DatabaseError("Saved process binding is invalid");}if(!args.value("background",false))throw Conflict("Only an owned background process can capture output after its run ends");}
    changed_one(db.execute("INSERT INTO process_output_artifacts(operation_id,run_id,state,stdout_bytes,stderr_bytes) VALUES(?,?,'capturing',0,0)",{operation_id,run_id}));transaction.commit();return {operation_id,run_id,"capturing",0,0};
}
ProcessOutputArtifact Repository::append_process_output(const std::string& operation_id,const std::string& run_id,bool stderr_channel,const std::string& bytes){
    identifier(operation_id);identifier(run_id);if(bytes.empty()||bytes.size()>8192)throw std::invalid_argument("Process output chunk exceeds its storage bound");auto& db=impl_->database;Transaction transaction(db);const auto op=operation(operation_id);
    if(op.spec.run_id!=run_id||op.spec.tool!="run_process"||op.state!=OperationState::executing)throw Conflict("Process output is not owned by an executing run_process operation");
    const auto rows=db.execute("SELECT state,stdout_bytes,stderr_bytes FROM process_output_artifacts WHERE operation_id=? AND run_id=?",{operation_id,run_id}).rows;if(rows.empty())throw NotFound("Process output artifact not found");if(text(rows[0][0])!="capturing")throw Conflict("Process output artifact is sealed");
    auto stdout_bytes=static_cast<std::uint64_t>(integer(rows[0][1])),stderr_bytes=static_cast<std::uint64_t>(integer(rows[0][2]));const auto offset=stderr_channel?stderr_bytes:stdout_bytes;
    constexpr std::uint64_t maximum=64u*1024u*1024u;if(stdout_bytes+stderr_bytes>maximum-bytes.size())throw Conflict("Process output artifact exceeded its 64 MiB capture limit");
    const auto channel=stderr_channel?"stderr":"stdout";const SqlBytes blob(bytes.begin(),bytes.end());changed_one(db.execute("INSERT INTO process_output_chunks(operation_id,channel,byte_offset,data) VALUES(?,?,?,?)",{operation_id,std::string(channel),static_cast<std::int64_t>(offset),blob}));
    if(stderr_channel)stderr_bytes+=bytes.size();else stdout_bytes+=bytes.size();changed_one(db.execute("UPDATE process_output_artifacts SET stdout_bytes=?,stderr_bytes=? WHERE operation_id=? AND state='capturing'",{static_cast<std::int64_t>(stdout_bytes),static_cast<std::int64_t>(stderr_bytes),operation_id}));
    transaction.commit();return {operation_id,run_id,"capturing",stdout_bytes,stderr_bytes};
}
ProcessOutputArtifact Repository::finish_process_output(const std::string& operation_id,const std::string& run_id,bool complete){
    identifier(operation_id);identifier(run_id);auto& db=impl_->database;Transaction transaction(db);const auto op=operation(operation_id);if(op.spec.run_id!=run_id||op.spec.tool!="run_process")throw NotFound("Process output artifact owner is invalid");
    const auto rows=db.execute("SELECT state,stdout_bytes,stderr_bytes FROM process_output_artifacts WHERE operation_id=? AND run_id=?",{operation_id,run_id}).rows;if(rows.empty())throw NotFound("Process output artifact not found");const auto state=std::string(text(rows[0][0]));const std::string desired=complete?"complete":"incomplete";
    if(state=="capturing"){changed_one(db.execute("UPDATE process_output_artifacts SET state=? WHERE operation_id=? AND run_id=? AND state='capturing'",{desired,operation_id,run_id}));}
    else if(state!=desired)throw Conflict("Process output artifact completion state changed");
    transaction.commit();return {operation_id,run_id,desired,static_cast<std::uint64_t>(integer(rows[0][1])),static_cast<std::uint64_t>(integer(rows[0][2]))};
}
ProcessOutputPage Repository::read_process_output(const std::string& operation_id,const std::string& channel,std::uint64_t offset,std::size_t limit){
    identifier(operation_id);if(channel!="stdout"&&channel!="stderr")throw std::invalid_argument("Invalid process output channel");if(limit<1||limit>262144||offset>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))throw std::invalid_argument("Invalid process output range");
    auto& db=impl_->database;Transaction transaction(db);const auto rows=db.execute("SELECT run_id,state,stdout_bytes,stderr_bytes FROM process_output_artifacts WHERE operation_id=?",{operation_id}).rows;if(rows.empty())throw NotFound("Process output artifact not found");
    ProcessOutputArtifact artifact{operation_id,text(rows[0][0]),text(rows[0][1]),static_cast<std::uint64_t>(integer(rows[0][2])),static_cast<std::uint64_t>(integer(rows[0][3]))};const auto total=channel=="stdout"?artifact.stdout_bytes:artifact.stderr_bytes;if(offset>total)throw std::invalid_argument("Process output offset exceeds captured bytes");
    const auto end=std::min<std::uint64_t>(total,offset+limit);std::string bytes;bytes.reserve(static_cast<std::size_t>(end-offset));
    for(const auto& row:db.execute("SELECT byte_offset,data FROM process_output_chunks WHERE operation_id=? AND channel=? AND byte_offset<? AND byte_offset+length(data)>? ORDER BY byte_offset",{operation_id,channel,static_cast<std::int64_t>(end),static_cast<std::int64_t>(offset)}).rows){const auto chunk_offset=static_cast<std::uint64_t>(integer(row[0]));const auto& blob=std::get<SqlBytes>(row[1]);const auto first=static_cast<std::size_t>(offset>chunk_offset?offset-chunk_offset:0),last=static_cast<std::size_t>(std::min<std::uint64_t>(blob.size(),end-chunk_offset));if(last>first)bytes.append(reinterpret_cast<const char*>(blob.data()+first),last-first);}
    if(bytes.size()!=end-offset)throw DatabaseError("Process output artifact has a gap");transaction.commit();return {std::move(artifact),channel,offset,total,std::move(bytes)};
}
void Repository::append_message(const std::string& id,const std::string& role,const std::string& json) {
    identifier(role); Transaction transaction(impl_->database); session(id);
    if(!impl_->database.execute("SELECT id FROM context_idle_owners WHERE session_id=? AND state='active'",{id}).rows.empty())throw Conflict("Session has an exclusive maintenance owner");
    changed_one(impl_->database.execute("INSERT INTO messages(session_id,role,payload) VALUES(?,?,?)",{id,role,json}));
    transaction.commit();
}
SessionInboxItem Repository::enqueue_session_input(const std::string& id,const std::string& session_id,const std::string& content,const std::string& files_json,const std::string& workspace_id,std::int64_t workspace_revision) {
    identifier(id);bounded_identity(session_id,128);if(content.empty()||content.size()>1024*1024||content.find('\0')!=std::string::npos)throw std::invalid_argument("Queued session input must contain 1-1048576 UTF-8 bytes");
    Json files;try{files=Json::parse(files_json);}catch(...){throw std::invalid_argument("Queued attachment metadata is invalid");}
    if(!files.is_array()||files.size()>32||files_json.size()>262144||workspace_id.size()>256||workspace_id.find('\0')!=std::string::npos||workspace_revision<0||workspace_revision>9007199254740991LL||(!files.empty()&&(workspace_id.empty()||workspace_revision<1)))throw std::invalid_argument("Queued attachments require valid workspace admission metadata");
    for(const auto& file:files)if(!file.is_object()||!file.contains("path")||!file["path"].is_string()||file["path"].get<std::string>().empty()||!file.contains("folder_id")||!file["folder_id"].is_string())throw std::invalid_argument("Queued attachment metadata is invalid");
    auto& db=impl_->database;Transaction transaction(db);session(session_id);
    if(!db.execute("SELECT id FROM context_idle_owners WHERE session_id=? AND state='active'",{session_id}).rows.empty())throw Conflict("Session has an exclusive maintenance owner");
    const auto existing=db.execute("SELECT session_id,content,state,created_ms,run_id,files_json,workspace_id,workspace_revision FROM session_inbox WHERE id=?",{id}).rows;
    if(!existing.empty()){
        if(text(existing[0][0])!=session_id||text(existing[0][1])!=content||text(existing[0][5])!=files_json||text(existing[0][6])!=workspace_id||integer(existing[0][7])!=workspace_revision)throw Conflict("Queued session input identity was reused with different content");
        if(text(existing[0][2])=="cancelled")throw Conflict("Cancelled session input identity cannot be reused");
        SessionInboxItem item{id,session_id,content,text(existing[0][2]),{},integer(existing[0][3]),files_json,workspace_id,workspace_revision};if(!std::holds_alternative<std::nullptr_t>(existing[0][4]))item.run_id=text(existing[0][4]);transaction.commit();return item;
    }
    const auto pending=db.execute("SELECT count(*),coalesce(sum(length(content)+length(files_json)),0) FROM session_inbox WHERE session_id=? AND state='queued'",{session_id}).rows;
    if(integer(pending[0][0])>=32||integer(pending[0][1])+static_cast<std::int64_t>(content.size()+files_json.size())>4*1024*1024)throw RunBusy("Session input queue is full");
    const auto created=now_ms();changed_one(db.execute("INSERT INTO session_inbox(id,session_id,content,state,created_ms,files_json,workspace_id,workspace_revision) VALUES(?,?,?,'queued',?,?,?,?)",{id,session_id,content,created,files_json,workspace_id,workspace_revision}));transaction.commit();return {id,session_id,content,"queued",{},created,files_json,workspace_id,workspace_revision};
}
std::vector<SessionInboxItem> Repository::session_inbox(const std::string& session_id) {
    bounded_identity(session_id,128);session(session_id);std::vector<SessionInboxItem> items;
    for(const auto& row:impl_->database.execute("SELECT id,content,state,created_ms,run_id,files_json,workspace_id,workspace_revision FROM session_inbox WHERE session_id=? AND state!='cancelled' ORDER BY created_ms,id LIMIT 128",{session_id}).rows){SessionInboxItem item{text(row[0]),session_id,text(row[1]),text(row[2]),{},integer(row[3]),text(row[5]),text(row[6]),integer(row[7])};if(!std::holds_alternative<std::nullptr_t>(row[4]))item.run_id=text(row[4]);items.push_back(std::move(item));}
    return items;
}
void Repository::cancel_session_input(const std::string& session_id,const std::string& input_id) {
    bounded_identity(session_id,128);identifier(input_id);auto& db=impl_->database;Transaction transaction(db);session(session_id);
    const auto existing=db.execute("SELECT state FROM session_inbox WHERE id=? AND session_id=?",{input_id,session_id}).rows;
    if(existing.empty())throw NotFound("Queued session input not found");
    const auto state=text(existing[0][0]);if(state=="cancelled"||state=="claimed"){transaction.commit();return;}
    changed_one(db.execute("UPDATE session_inbox SET state='cancelled' WHERE id=? AND session_id=? AND state='queued'",{input_id,session_id}));transaction.commit();
}
Message Repository::message(const std::string& id,std::int64_t sequence) {
    if(sequence<1||sequence>9007199254740991LL)throw std::invalid_argument("Invalid message sequence");
    session(id);
    const auto rows=impl_->database.execute("SELECT seq,role,payload FROM messages WHERE session_id=? AND seq=? AND execution_run_id IS NULL",{id,sequence}).rows;
    if(rows.empty())throw NotFound("Message not found");
    if(rows.size()!=1)throw DatabaseError("Message sequence is not unique");
    return {integer(rows[0][0]),text(rows[0][1]),text(rows[0][2])};
}
std::vector<Message> Repository::history(const std::string& id) {
    session(id); std::vector<Message> result;
    for(const auto& row:impl_->database.execute("SELECT seq,role,payload FROM messages WHERE session_id=? AND execution_run_id IS NULL ORDER BY seq",{id}).rows)
        result.push_back({integer(row[0]),text(row[1]),text(row[2])});
    return result;
}
MessagePage Repository::history_page(const std::string& id,std::optional<std::int64_t> before_sequence,std::size_t count) {
    if(count<1||count>1000)throw std::invalid_argument("Message page size must be between 1 and 1000");
    session(id);if(before_sequence)message(id,*before_sequence);
    const auto rows=before_sequence
        ?impl_->database.execute("SELECT seq,role,payload FROM messages WHERE session_id=? AND execution_run_id IS NULL AND seq<? ORDER BY seq DESC LIMIT ?",{id,*before_sequence,static_cast<std::int64_t>(count+1)}).rows
        :impl_->database.execute("SELECT seq,role,payload FROM messages WHERE session_id=? AND execution_run_id IS NULL ORDER BY seq DESC LIMIT ?",{id,static_cast<std::int64_t>(count+1)}).rows;
    MessagePage page;const bool more=rows.size()>count;const auto included=std::min(rows.size(),count);
    page.messages.reserve(included);
    for(std::size_t i=0;i<included;++i)page.messages.push_back({integer(rows[i][0]),text(rows[i][1]),text(rows[i][2])});
    if(more&&!page.messages.empty())page.next_before_sequence=page.messages.back().sequence;
    std::reverse(page.messages.begin(),page.messages.end());return page;
}
namespace {
constexpr const char* backend_owner_category="native-backend-owner";
void owner_identity(const std::string& value){if(value.size()!=32||value.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("Invalid native owner identity");}
void owner_revision(std::int64_t value){if(value<1||value>=9007199254740991LL)throw std::invalid_argument("Invalid native owner revision");}
void owner_text(const std::string& value,std::size_t bound){if(value.empty()||value.size()>bound||std::any_of(value.begin(),value.end(),[](unsigned char c){return c<32;}))throw std::invalid_argument("Invalid native owner binding");}
void owner_hash(const std::string& value,std::size_t size){if(value.size()!=size||value.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("Invalid native owner digest");}
Json target_json(const BackendOwnerTarget& t){const auto& r=t.runtime;owner_text(r.root,32768);owner_text(r.root_identity,256);owner_hash(r.manifest_sha256,64);owner_hash(r.native_revision,40);owner_hash(r.sdk_revision,40);owner_hash(r.source_manifest_sha256,64);owner_hash(r.server_sha256,64);owner_text(t.workspace_root,32768);owner_text(t.workspace_id,256);owner_hash(t.auth_binding,64);return {{"runtime",{{"root",r.root},{"root_identity",r.root_identity},{"manifest_sha256",r.manifest_sha256},{"native_revision",r.native_revision},{"sdk_revision",r.sdk_revision},{"source_manifest_sha256",r.source_manifest_sha256},{"server_sha256",r.server_sha256}}},{"workspace_root",t.workspace_root},{"workspace_id",t.workspace_id},{"auth_binding",t.auth_binding},{"approved_edits",t.approved_edits}};}
BackendOwnerTarget decode_target(const Json& j){const auto& r=j.at("runtime");BackendOwnerTarget t{{r.at("root").get<std::string>(),r.at("root_identity").get<std::string>(),r.at("manifest_sha256").get<std::string>(),r.at("native_revision").get<std::string>(),r.at("sdk_revision").get<std::string>(),r.at("source_manifest_sha256").get<std::string>(),r.at("server_sha256").get<std::string>()},j.at("workspace_root").get<std::string>(),j.at("workspace_id").get<std::string>(),j.at("auth_binding").get<std::string>(),j.at("approved_edits").get<bool>()};target_json(t);return t;}
Json receipt_json(const BackendOwnerReceipt& r){owner_identity(r.generation);owner_identity(r.receipt_id);owner_revision(r.revision);return {{"generation",r.generation},{"receipt_id",r.receipt_id},{"revision",r.revision}};}
Json owner_json(const BackendOwnerState& s){
    owner_identity(s.generation);owner_revision(s.revision);
    if(s.quiesced)owner_identity(s.receipt_id);else if(!s.receipt_id.empty()||s.retirement_requested||s.replacement_prepared||s.replacement_target)throw std::invalid_argument("Active owner cannot retain closed lifecycle state");
    if(s.replacement_prepared&&(!s.quiesced||s.retirement_requested||!s.replacement_target||(!s.replacement_source&&s.legacy_ticket_id.empty())))throw std::invalid_argument("Invalid prepared replacement state");
    if(s.replacement_target){if(!s.retirement_requested&&!s.replacement_prepared)throw std::invalid_argument("Target requires closed handoff state");owner_text(s.database_path,32768);}else if(!s.database_path.empty()||s.replacement_source)throw std::invalid_argument("Unbound replacement state");
    if(s.replacement_target&&!s.replacement_source&&s.legacy_ticket_id.empty())throw std::invalid_argument("Replacement target requires its native source receipt or operator ticket");
    if(!s.legacy_ticket_id.empty()){owner_identity(s.legacy_ticket_id);if(!s.replacement_prepared||s.replacement_source||s.retirement_requested)throw std::invalid_argument("Invalid legacy prepared owner");}
    if(s.retirement_requested&&s.replacement_source&&(s.replacement_source->generation!=s.generation||s.replacement_source->receipt_id!=s.receipt_id||s.replacement_source->revision+1!=s.revision))throw std::invalid_argument("Retirement source differs from consumed native receipt");
    if(s.replacement_prepared&&s.replacement_source&&(s.replacement_source->generation==s.generation||s.replacement_source->revision>=s.revision-1))throw std::invalid_argument("Prepared replacement source is invalid");
    Json result{{"version",!s.legacy_ticket_id.empty()?5:s.replacement_prepared?4:s.replacement_target?3:s.retirement_requested?2:1},{"generation",s.generation},{"revision",s.revision},{"quiesced",s.quiesced},{"receipt_id",s.receipt_id}};
    if(s.retirement_requested||s.replacement_prepared)result["retirement_requested"]=s.retirement_requested;
    if(s.replacement_target){result["replacement_target"]=target_json(*s.replacement_target);result["database_path"]=s.database_path;}
    if(s.replacement_source)result["replacement_source"]=receipt_json(*s.replacement_source);
    if(s.replacement_prepared)result["replacement_prepared"]=true;
    if(!s.legacy_ticket_id.empty())result["legacy_ticket_id"]=s.legacy_ticket_id;
    return result;
}
BackendOwnerState decode_owner(const std::string& raw){
    try {if(raw.size()>128*1024)throw std::invalid_argument("Owner record exceeds limits");const auto j=Json::parse(raw);BackendOwnerState s{j.at("generation").get<std::string>(),j.at("revision").get<std::int64_t>(),j.at("quiesced").get<bool>(),j.at("receipt_id").get<std::string>()};const auto version=j.at("version").get<int>();if(version>=2)s.retirement_requested=j.at("retirement_requested").get<bool>();if(version>=3){s.replacement_target=decode_target(j.at("replacement_target"));s.database_path=j.at("database_path").get<std::string>();if(version==5)s.legacy_ticket_id=j.at("legacy_ticket_id").get<std::string>();else{const auto& r=j.at("replacement_source");s.replacement_source=BackendOwnerReceipt{r.at("generation").get<std::string>(),r.at("receipt_id").get<std::string>(),r.at("revision").get<std::int64_t>()};}}if(version>=4)s.replacement_prepared=j.at("replacement_prepared").get<bool>();if(owner_json(s).dump()!=raw)throw std::invalid_argument("Owner record differs from canonical typed state");return s;}
    catch(...){throw DatabaseError("Saved native owner state is invalid");}
}
std::optional<std::string> owner_record(XlangSqlite& db){const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id='owner'",{std::string(backend_owner_category)}).rows;return rows.empty()?std::optional<std::string>{}:text(rows[0][0]);}
void write_owner(XlangSqlite& db,const BackendOwnerState& s,const std::optional<std::string>& previous){const auto raw=owner_json(s).dump();const auto written=previous?db.execute("UPDATE information SET payload=? WHERE category=? AND id='owner' AND payload=?",{raw,std::string(backend_owner_category),*previous}):db.execute("INSERT INTO information(category,id,payload) VALUES(?,'owner',?) ON CONFLICT(category,id) DO NOTHING",{std::string(backend_owner_category),raw});if(written.affected_rows!=1)throw Conflict("Native owner changed before publication");}
void require_database_idle(XlangSqlite& db,bool allow_refresh=false){
    if(!allow_refresh)for(const auto& r:refresh_records(db))if(r.state=="prepared"||r.state=="dispatched")throw Conflict("Native MCP refresh ownership is still active");
    // All predicates are native literals. Include unfinished effect and context
    // ownership even if a corrupt/failed root claims to be terminal.
    const auto require_empty=[&](const char* sql,const char* owner){if(!db.execute(sql).rows.empty())throw Conflict(std::string("Native backend upgrade is blocked by ")+owner);};
    require_empty("SELECT id FROM information WHERE category='native-agent-worker-leases' AND json_extract(payload,'$.state')='active' LIMIT 1","an active native worker lease");
    require_empty("SELECT id FROM information WHERE category='native-agent-worker-model-claims' AND json_extract(payload,'$.state')='dispatching' LIMIT 1","an active worker model dispatch");
    require_empty("SELECT id FROM information WHERE category='native-agent-worker-read-claims' AND json_extract(payload,'$.state') IN ('awaiting_dispatch','executing') LIMIT 1","an active worker read");
    require_empty("SELECT id FROM information WHERE category='native-agent-worker-file-claims' AND json_extract(payload,'$.state') IN ('awaiting_dispatch','executing') LIMIT 1","an active worker file call");
    require_empty("SELECT id FROM information WHERE category='native-agent-worker-process-claims' AND json_extract(payload,'$.state') IN ('awaiting_dispatch','executing') LIMIT 1","an active worker process call");
    require_empty("SELECT id FROM information WHERE category='native-agent-worker-remote-claims' AND json_extract(payload,'$.state') IN ('awaiting_dispatch','executing') LIMIT 1","an active worker remote call");
    require_empty("SELECT id FROM information WHERE category='native-agent-worker-callable-claims' AND json_extract(payload,'$.state') IN ('awaiting_dispatch','executing') LIMIT 1","an active worker callable call");
    require_empty("SELECT id FROM runs WHERE state IN ('queued','running','paused') LIMIT 1","a queued, running or paused run");
    require_empty("SELECT attempt_id FROM agent_model_call_reservations WHERE state IN ('reserved','started') LIMIT 1","an active model call");
    require_empty("SELECT id FROM delegation_batches WHERE state IN ('accepted','working') LIMIT 1","an active delegation batch");
    require_empty("SELECT id FROM dynamic_plans WHERE state IN ('active','waiting_human','uncertain') LIMIT 1","an unresolved dynamic plan");
    require_empty("SELECT label FROM dynamic_plan_nodes WHERE state IN ('claimed','waiting_human','uncertain') LIMIT 1","an unresolved graph node");
    require_empty("SELECT id FROM dynamic_plan_calls WHERE state IN ('accepted','report_ready') LIMIT 1","an uncommitted plan call");
    require_empty("SELECT id FROM agent_budget_segments WHERE state='open' LIMIT 1","an open agent budget segment");
    require_empty("SELECT root_run_id FROM dynamic_owner_pauses WHERE state='paused' LIMIT 1","a paused dynamic owner");
    require_empty("SELECT id FROM context_idle_owners WHERE state='active' LIMIT 1","an active context maintenance owner");
    require_empty("SELECT id FROM context_compactions WHERE state IN ('reserved','started','received') LIMIT 1","an active context compaction");
    require_empty("SELECT id FROM context_manual_requests WHERE state IN ('pending','claimed') LIMIT 1","a pending context request");
    require_empty("SELECT id FROM context_measures WHERE state IN ('reserved','started') LIMIT 1","an active context measurement");
    require_empty("SELECT root_run_id FROM inference_steps WHERE state IN ('reserved','started') LIMIT 1","an active inference step");
    const bool has_process_output=!db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='process_output_artifacts'").rows.empty();
    const auto unresolved=has_process_output
        ?db.execute("SELECT o.id FROM operations o LEFT JOIN runs r ON r.id=o.run_id LEFT JOIN process_output_artifacts a ON a.operation_id=o.id WHERE o.state IN ('awaiting_approval','ready','executing','uncertain') AND NOT (o.state='uncertain' AND o.tool='run_process' AND r.state IN ('failed','cancelled') AND json_valid(o.result)=1 AND json_extract(o.result,'$.process_tree_retired')=1 AND json_type(o.result,'$.output_artifact')='object' AND json_extract(o.result,'$.output_artifact.operation_id')=o.id AND json_extract(o.result,'$.output_artifact.state')='complete' AND a.run_id=o.run_id AND a.state='complete') LIMIT 1")
        :db.execute("SELECT id FROM operations WHERE state IN ('awaiting_approval','ready','executing','uncertain') LIMIT 1");
    if(!unresolved.rows.empty())throw Conflict("Native backend upgrade is blocked by an unresolved operation");
}
void require_bootstrap(XlangSqlite& db,const BackendLease& lease,const BackendOwnerState& prior,const BackendOwnerBootstrap& bootstrap){
    receipt_json(bootstrap.receipt);target_json(bootstrap.target);
    const auto source=prior.replacement_source;
    if(!prior.quiesced||(!prior.retirement_requested&&!prior.replacement_prepared)||!prior.replacement_target||!source||*source!=bootstrap.receipt||*prior.replacement_target!=bootstrap.target||prior.database_path!=lease.canonical_database_path())throw Conflict("Replacement receipt, target or database binding changed");
    require_database_idle(db);
}
BackendOwnerState stage_backend_owner(XlangSqlite& db,const BackendLease& lease,const std::string& generation,const BackendOwnerBootstrap* bootstrap){
    owner_identity(generation);const auto previous=owner_record(db);std::int64_t revision=1;
    if(bootstrap&&generation==bootstrap->receipt.generation)throw Conflict("Replacement must bind a new native generation");
    if(previous){const auto prior=decode_owner(*previous);if(bootstrap)require_bootstrap(db,lease,prior,*bootstrap);else if(prior.quiesced)throw BackendQuiesced("A durable native quiescence receipt requires explicit generation handoff; normal startup cannot reopen admission");owner_revision(prior.revision+1);revision=prior.revision+1;}
    else if(bootstrap)throw Conflict("Replacement source owner is absent");
    BackendOwnerState current{generation,revision,false,{}};
    if(bootstrap){current.quiesced=true;current.receipt_id=bootstrap->receipt.receipt_id;current.replacement_target=bootstrap->target;current.replacement_prepared=true;current.replacement_source=bootstrap->receipt;current.database_path=lease.canonical_database_path();}
    write_owner(db,current,previous);refresh_recover(db,generation);return current;
}
BackendOwnerState stage_legacy_owner(XlangSqlite& db,const BackendLease& lease,const std::string& generation,const LegacyOwnerBootstrap& legacy){
    owner_identity(generation);require_legacy_owner_bootstrap(db,lease,legacy);const auto previous=owner_record(db);std::int64_t revision=1;
    if(previous){const auto prior=decode_owner(*previous);if(prior.quiesced&&prior.legacy_ticket_id!=legacy.ticket_id)throw Conflict("Legacy operator ticket does not own the prepared generation");if(prior.generation==generation)throw Conflict("Legacy replacement must bind a fresh generation");revision=prior.revision+1;owner_revision(revision);}
    BackendOwnerState current{generation,revision,true,generation};current.replacement_target=legacy.target;current.replacement_prepared=true;current.database_path=lease.canonical_database_path();current.legacy_ticket_id=legacy.ticket_id;write_owner(db,current,previous);refresh_recover(db,generation);return current;
}
}
std::string encode_backend_owner_target(const BackendOwnerTarget& target){return target_json(target).dump();}
BackendOwnerTarget decode_backend_owner_target(const std::string& raw){const auto target=decode_target(Json::parse(raw));if(target_json(target).dump()!=raw)throw std::invalid_argument("Native target differs from canonical typed state");return target;}
BackendOwnerState Repository::open_backend_owner(const BackendLease& lease,const std::string& generation,const BackendOwnerBootstrap* bootstrap){
    if(!lease.covers(impl_->path))throw Conflict("Native owner requires this database lease");auto& db=impl_->database;Transaction tx(db);auto current=stage_backend_owner(db,lease,generation,bootstrap);tx.commit();return current;
}
BackendOwnerState Repository::backend_owner(){const auto raw=owner_record(impl_->database);if(!raw)throw NotFound("Native database owner is not initialized");return decode_owner(*raw);}
BackendOwnerState Repository::quiesce_backend_owner(const BackendLease& lease,const BackendOwnerPrecondition& expected,const std::string& receipt){
    if(!lease.covers(impl_->path))throw Conflict("Quiescence requires this database lease");owner_identity(expected.generation);owner_revision(expected.revision);owner_identity(receipt);auto& db=impl_->database;Transaction tx(db);const auto previous=owner_record(db);if(!previous)throw NotFound("Native owner is absent");auto current=decode_owner(*previous);
    if(current.generation!=expected.generation||current.revision!=expected.revision||current.quiesced)throw Conflict("Native quiescence precondition changed");require_database_idle(db);owner_revision(current.revision+1);++current.revision;current.quiesced=true;current.receipt_id=receipt;write_owner(db,current,previous);tx.commit();return current;
}
BackendOwnerState Repository::resume_backend_owner(const BackendLease& lease,const BackendOwnerReceipt& expected){
    if(!lease.covers(impl_->path))throw Conflict("Resume requires this database lease");owner_identity(expected.generation);owner_identity(expected.receipt_id);owner_revision(expected.revision);auto& db=impl_->database;Transaction tx(db);const auto previous=owner_record(db);if(!previous)throw NotFound("Native owner is absent");auto current=decode_owner(*previous);
    if(!current.quiesced||current.retirement_requested||current.replacement_prepared||current.generation!=expected.generation||current.receipt_id!=expected.receipt_id||current.revision!=expected.revision)throw Conflict("Native resume receipt changed or handoff is pending");require_database_idle(db);owner_revision(current.revision+1);++current.revision;current.quiesced=false;current.receipt_id.clear();write_owner(db,current,previous);tx.commit();return current;
}
BackendOwnerState Repository::request_backend_retirement(const BackendLease& lease,const BackendOwnerReceipt& expected,std::optional<BackendOwnerTarget> target){
    if(!lease.covers(impl_->path))throw Conflict("Retirement requires this database lease");
    owner_identity(expected.generation);owner_identity(expected.receipt_id);owner_revision(expected.revision);
    auto& db=impl_->database;Transaction tx(db);const auto previous=owner_record(db);if(!previous)throw NotFound("Native owner is absent");auto current=decode_owner(*previous);
    if(!current.quiesced||current.retirement_requested||current.replacement_prepared||current.generation!=expected.generation||current.receipt_id!=expected.receipt_id||current.revision!=expected.revision)throw Conflict("Native retirement receipt changed");
    if(target){target_json(*target);current.replacement_target=std::move(target);current.database_path=lease.canonical_database_path();current.replacement_source=expected;}
    require_database_idle(db);owner_revision(current.revision+1);++current.revision;current.retirement_requested=true;write_owner(db,current,previous);tx.commit();return current;
}
BackendOwnerState Repository::activate_backend_replacement(const BackendLease& lease,const BackendOwnerReceipt& expected,const BackendOwnerTarget& target){
    if(!lease.covers(impl_->path))throw Conflict("Activation requires this database lease");receipt_json(expected);target_json(target);auto& db=impl_->database;Transaction tx(db);const auto previous=owner_record(db);if(!previous)throw NotFound("Native owner is absent");const auto prior=decode_owner(*previous);
    if(!prior.quiesced)throw Conflict("Prepared replacement owner is not quiesced");
    if(!prior.replacement_prepared)throw Conflict("Prepared replacement was not marked ready");
    if(prior.retirement_requested)throw Conflict("Prepared replacement owner is still retiring");
    if(!prior.replacement_target)throw Conflict("Prepared replacement target is absent");
    if(*prior.replacement_target!=target)throw Conflict("Prepared replacement target changed");
    if(prior.database_path!=lease.canonical_database_path())throw Conflict("Prepared replacement database changed");
    if(BackendOwnerReceipt{prior.generation,prior.receipt_id,prior.revision}!=expected)throw Conflict("Prepared replacement receipt changed");
    require_database_idle(db);if(!prior.legacy_ticket_id.empty()){verify_legacy_owner_saved_records(db);consume_legacy_owner_ticket(db,prior.legacy_ticket_id);}owner_revision(prior.revision+1);BackendOwnerState active{prior.generation,prior.revision+1,false,{}};write_owner(db,active,previous);tx.commit();return active;
}

namespace {
constexpr const char* worker_lease_category="native-agent-worker-leases";
constexpr const char* worker_current_category="native-agent-worker-current";
constexpr const char* worker_claim_category="native-agent-worker-model-claims";
constexpr const char* worker_event_category="native-agent-worker-model-events";
constexpr const char* worker_read_category="native-agent-worker-read-claims";
constexpr const char* worker_file_category="native-agent-worker-file-claims";
constexpr const char* worker_file_effect_category="native-agent-worker-file-effects";
constexpr const char* worker_remote_category="native-agent-worker-remote-claims";
constexpr const char* worker_callable_category="native-agent-worker-callable-claims";
constexpr const char* worker_remote_effect_category="native-agent-worker-remote-effects";
constexpr const char* worker_callable_effect_category="native-agent-worker-callable-effects";
constexpr const char* worker_process_category="native-agent-worker-process-claims";
constexpr const char* worker_process_effect_category="native-agent-worker-process-effects";
constexpr const char* worker_turn_category="native-agent-worker-tool-turns";
constexpr const char* worker_tool_event_category="native-agent-worker-tool-events";
bool worker_private_category(const std::string& category){return category.starts_with("native-agent-worker-");}
void worker_digest(const std::string& value){if(value.size()!=64||value.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("Invalid worker payload binding");}
Json worker_spec_json(const AgentWorkerLeaseSpec& spec){
    bounded_identity(spec.generation);bounded_identity(spec.root_run_id);bounded_identity(spec.owner_run_id);bounded_identity(spec.workspace_id,4096);
    if(spec.workspace_revision<1||spec.workspace_revision>9007199254740991||!spec.pid||spec.creation_time.empty()||spec.creation_time.size()>20||spec.creation_time[0]=='0'||spec.creation_time.find_first_not_of("0123456789")!=std::string::npos||spec.executable_identity.empty()||spec.executable_identity.size()>4096||spec.executable_identity.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos)throw std::invalid_argument("Invalid captured worker process identity");
    return {{"generation",spec.generation},{"root_run_id",spec.root_run_id},{"owner_run_id",spec.owner_run_id},{"workspace_id",spec.workspace_id},{"workspace_revision",spec.workspace_revision},{"pid",spec.pid},{"creation_time",spec.creation_time},{"executable_identity",spec.executable_identity},{"provider_identity",Json::parse(object_json(spec.provider_identity_json,4096))}};
}
Json worker_lease_json(const AgentWorkerLeaseRecord& record){return {{"version",1},{"spec",worker_spec_json(record.spec)},{"state",record.state},{"retirement_reason",record.retirement_reason},{"revision",record.revision}};}
AgentWorkerLeaseRecord read_worker_lease(XlangSqlite& db,const std::string& generation){
    bounded_identity(generation);const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_lease_category),generation}).rows;if(rows.empty())throw NotFound("Worker lease not found");
    try{const auto value=Json::parse(text(rows[0][0]));const auto& s=value.at("spec");AgentWorkerLeaseRecord record;
        record.spec={s.at("generation").get<std::string>(),s.at("root_run_id").get<std::string>(),s.at("owner_run_id").get<std::string>(),s.at("workspace_id").get<std::string>(),s.at("workspace_revision").get<std::int64_t>(),s.at("pid").get<std::uint32_t>(),s.at("creation_time").get<std::string>(),s.at("executable_identity").get<std::string>(),s.at("provider_identity").dump()};record.state=value.at("state").get<std::string>();record.retirement_reason=value.at("retirement_reason").get<std::string>();record.revision=value.at("revision").get<std::int64_t>();
        if(record.spec.generation!=generation||record.revision<1||record.revision>9007199254740991||(record.state!="active"&&record.state!="retired")||(record.state=="active")!=record.retirement_reason.empty()||worker_lease_json(record)!=value)throw DatabaseError("Invalid stored worker lease");return record;
    }catch(const Json::exception&){throw DatabaseError("Invalid stored worker lease");}catch(const std::invalid_argument&){throw DatabaseError("Invalid stored worker lease");}
}
Json worker_claim_spec_json(const AgentWorkerModelClaimSpec& spec){bounded_identity(spec.generation);bounded_identity(spec.root_run_id);bounded_identity(spec.owner_run_id);bounded_identity(spec.attempt_id);worker_digest(spec.request_binding);return {{"generation",spec.generation},{"root_run_id",spec.root_run_id},{"owner_run_id",spec.owner_run_id},{"attempt_id",spec.attempt_id},{"request_binding",spec.request_binding}};}
std::string worker_claim_key(const std::string& root,const std::string& attempt){bounded_identity(root);bounded_identity(attempt);return context_digest(Json::array({root,attempt}).dump());}
std::string worker_event_key(const std::string& root,const std::string& attempt,std::int64_t ordinal){bounded_identity(root);bounded_identity(attempt);if(ordinal<0||ordinal>=8192)throw std::invalid_argument("Invalid worker model event ordinal");return context_digest(Json::array({root,attempt,ordinal}).dump());}
Json worker_claim_json(const AgentWorkerModelClaimRecord& record){Json value={{"version",1},{"spec",worker_claim_spec_json(record.spec)},{"state",record.state},{"actual_response_json",record.actual_response_json},{"response_binding",record.response_binding}};if(!record.actual_assistant_json.empty()){value["actual_assistant_json"]=record.actual_assistant_json;value["assistant_binding"]=record.assistant_binding;}return value;}
AgentWorkerModelClaimRecord read_worker_claim(XlangSqlite& db,const std::string& root,const std::string& attempt){
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_claim_category),worker_claim_key(root,attempt)}).rows;if(rows.empty())throw NotFound("Worker model dispatch not found");
    try{const auto value=Json::parse(text(rows[0][0]));const auto& s=value.at("spec");AgentWorkerModelClaimRecord record{{s.at("generation").get<std::string>(),s.at("root_run_id").get<std::string>(),s.at("owner_run_id").get<std::string>(),s.at("attempt_id").get<std::string>(),s.at("request_binding").get<std::string>()},value.at("state").get<std::string>(),value.at("actual_response_json").get<std::string>(),value.at("response_binding").get<std::string>()};
        record.actual_assistant_json=value.value("actual_assistant_json",std::string{});record.assistant_binding=value.value("assistant_binding",std::string{});
        if(record.spec.root_run_id!=root||record.spec.attempt_id!=attempt||(record.state!="dispatching"&&record.state!="responded"&&record.state!="uncertain"&&record.state!="rejected")||worker_claim_json(record)!=value)throw DatabaseError("Invalid stored worker dispatch");
        if(!record.actual_assistant_json.empty()){if(record.state!="responded"||record.assistant_binding!=context_digest(record.actual_assistant_json))throw DatabaseError("Stored worker assistant binding differs");(void)object_json(record.actual_assistant_json,8*1024*1024);}else if(!record.assistant_binding.empty())throw DatabaseError("Worker assistant binding has no receipt");
        if(record.state=="responded"){(void)object_json(record.actual_response_json,8*1024*1024);if(record.response_binding!=context_digest(record.actual_response_json))throw DatabaseError("Stored worker response binding differs");}
        else if(!record.actual_response_json.empty()||!record.response_binding.empty())throw DatabaseError("Unacknowledged worker dispatch has a response");return record;
    }catch(const Json::exception&){throw DatabaseError("Invalid stored worker dispatch");}catch(const std::invalid_argument&){throw DatabaseError("Invalid stored worker dispatch");}
}
constexpr std::string_view worker_planning_category="native-agent-worker-planning-claims";
Json worker_planning_json(const AgentWorkerPlanningClaimRecord& record){auto value=Json{{"version",1},{"model",worker_claim_spec_json(record.model)},{"call_id",record.call_id},{"tool_name",record.tool_name},{"arguments_json",record.arguments_json},{"backend_identity",record.backend_identity},{"segment_id",record.segment_id},{"state",record.state}};if(!record.native_call_id.empty())value["native_call_id"]=record.native_call_id;if(!record.rejection_code.empty())value["rejection_code"]=record.rejection_code;return value;}
AgentWorkerPlanningClaimRecord read_worker_planning(XlangSqlite& db,const std::string& root,const std::string& attempt){
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_planning_category),worker_claim_key(root,attempt)}).rows;if(rows.empty())throw NotFound("Worker planning admission not found");
    try{const auto value=Json::parse(text(rows[0][0]));const auto& m=value.at("model");AgentWorkerPlanningClaimRecord record{{m.at("generation").get<std::string>(),m.at("root_run_id").get<std::string>(),m.at("owner_run_id").get<std::string>(),m.at("attempt_id").get<std::string>(),m.at("request_binding").get<std::string>()},value.at("call_id").get<std::string>(),value.at("tool_name").get<std::string>(),value.at("arguments_json").get<std::string>(),value.at("backend_identity").get<std::string>(),value.at("segment_id").get<std::string>(),value.at("state").get<std::string>()};
        record.native_call_id=value.value("native_call_id",std::string{});record.rejection_code=value.value("rejection_code",std::string{});
        if(record.model.root_run_id!=root||record.model.attempt_id!=attempt||(record.state!="entered"&&record.state!="uncertain"&&record.state!="accepted"&&record.state!="rejected"&&record.state!="inspected")||(record.tool_name!="plan_tasks"&&record.tool_name!="revise_plan"&&record.tool_name!="inspect_plan")||worker_planning_json(record)!=value)throw DatabaseError("Invalid stored worker planning admission");bounded_identity(record.call_id,256);worker_digest(record.backend_identity);bounded_identity(record.segment_id);(void)object_json(record.arguments_json,4*1024*1024);if(record.state=="accepted"){bounded_identity(record.native_call_id,128);if(!record.rejection_code.empty())throw DatabaseError("Accepted planning has a rejection");const auto accepted=db.execute("SELECT id FROM dynamic_plan_calls WHERE id=? AND root_run_id=? AND owner_run_id=? AND origin_attempt_id=? AND provider_tool_call_id=? AND arguments_json=?",{record.native_call_id,root,record.model.owner_run_id,attempt,record.call_id,record.arguments_json}).rows;if(accepted.size()!=1)throw DatabaseError("Stored worker planning native acceptance differs");}else if(record.state=="inspected"){if(record.tool_name!="inspect_plan"||!record.native_call_id.empty()||!record.rejection_code.empty())throw DatabaseError("Inspected planning contains foreign settlement");const auto inspected=db.execute("SELECT payload FROM events WHERE run_id=? AND kind='conversation.tool_turn' AND json_extract(payload,'$.inspected_plan_call_id')=? AND json_extract(payload,'$.origin_attempt_id')=?",{record.model.owner_run_id,record.call_id,attempt}).rows;if(inspected.size()!=1)throw DatabaseError("Stored worker inspection has no canonical commit");const auto report=object_json(Json::parse(text(inspected[0][0])).at("inspection_result_json").get<std::string>(),4*1024*1024);const auto completed=db.execute("SELECT payload FROM events WHERE run_id=? AND kind='tool.completed' AND json_extract(payload,'$.activity_id')=?",{record.model.owner_run_id,record.model.owner_run_id+":plan:"+record.call_id}).rows;if(completed.size()!=1||Json::parse(text(completed[0][0])).at("data")!=Json::parse(report))throw DatabaseError("Stored worker inspection report differs");}else if(record.state=="rejected"){if(!record.native_call_id.empty())throw DatabaseError("Rejected planning has an accepted call");(void)DynamicPlanRejected(record.rejection_code);const auto rejected=db.execute("SELECT seq FROM events WHERE run_id=? AND kind='conversation.tool_turn' AND json_extract(payload,'$.rejected_plan_call_id')=? AND json_extract(payload,'$.origin_attempt_id')=? AND json_extract(payload,'$.error_code')=?",{record.model.owner_run_id,record.call_id,attempt,record.rejection_code}).rows;if(rejected.size()!=1)throw DatabaseError("Stored worker planning rejection differs");}else if(!record.native_call_id.empty()||!record.rejection_code.empty())throw DatabaseError("Unsettled planning contains settled facts");return record;
    }catch(const Json::exception&){throw DatabaseError("Invalid stored worker planning admission");}catch(const std::invalid_argument&){throw DatabaseError("Invalid stored worker planning admission");}
}
void settle_worker_planning(Repository& repository,XlangSqlite& db,const std::string& root,const std::string& owner,const std::string& attempt,const std::string& assistant,const std::string& accepted,const std::string& rejected){
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_planning_category),worker_claim_key(root,attempt)}).rows;
    if(rows.empty())return; // Ordinary in-process planning has no worker claim.
    auto record=read_worker_planning(db,root,attempt);const auto model=read_worker_claim(db,root,attempt);
    if(record.model.owner_run_id!=owner||model.spec.generation!=record.model.generation||model.spec.root_run_id!=record.model.root_run_id||model.spec.owner_run_id!=record.model.owner_run_id||model.spec.attempt_id!=record.model.attempt_id||model.spec.request_binding!=record.model.request_binding||model.state!="responded"||model.actual_assistant_json!=assistant||record.backend_identity!=repository.dynamic_capabilities(root).backend_identity)throw Conflict("Worker planning settlement differs from captured model authority");
    if(!accepted.empty()){
        const auto call=repository.dynamic_plan_call(accepted);
        if(!rejected.empty()||call.root_run_id!=root||call.owner_run_id!=owner||call.origin_attempt_id!=attempt||call.provider_tool_call_id!=record.call_id||call.arguments_json!=record.arguments_json||call.parent_assistant_json!=assistant)throw Conflict("Worker planning settlement differs from native acceptance");
    }else{(void)DynamicPlanRejected(rejected);}
    const auto state=accepted.empty()?std::string("rejected"):std::string("accepted");
    if(record.state==state&&record.native_call_id==accepted&&record.rejection_code==rejected)return;
    if(record.state!="entered")throw Conflict("Worker planning settlement cannot change or replay an unresolved generation");
    const auto lease=read_worker_lease(db,record.model.generation);worker_current_scope(repository,db,lease.spec);
    if(lease.spec.root_run_id!=root||lease.spec.owner_run_id!=owner)throw Conflict("Worker planning settlement lost its captured lease");
    record.state=state;record.native_call_id=accepted;record.rejection_code=rejected;
    changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_planning_json(record).dump(),std::string(worker_planning_category),worker_claim_key(root,attempt)}));
}

void worker_planning_continuation_authority(Repository& repository,XlangSqlite& db,const DynamicPlanCallRecord& call,const AgentWorkerLeaseSpec* worker){
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_planning_category),worker_claim_key(call.root_run_id,call.origin_attempt_id)}).rows;
    if(!worker){if(!rows.empty())throw Conflict("Worker planning requires its captured continuation lease");return;}
    worker_current_scope(repository,db,*worker);
    if(worker->root_run_id!=call.root_run_id||worker->owner_run_id!=call.owner_run_id||worker->root_run_id!=worker->owner_run_id||repository.root_budget(call.root_run_id).spec.policy_id!="native.dynamic-plan")throw Conflict("Worker continuation differs from native planning owner");
    const auto record=read_worker_planning(db,call.root_run_id,call.origin_attempt_id);
    if(record.state!="accepted"||record.native_call_id!=call.id||record.model.generation!=worker->generation||record.model.owner_run_id!=worker->owner_run_id||record.backend_identity!=repository.dynamic_capabilities(call.root_run_id).backend_identity)throw Conflict("Worker continuation lacks exact accepted planning authority");
    const auto segment=repository.dynamic_budget_segment(call.root_run_id);if(segment.state!="open"||segment.id!=record.segment_id)throw Conflict("Worker planning continuation lost its admitted active segment");
    worker_require_tool_turns(db,*worker);
}
bool worker_planning_attempt(Repository& repository,XlangSqlite& db,const AgentWorkerLeaseSpec& lease,const std::string& attempt){
    if(lease.root_run_id!=lease.owner_run_id||repository.root_budget(lease.root_run_id).spec.policy_id!="native.dynamic-plan")return false;
    const auto rows=db.execute("SELECT id FROM dynamic_plan_calls WHERE root_run_id=? AND owner_run_id=? AND continuation_attempt_id=? AND state='turn_committed' AND conversation_commit_seq IS NOT NULL",{lease.root_run_id,lease.owner_run_id,attempt}).rows;if(rows.size()!=1)return false;
    const auto call=repository.dynamic_plan_call(text(rows[0][0]));worker_planning_continuation_authority(repository,db,call,&lease);return worker_attempt_generation(db,lease.root_run_id,lease.owner_run_id,attempt)==std::optional<std::string>{lease.generation};
}

std::string worker_tool_call_key(const std::string& root,const std::string& attempt,const std::string& call){bounded_identity(root);bounded_identity(attempt);identifier(call);if(call.size()>256)throw std::invalid_argument("Provider tool call identity exceeds limits");return context_digest(Json::array({root,attempt,call}).dump());}
Json worker_read_json(const AgentWorkerReadClaimRecord& record){return {{"version",1},{"model",worker_claim_spec_json(record.model)},{"invocation",record.invocation.serialize()},{"state",record.state},{"dispatched",record.dispatched},{"success",record.success},{"actual_output_json",record.actual_output_json},{"output_binding",record.output_binding}};}
AgentWorkerReadClaimRecord read_worker_read(XlangSqlite& db,const std::string& root,const std::string& attempt,const std::string& call){
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_read_category),worker_tool_call_key(root,attempt,call)}).rows;if(rows.empty())throw NotFound("Worker read claim not found");
    try{const auto value=Json::parse(text(rows[0][0]));const auto& m=value.at("model");AgentWorkerReadClaimRecord record;
        record.model={m.at("generation").get<std::string>(),m.at("root_run_id").get<std::string>(),m.at("owner_run_id").get<std::string>(),m.at("attempt_id").get<std::string>(),m.at("request_binding").get<std::string>()};
        record.invocation=AgentWorkspaceInvocation::parse(value.at("invocation").get<std::string>());record.state=value.at("state").get<std::string>();record.dispatched=value.at("dispatched").get<bool>();record.success=value.at("success").get<bool>();record.actual_output_json=value.at("actual_output_json").get<std::string>();record.output_binding=value.at("output_binding").get<std::string>();
        if(record.model.root_run_id!=root||record.model.attempt_id!=attempt||record.invocation.attempt_id!=attempt||record.invocation.call_id!=call||record.invocation.run_id!=record.model.owner_run_id||worker_read_json(record)!=value)throw DatabaseError("Stored worker read binding differs");
        if(record.state!="awaiting_dispatch"&&record.state!="executing"&&record.state!="returned"&&record.state!="uncertain")throw DatabaseError("Invalid worker read state");
        if((record.state=="awaiting_dispatch"&&record.dispatched)||(record.state=="executing"&&!record.dispatched)||(record.success&&!record.dispatched))throw DatabaseError("Invalid worker read dispatch fact");
        if(record.state=="returned"){(void)object_json(record.actual_output_json);if(record.output_binding!=context_digest(record.actual_output_json))throw DatabaseError("Stored worker read output differs");}
        else if(record.success||!record.actual_output_json.empty()||!record.output_binding.empty())throw DatabaseError("Unacknowledged worker read has output");return record;
    }catch(const Json::exception&){throw DatabaseError("Invalid stored worker read");}catch(const std::invalid_argument&){throw DatabaseError("Invalid stored worker read");}
}
Json worker_file_json(const AgentWorkerFileClaimRecord& record){return {{"version",1},{"model",worker_claim_spec_json(record.model)},{"invocation",record.invocation.serialize()},{"state",record.state},{"operation_ids",record.operation_ids},{"success",record.success},{"actual_output_json",record.actual_output_json},{"output_binding",record.output_binding}};}
std::string worker_operation_binding(const Operation& operation){return context_digest(Json{{"run_id",operation.spec.run_id},{"workspace",operation.spec.workspace},{"tool",operation.spec.tool},{"arguments_json",operation.spec.arguments_json},{"resources",operation.spec.resources}}.dump());}
Json worker_file_effect_binding(XlangSqlite& db,const AgentWorkerFileClaimRecord& record,const std::string& id){
    bounded_identity(id);const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_file_effect_category),id}).rows;
    if(rows.size()!=1)throw DatabaseError("Worker file operation has no unique private binding");
    const auto value=Json::parse(text(rows[0][0]));
    if(!value.is_object()||value.size()!=5||value.at("version")!=1||value.at("model")!=worker_claim_spec_json(record.model)||value.at("invocation_binding")!=context_digest(record.invocation.serialize())||value.at("operation_id")!=id||!value.at("operation_spec_binding").is_string())throw DatabaseError("Worker file operation binding differs");
    worker_digest(value.at("operation_spec_binding").get<std::string>());return value;
}
AgentWorkerFileClaimRecord read_worker_file(XlangSqlite& db,const std::string& root,const std::string& attempt,const std::string& call){
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_file_category),worker_tool_call_key(root,attempt,call)}).rows;
    if(rows.empty())throw NotFound("Worker file claim not found");
    try{
        const auto value=Json::parse(text(rows[0][0]));const auto& m=value.at("model");AgentWorkerFileClaimRecord record;
        record.model={m.at("generation").get<std::string>(),m.at("root_run_id").get<std::string>(),m.at("owner_run_id").get<std::string>(),m.at("attempt_id").get<std::string>(),m.at("request_binding").get<std::string>()};
        record.invocation=AgentFileToolInvocation::parse(value.at("invocation").get<std::string>());record.state=value.at("state").get<std::string>();record.operation_ids=value.at("operation_ids").get<std::vector<std::string>>();record.success=value.at("success").get<bool>();record.actual_output_json=value.at("actual_output_json").get<std::string>();record.output_binding=value.at("output_binding").get<std::string>();
        if(record.model.root_run_id!=root||record.model.attempt_id!=attempt||record.invocation.attempt_id!=attempt||record.invocation.call_id!=call||record.invocation.run_id!=record.model.owner_run_id||worker_file_json(record)!=value)throw DatabaseError("Stored worker file binding differs");
        if(record.state!="awaiting_dispatch"&&record.state!="executing"&&record.state!="returned"&&record.state!="uncertain")throw DatabaseError("Invalid worker file state");
        if(record.operation_ids.size()>128||(record.state=="awaiting_dispatch"&&!record.operation_ids.empty())||(record.success&&record.operation_ids.empty()))throw DatabaseError("Invalid worker file effect facts");
        std::set<std::string> seen;for(const auto& id:record.operation_ids){if(!seen.insert(id).second)throw DatabaseError("Duplicate worker file operation");(void)worker_file_effect_binding(db,record,id);}
        if(record.state=="returned"){(void)object_json(record.actual_output_json);if(record.output_binding!=context_digest(record.actual_output_json))throw DatabaseError("Stored worker file output differs");}
        else if(record.success||!record.actual_output_json.empty()||!record.output_binding.empty())throw DatabaseError("Unacknowledged worker file has output");return record;
    }catch(const Json::exception&){throw DatabaseError("Invalid stored worker file");}catch(const std::invalid_argument&){throw DatabaseError("Invalid stored worker file");}
}
Json worker_process_json(const AgentWorkerProcessClaimRecord& record){return {{"version",1},{"model",worker_claim_spec_json(record.model)},{"invocation",record.invocation.serialize()},{"state",record.state},{"operation_ids",record.operation_ids},{"success",record.success},{"actual_output_json",record.actual_output_json},{"output_binding",record.output_binding}};}
std::string worker_process_effect_key(const AgentProcessToolInvocation& call,const std::string& id){return call.tool_name=="run_process"?id:context_digest(Json::array({call.run_id,call.attempt_id,call.call_id,id}).dump());}
Json worker_process_effect_binding(XlangSqlite& db,const AgentWorkerProcessClaimRecord& record,const std::string& id){
    bounded_identity(id);const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_process_effect_category),worker_process_effect_key(record.invocation,id)}).rows;
    if(rows.size()!=1)throw DatabaseError("Worker process operation has no unique private binding");
    const auto value=Json::parse(text(rows[0][0]));
    if(!value.is_object()||value.size()!=5||value.at("version")!=1||value.at("model")!=worker_claim_spec_json(record.model)||value.at("invocation_binding")!=context_digest(record.invocation.serialize())||value.at("operation_id")!=id||!value.at("operation_spec_binding").is_string())throw DatabaseError("Worker process operation binding differs");
    worker_digest(value.at("operation_spec_binding").get<std::string>());return value;
}
AgentWorkerProcessClaimRecord read_worker_process(XlangSqlite& db,const std::string& root,const std::string& attempt,const std::string& call){
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_process_category),worker_tool_call_key(root,attempt,call)}).rows;
    if(rows.empty())throw NotFound("Worker process claim not found");
    try{
        const auto value=Json::parse(text(rows[0][0]));const auto& m=value.at("model");AgentWorkerProcessClaimRecord record;
        record.model={m.at("generation").get<std::string>(),m.at("root_run_id").get<std::string>(),m.at("owner_run_id").get<std::string>(),m.at("attempt_id").get<std::string>(),m.at("request_binding").get<std::string>()};
        record.invocation=AgentProcessToolInvocation::parse(value.at("invocation").get<std::string>());record.state=value.at("state").get<std::string>();record.operation_ids=value.at("operation_ids").get<std::vector<std::string>>();record.success=value.at("success").get<bool>();record.actual_output_json=value.at("actual_output_json").get<std::string>();record.output_binding=value.at("output_binding").get<std::string>();
        if(record.model.root_run_id!=root||record.model.attempt_id!=attempt||record.invocation.attempt_id!=attempt||record.invocation.call_id!=call||record.invocation.run_id!=record.model.owner_run_id||worker_process_json(record)!=value)throw DatabaseError("Stored worker process binding differs");
        if(record.state!="awaiting_dispatch"&&record.state!="executing"&&record.state!="returned"&&record.state!="uncertain")throw DatabaseError("Invalid worker process state");
        if(record.operation_ids.size()>1||(record.state=="awaiting_dispatch"&&!record.operation_ids.empty())||(record.success&&record.operation_ids.empty()))throw DatabaseError("Invalid worker process effect facts");
        std::set<std::string> seen;for(const auto& id:record.operation_ids){if(!seen.insert(id).second)throw DatabaseError("Duplicate worker process operation");(void)worker_process_effect_binding(db,record,id);}
        if(record.state=="returned"){(void)object_json(record.actual_output_json);if(record.output_binding!=context_digest(record.actual_output_json))throw DatabaseError("Stored worker process output differs");}
        else if(record.success||!record.actual_output_json.empty()||!record.output_binding.empty())throw DatabaseError("Unacknowledged worker process has output");return record;
    }catch(const Json::exception&){throw DatabaseError("Invalid stored worker process");}catch(const std::invalid_argument&){throw DatabaseError("Invalid stored worker process");}
}
Json worker_remote_json(const AgentWorkerRemoteClaimRecord& record){return {{"version",1},{"model",worker_claim_spec_json(record.model)},{"invocation",record.invocation.serialize()},{"state",record.state},{"operation_ids",record.operation_ids},{"success",record.success},{"actual_output_json",record.actual_output_json},{"output_binding",record.output_binding}};}
std::string worker_remote_effect_key(const AgentRemoteToolInvocation&,const std::string& id){return id;}
Json worker_remote_effect_binding(XlangSqlite& db,const AgentWorkerRemoteClaimRecord& record,const std::string& id){
    bounded_identity(id);const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_remote_effect_category),worker_remote_effect_key(record.invocation,id)}).rows;
    if(rows.size()!=1)throw DatabaseError("Worker process operation has no unique private binding");
    const auto value=Json::parse(text(rows[0][0]));
    if(!value.is_object()||value.size()!=5||value.at("version")!=1||value.at("model")!=worker_claim_spec_json(record.model)||value.at("invocation_binding")!=context_digest(record.invocation.serialize())||value.at("operation_id")!=id||!value.at("operation_spec_binding").is_string())throw DatabaseError("Worker process operation binding differs");
    worker_digest(value.at("operation_spec_binding").get<std::string>());return value;
}
AgentWorkerRemoteClaimRecord read_worker_remote(XlangSqlite& db,const std::string& root,const std::string& attempt,const std::string& call){
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_remote_category),worker_tool_call_key(root,attempt,call)}).rows;
    if(rows.empty())throw NotFound("Worker process claim not found");
    try{
        const auto value=Json::parse(text(rows[0][0]));const auto& m=value.at("model");AgentWorkerRemoteClaimRecord record;
        record.model={m.at("generation").get<std::string>(),m.at("root_run_id").get<std::string>(),m.at("owner_run_id").get<std::string>(),m.at("attempt_id").get<std::string>(),m.at("request_binding").get<std::string>()};
        record.invocation=AgentRemoteToolInvocation::parse(value.at("invocation").get<std::string>());record.state=value.at("state").get<std::string>();record.operation_ids=value.at("operation_ids").get<std::vector<std::string>>();record.success=value.at("success").get<bool>();record.actual_output_json=value.at("actual_output_json").get<std::string>();record.output_binding=value.at("output_binding").get<std::string>();
        if(record.model.root_run_id!=root||record.model.attempt_id!=attempt||record.invocation.attempt_id!=attempt||record.invocation.call_id!=call||record.invocation.run_id!=record.model.owner_run_id||worker_remote_json(record)!=value)throw DatabaseError("Stored worker remote binding differs");
        if(record.state!="awaiting_dispatch"&&record.state!="executing"&&record.state!="returned"&&record.state!="uncertain")throw DatabaseError("Invalid worker remote state");
        if(record.operation_ids.size()>1||(record.state=="awaiting_dispatch"&&!record.operation_ids.empty())||(record.success&&record.operation_ids.empty()))throw DatabaseError("Invalid worker remote effect facts");
        std::set<std::string> seen;for(const auto& id:record.operation_ids){if(!seen.insert(id).second)throw DatabaseError("Duplicate worker remote operation");(void)worker_remote_effect_binding(db,record,id);}
        if(record.state=="returned"){(void)object_json(record.actual_output_json);if(record.output_binding!=context_digest(record.actual_output_json))throw DatabaseError("Stored worker remote output differs");}
        else if(record.success||!record.actual_output_json.empty()||!record.output_binding.empty())throw DatabaseError("Unacknowledged worker remote has output");return record;
    }catch(const Json::exception&){throw DatabaseError("Invalid stored worker remote");}catch(const std::invalid_argument&){throw DatabaseError("Invalid stored worker remote");}
}
void worker_callable_output(const std::string& output){if(output.empty()||output.size()>1024*1024)throw std::invalid_argument("Callable output exceeds limits");try{(void)Json::parse(output);}catch(const Json::exception&){throw std::invalid_argument("Callable output is not JSON");}}
Json worker_callable_json(const AgentWorkerCallableClaimRecord& record){return {{"version",1},{"model",worker_claim_spec_json(record.model)},{"invocation",record.invocation.serialize()},{"state",record.state},{"operation_ids",record.operation_ids},{"success",record.success},{"actual_output_json",record.actual_output_json},{"output_binding",record.output_binding}};}
std::string worker_callable_effect_key(const AgentCallableInvocation&,const std::string& id){return id;}
Json worker_callable_effect_binding(XlangSqlite& db,const AgentWorkerCallableClaimRecord& record,const std::string& id){
    bounded_identity(id);const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_callable_effect_category),worker_callable_effect_key(record.invocation,id)}).rows;
    if(rows.size()!=1)throw DatabaseError("Worker process operation has no unique private binding");
    const auto value=Json::parse(text(rows[0][0]));
    if(!value.is_object()||value.size()!=5||value.at("version")!=1||value.at("model")!=worker_claim_spec_json(record.model)||value.at("invocation_binding")!=context_digest(record.invocation.serialize())||value.at("operation_id")!=id||!value.at("operation_spec_binding").is_string())throw DatabaseError("Worker process operation binding differs");
    worker_digest(value.at("operation_spec_binding").get<std::string>());return value;
}
AgentWorkerCallableClaimRecord read_worker_callable(XlangSqlite& db,const std::string& root,const std::string& attempt,const std::string& call){
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_callable_category),worker_tool_call_key(root,attempt,call)}).rows;
    if(rows.empty())throw NotFound("Worker process claim not found");
    try{
        const auto value=Json::parse(text(rows[0][0]));const auto& m=value.at("model");AgentWorkerCallableClaimRecord record;
        record.model={m.at("generation").get<std::string>(),m.at("root_run_id").get<std::string>(),m.at("owner_run_id").get<std::string>(),m.at("attempt_id").get<std::string>(),m.at("request_binding").get<std::string>()};
        record.invocation=AgentCallableInvocation::parse(value.at("invocation").get<std::string>());record.state=value.at("state").get<std::string>();record.operation_ids=value.at("operation_ids").get<std::vector<std::string>>();record.success=value.at("success").get<bool>();record.actual_output_json=value.at("actual_output_json").get<std::string>();record.output_binding=value.at("output_binding").get<std::string>();
        if(record.model.root_run_id!=root||record.model.attempt_id!=attempt||record.invocation.attempt_id!=attempt||record.invocation.call_id!=call||record.invocation.run_id!=record.model.owner_run_id||worker_callable_json(record)!=value)throw DatabaseError("Stored worker callable binding differs");
        if(record.state!="awaiting_dispatch"&&record.state!="executing"&&record.state!="returned"&&record.state!="uncertain")throw DatabaseError("Invalid worker callable state");
        if(record.operation_ids.size()>1||(record.state=="awaiting_dispatch"&&!record.operation_ids.empty())||(record.success&&record.operation_ids.empty()))throw DatabaseError("Invalid worker callable effect facts");
        std::set<std::string> seen;for(const auto& id:record.operation_ids){if(!seen.insert(id).second)throw DatabaseError("Duplicate worker callable operation");(void)worker_callable_effect_binding(db,record,id);}
        if(record.state=="returned"){worker_callable_output(record.actual_output_json);if(record.output_binding!=context_digest(record.actual_output_json))throw DatabaseError("Stored worker callable output differs");}
        else if(record.success||!record.actual_output_json.empty()||!record.output_binding.empty())throw DatabaseError("Unacknowledged worker callable has output");return record;
    }catch(const Json::exception&){throw DatabaseError("Invalid stored worker callable");}catch(const std::invalid_argument&){throw DatabaseError("Invalid stored worker callable");}
}
Json worker_turn_json(const AgentWorkerToolTurnRecord& turn){return {{"version",1},{"model",worker_claim_spec_json(turn.model)},{"message_sequences",turn.message_sequences},{"messages_binding",turn.messages_binding}};}
Json worker_turn_messages(XlangSqlite& db,const AgentWorkerToolTurnRecord& turn){
    if(turn.message_sequences.size()<2||turn.message_sequences.size()>65)throw DatabaseError("Invalid worker tool turn size");Json messages=Json::array();std::int64_t previous=0;
    for(const auto sequence:turn.message_sequences){if(sequence<=previous||sequence>9007199254740991LL)throw DatabaseError("Invalid worker tool turn sequence");previous=sequence;
        const auto rows=db.execute("SELECT m.role,m.payload FROM messages m JOIN task_messages t ON t.message_seq=m.seq WHERE m.seq=? AND t.run_id=?",{sequence,turn.model.owner_run_id}).rows;
        if(rows.size()!=1||text(rows[0][0])!=(messages.empty()?"assistant":"tool"))throw DatabaseError("Worker tool turn message differs");messages.push_back({{"sequence",sequence},{"role",text(rows[0][0])},{"json",text(rows[0][1])}});
    }return messages;
}
AgentWorkerToolTurnRecord read_worker_turn(XlangSqlite& db,const AgentWorkerModelClaimSpec& model){
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_turn_category),worker_claim_key(model.root_run_id,model.attempt_id)}).rows;if(rows.empty())throw NotFound("Worker tool turn not committed");
    try{const auto saved=Json::parse(text(rows[0][0]));AgentWorkerToolTurnRecord turn{model,saved.at("message_sequences").get<std::vector<std::int64_t>>(),saved.at("messages_binding").get<std::string>()};worker_digest(turn.messages_binding);
        if(saved!=worker_turn_json(turn)||context_digest(worker_turn_messages(db,turn).dump())!=turn.messages_binding)throw DatabaseError("Stored worker tool turn binding differs");return turn;
    }catch(const Json::exception&){throw DatabaseError("Invalid worker tool turn receipt");}catch(const std::invalid_argument&){throw DatabaseError("Invalid worker tool turn receipt");}
}
void worker_require_tool_turns(XlangSqlite& db,const AgentWorkerLeaseSpec& lease){
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND json_extract(payload,'$.spec.root_run_id')=? AND json_extract(payload,'$.spec.owner_run_id')=? AND json_extract(payload,'$.state')='responded'",{std::string(worker_claim_category),lease.root_run_id,lease.owner_run_id}).rows;
    for(const auto& row:rows){const auto saved=Json::parse(text(row[0]));const auto model=read_worker_claim(db,lease.root_run_id,saved.at("spec").at("attempt_id").get<std::string>());if(model.actual_assistant_json.empty())continue;
        const auto actual=decode_agent_model_completion(model.actual_response_json);if(actual.tool_calls.empty()&&actual.finish_reason!="tool_calls")continue;
        if(actual.tool_calls.size()==1&&(actual.tool_calls[0].name=="plan_tasks"||actual.tool_calls[0].name=="revise_plan"||actual.tool_calls[0].name=="inspect_plan")){
            try{const auto planning=read_worker_planning(db,lease.root_run_id,model.spec.attempt_id);if(planning.model.generation!=lease.generation||planning.model.owner_run_id!=lease.owner_run_id)throw Conflict("Planning turn belongs to another generation");if(planning.state=="rejected"||planning.state=="inspected")continue;if(planning.state=="accepted"){const auto committed=db.execute("SELECT id FROM dynamic_plan_calls WHERE id=? AND state='turn_committed' AND conversation_commit_seq IS NOT NULL",{planning.native_call_id}).rows;if(committed.size()==1)continue;}}catch(const NotFound&){}
            throw Conflict("Worker planning turn has no signed native conversation commit");
        }
        try{(void)read_worker_turn(db,model.spec);}catch(const NotFound&){throw Conflict("Worker must commit its actual tool turn before another model call");}
    }
}
bool worker_read_tool(const std::string& name){const std::set<std::string> reads{"read_file","list_files","search_files","find_files","glob_files","language_server","read_repository_instructions","list_skills","load_skill"};return reads.contains(name);}
template<class Invocation>AgentWorkerModelClaimSpec worker_workspace_authority(Repository& repository,XlangSqlite& db,const AgentWorkerLeaseSpec& lease,const Invocation& call,bool file,bool process=false){
    (void)call.serialize();worker_current_scope(repository,db,lease);
    if(((file||process)?!worker_effect_owner(repository,lease):!worker_agent_owner(repository,lease))||lease.workspace_id=="workspace:none"||call.run_id!=lease.owner_run_id||call.workspace_id!=lease.workspace_id||call.workspace_revision!=lease.workspace_revision)throw Conflict("Worker read differs from its ordinary workspace owner");
    const bool delegation=!file&&!process&&call.tool_name=="delegate_tasks";
    if(delegation&&(lease.root_run_id!=lease.owner_run_id||repository.root_budget(lease.root_run_id).spec.policy_id!="native.delegation"))throw Conflict("Worker delegation differs from owning root policy");
    if(process?!is_agent_process_tool(call.tool_name):file?!is_agent_file_tool(call.tool_name):(!worker_read_tool(call.tool_name)&&!delegation))throw Conflict("Worker workspace tool is not admitted");
    const auto model=read_worker_claim(db,lease.root_run_id,call.attempt_id);
    if(model.spec.generation!=lease.generation||model.spec.owner_run_id!=lease.owner_run_id||model.state!="responded"||model.actual_assistant_json.empty())throw Conflict("Worker read has no actual model receipt");
    const auto reservation=db.execute("SELECT state,COALESCE(actual_assistant_json,'') FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=? AND attempt_id=?",{lease.root_run_id,lease.owner_run_id,call.attempt_id}).rows;
    const auto latest=db.execute("SELECT json_extract(payload,'$.attempt_id') FROM events WHERE run_id=? AND kind='budget.model_call.reserved' AND json_extract(payload,'$.root_run_id')=? ORDER BY seq DESC LIMIT 1",{lease.owner_run_id,lease.root_run_id}).rows;
    if(reservation.size()!=1||text(reservation[0][0])!="finished"||text(reservation[0][1])!=model.actual_assistant_json||latest.size()!=1||text(latest[0][0])!=call.attempt_id)throw Conflict("Worker read requires its latest fully retired model attempt");
    const auto actual=decode_agent_model_completion(model.actual_response_json);std::size_t matches=0;
    if(actual.finish_reason!="tool_calls")throw Conflict("Worker read requires a tool response");
    for(const auto& tool:actual.tool_calls)if(tool.id==call.call_id){++matches;if(tool.name!=call.tool_name||tool.arguments_json!=call.arguments_json)throw Conflict("Worker read differs from the actual provider tool call");}
    if(matches!=1)throw Conflict("Worker read provider call identity is absent or ambiguous");
    const auto args=Json::parse(object_json(call.arguments_json,(file||process)?4*1024*1024:2*1024*1024));const auto folder=args.value("folder_id",lease.workspace_id);if(call.folder_id!=folder)throw Conflict("Worker workspace folder differs from actual provider arguments");return model.spec;
}
AgentWorkerModelClaimSpec worker_remote_authority(Repository& repository,XlangSqlite& db,const AgentWorkerLeaseSpec& lease,const AgentRemoteToolInvocation& call){
    (void)call.serialize();worker_current_scope(repository,db,lease);
    if(!worker_effect_owner(repository,lease)||lease.workspace_id=="workspace:none"||call.run_id!=lease.owner_run_id||call.workspace_id!=lease.workspace_id||call.workspace_revision!=lease.workspace_revision)throw Conflict("Worker remote differs from its ordinary workspace owner");
    if(!is_agent_remote_tool(call.tool_name))throw Conflict("Worker workspace tool is not admitted");
    const auto model=read_worker_claim(db,lease.root_run_id,call.attempt_id);
    if(model.spec.generation!=lease.generation||model.spec.owner_run_id!=lease.owner_run_id||model.state!="responded"||model.actual_assistant_json.empty())throw Conflict("Worker remote has no actual model receipt");
    const auto reservation=db.execute("SELECT state,COALESCE(actual_assistant_json,'') FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=? AND attempt_id=?",{lease.root_run_id,lease.owner_run_id,call.attempt_id}).rows;
    const auto latest=db.execute("SELECT json_extract(payload,'$.attempt_id') FROM events WHERE run_id=? AND kind='budget.model_call.reserved' AND json_extract(payload,'$.root_run_id')=? ORDER BY seq DESC LIMIT 1",{lease.owner_run_id,lease.root_run_id}).rows;
    if(reservation.size()!=1||text(reservation[0][0])!="finished"||text(reservation[0][1])!=model.actual_assistant_json||latest.size()!=1||text(latest[0][0])!=call.attempt_id)throw Conflict("Worker remote requires its latest fully retired model attempt");
    const auto actual=decode_agent_model_completion(model.actual_response_json);std::size_t matches=0;
    if(actual.finish_reason!="tool_calls")throw Conflict("Worker remote requires a tool response");
    for(const auto& tool:actual.tool_calls)if(tool.id==call.call_id){++matches;if(tool.name!=call.tool_name||tool.arguments_json!=call.arguments_json)throw Conflict("Worker remote differs from the actual provider tool call");}
    if(matches!=1)throw Conflict("Worker remote provider call identity is absent or ambiguous");
    (void)object_json(call.arguments_json,4*1024*1024);if(call.tool_index>=actual.tool_calls.size()||actual.tool_calls[call.tool_index].id!=call.call_id)throw Conflict("Worker remote call index differs from its actual model ordinal");return model.spec;
}
bool worker_callable_tool(XlangSqlite& db,const AgentWorkerLeaseSpec& lease,const std::string& name){
    const auto rows=db.execute("SELECT payload FROM information WHERE category='native-agent-worker-callable-catalogues' AND id=?",{lease.generation}).rows;if(rows.empty())return false;const auto value=Json::parse(text(rows[0][0]));if(value.at("version")!=1||value.at("lease")!=worker_spec_json(lease)||!value.at("tools").is_array())throw DatabaseError("Stored callable catalogue differs from captured lease");for(const auto& tool:value.at("tools"))if(tool.at("name")==name)return true;return false;
}
AgentWorkerModelClaimSpec worker_callable_authority(Repository& repository,XlangSqlite& db,const AgentWorkerLeaseSpec& lease,const AgentCallableInvocation& call){
    (void)call.serialize();worker_current_scope(repository,db,lease);if(!worker_callable_tool(db,lease,call.tool_name))throw Conflict("Callable is absent from its backend-admitted catalogue");
    if(!worker_effect_owner(repository,lease)||lease.workspace_id=="workspace:none"||call.run_id!=lease.owner_run_id||call.workspace_id!=lease.workspace_id||call.workspace_revision!=lease.workspace_revision)throw Conflict("Worker callable differs from its captured coding workspace owner");
    const auto model=read_worker_claim(db,lease.root_run_id,call.attempt_id);
    if(model.spec.generation!=lease.generation||model.spec.owner_run_id!=lease.owner_run_id||model.state!="responded"||model.actual_assistant_json.empty())throw Conflict("Worker callable has no actual model receipt");
    const auto reservation=db.execute("SELECT state,COALESCE(actual_assistant_json,'') FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=? AND attempt_id=?",{lease.root_run_id,lease.owner_run_id,call.attempt_id}).rows;
    const auto latest=db.execute("SELECT json_extract(payload,'$.attempt_id') FROM events WHERE run_id=? AND kind='budget.model_call.reserved' AND json_extract(payload,'$.root_run_id')=? ORDER BY seq DESC LIMIT 1",{lease.owner_run_id,lease.root_run_id}).rows;
    if(reservation.size()!=1||text(reservation[0][0])!="finished"||text(reservation[0][1])!=model.actual_assistant_json||latest.size()!=1||text(latest[0][0])!=call.attempt_id)throw Conflict("Worker callable requires its latest fully retired model attempt");
    const auto actual=decode_agent_model_completion(model.actual_response_json);std::size_t matches=0;
    if(actual.finish_reason!="tool_calls")throw Conflict("Worker callable requires a tool response");
    for(const auto& tool:actual.tool_calls)if(tool.id==call.call_id){++matches;if(tool.name!=call.tool_name||tool.arguments_json!=call.arguments_json)throw Conflict("Worker callable differs from the actual provider tool call");}
    if(matches!=1)throw Conflict("Worker callable provider call identity is absent or ambiguous");
    (void)object_json(call.arguments_json,4*1024*1024);if(call.tool_index>=actual.tool_calls.size()||actual.tool_calls[call.tool_index].id!=call.call_id)throw Conflict("Worker callable call index differs from its actual model ordinal");return model.spec;
}
AgentWorkerModelClaimSpec worker_read_authority(Repository& repository,XlangSqlite& db,const AgentWorkerLeaseSpec& lease,const AgentWorkspaceInvocation& call){return worker_workspace_authority(repository,db,lease,call,false);}
AgentWorkerModelClaimSpec worker_file_authority(Repository& repository,XlangSqlite& db,const AgentWorkerLeaseSpec& lease,const AgentFileToolInvocation& call){return worker_workspace_authority(repository,db,lease,call,true);}
AgentWorkerModelClaimSpec worker_process_authority(Repository& repository,XlangSqlite& db,const AgentWorkerLeaseSpec& lease,const AgentProcessToolInvocation& call){return worker_workspace_authority(repository,db,lease,call,false,true);}
Json worker_provider_identity(Repository& repository,const std::string& root,const std::string& owner){
    const auto budget=repository.root_budget(root);auto provider=Json::parse(budget.spec.provider_identity_json);
    if(root!=owner){const auto admitted=repository.child_admission(owner);if(admitted.kind==ChildAdmissionKind::delegated_leaf){if(admitted.root_run_id!=root)throw Conflict("Delegated model identity belongs to another root");const auto history=repository.run_history(owner);if(history.empty()||history.front().role!="user")throw Conflict("Worker child has no admitted objective");const auto prompt=Json::parse(history.front().json);if(prompt.contains("agent_profile")){const auto model=prompt.at("agent_profile").at("model_id").get<std::string>();if(!model.empty())provider["model_id"]=model;}auto policy=budget.spec;policy.provider_identity_json=provider.dump();(void)budget_provider_identity(policy);}}
    if(root!=owner){const auto admitted=repository.child_admission(owner);if(admitted.kind==ChildAdmissionKind::graph_agent){if(admitted.root_run_id!=root)throw Conflict("Graph worker model belongs to another root");const auto capture=repository.graph_context_owner(root);const auto found=std::find_if(capture.authority.agents.begin(),capture.authority.agents.end(),[&](const auto& value){return value.node_id==admitted.graph_node_path;});if(found==capture.authority.agents.end())throw Conflict("Graph worker lacks captured node authority");provider=Json::parse(found->provider_identity_json);}}
    return provider;
}
bool worker_graph_owner(Repository& repository,const AgentWorkerLeaseSpec& lease){
    if(lease.owner_run_id==lease.root_run_id)return false;
    const auto root=repository.run(lease.root_run_id),owner=repository.run(lease.owner_run_id);const auto budget=repository.root_budget(root.id);
    if(!root.graph_root||!root.parent_id.empty()||owner.graph_root||owner.session_id!=root.session_id||budget.spec.policy_id!="native.graph-context"||budget.spec.policy_revision!=1)return false;
    const auto admission=repository.child_admission(owner.id);if(admission.kind!=ChildAdmissionKind::graph_agent||admission.root_run_id!=root.id)return false;
    auto ancestor=repository.run(owner.parent_id);for(unsigned depth=0;ancestor.id!=root.id;++depth){if(depth>=4||!ancestor.graph_root||ancestor.state!=RunState::running||ancestor.parent_id.empty()||ancestor.session_id!=root.session_id)return false;ancestor=repository.run(ancestor.parent_id);}
    const auto capture=repository.graph_context_owner(root.id);if(!capture.segment||capture.segment->state!="open")return false;
    return std::any_of(capture.authority.agents.begin(),capture.authority.agents.end(),[&](const auto& node){return node.node_id==admission.graph_node_path&&Json::parse(node.provider_identity_json)==Json::parse(lease.provider_identity_json);});
}
bool worker_uncounted_graph_owner(Repository& repository,const AgentWorkerLeaseSpec& lease){return worker_graph_owner(repository,lease)&&!repository.graph_context_owner(lease.root_run_id).authority.token_context;}
bool worker_delegated_owner(Repository& repository,const AgentWorkerLeaseSpec& lease){
    if(lease.owner_run_id==lease.root_run_id)return false;
    const auto root=repository.run(lease.root_run_id),owner=repository.run(lease.owner_run_id);
    const auto budget=repository.root_budget(lease.root_run_id);
    if(root.graph_root||!root.parent_id.empty()||owner.graph_root||owner.parent_id!=root.id||owner.session_id!=root.session_id||budget.spec.policy_id!="native.delegation"||budget.spec.policy_revision!=1)return false;
    const auto admitted=repository.child_admission(owner.id);
    return admitted.kind==ChildAdmissionKind::delegated_leaf&&admitted.root_run_id==root.id&&admitted.preset_id=="workspace.inspect"&&admitted.preset_revision==1&&repository.delegation_batch(admitted.batch_id).state=="accepted";
}
bool worker_effect_owner(Repository& repository,const AgentWorkerLeaseSpec& lease){return lease.owner_run_id==lease.root_run_id?ordinary_worker_policy(repository.root_budget(lease.root_run_id).spec.policy_id):worker_graph_owner(repository,lease);}
bool worker_agent_owner(Repository& repository,const AgentWorkerLeaseSpec& lease){
    return lease.owner_run_id==lease.root_run_id?ordinary_worker_policy(repository.root_budget(lease.root_run_id).spec.policy_id):(worker_delegated_owner(repository,lease)||worker_graph_owner(repository,lease));
}
void worker_running_scope(Repository& repository,XlangSqlite& db,const AgentWorkerLeaseSpec& spec,bool allow_queued_root=false){
    const auto root=repository.run(spec.root_run_id),owner=repository.run(spec.owner_run_id);if(!root.parent_id.empty())throw Conflict("Worker requires an owning root, not another child");const auto budget=repository.root_budget(spec.root_run_id);
    const bool delegated=worker_delegated_owner(repository,spec),graph=worker_graph_owner(repository,spec);
    const bool queued=allow_queued_root&&owner.state==RunState::queued&&((spec.root_run_id==spec.owner_run_id&&!root.graph_root&&root.state==RunState::queued&&ordinary_worker_policy(budget.spec.policy_id))||((delegated||graph)&&root.state==RunState::running));
    const auto provider=(delegated||graph)?worker_provider_identity(repository,spec.root_run_id,spec.owner_run_id):Json::parse(budget.spec.provider_identity_json);
    if((!queued&&(root.state!=RunState::running||owner.state!=RunState::running))||owner.graph_root||budget.spec.workspace_identity!=spec.workspace_id||provider!=Json::parse(spec.provider_identity_json))throw Conflict("Worker admission differs from its execution owners");
    if(spec.owner_run_id!=spec.root_run_id){const auto child=repository.child_admission(spec.owner_run_id);if(child.root_run_id!=spec.root_run_id||(child.kind!=ChildAdmissionKind::graph_agent&&child.kind!=ChildAdmissionKind::dynamic_agent&&child.kind!=ChildAdmissionKind::delegated_leaf))throw Conflict("Worker is not an admitted agent child");}
    const auto membership=db.execute("SELECT payload FROM events WHERE run_id=? AND kind='agent.workspace_membership' ORDER BY seq LIMIT 1",{spec.owner_run_id}).rows;
    if(delegated||graph){const auto parent_membership=db.execute("SELECT payload FROM events WHERE run_id=? AND kind='agent.workspace_membership' ORDER BY seq LIMIT 1",{spec.root_run_id}).rows;if(parent_membership.empty()!=membership.empty()||(!membership.empty()&&Json::parse(text(parent_membership[0][0]))!=Json::parse(text(membership[0][0]))))throw Conflict("Child worker membership differs from its captured root");}
    if(spec.workspace_id=="workspace:none"){if(spec.workspace_revision!=1||!membership.empty())throw Conflict("Worker workspace-free admission differs");}
    else{if(membership.empty())throw Conflict("Worker requires backend-captured workspace membership");const auto saved=Json::parse(text(membership[0][0]));if(saved.at("workspace_id")!=spec.workspace_id||saved.at("revision")!=spec.workspace_revision)throw Conflict("Worker workspace admission changed");}
}
void worker_current_scope(Repository& repository,XlangSqlite& db,const AgentWorkerLeaseSpec& expected,bool allow_queued_root){
    const auto lease=read_worker_lease(db,expected.generation);if(lease.state!="active"||lease.spec!=expected)throw Conflict("Worker has no exact active captured lease");worker_running_scope(repository,db,expected,allow_queued_root);
    const auto current=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_current_category),expected.owner_run_id}).rows;
    if(current.empty()||Json::parse(text(current[0][0])).at("generation")!=expected.generation)throw Conflict("Worker generation is stale");
}
std::optional<std::string> worker_attempt_generation(XlangSqlite& db,const std::string& root,const std::string& owner,const std::string& attempt){
    const auto rows=db.execute("SELECT payload FROM events WHERE run_id=? AND kind='budget.model_call.reserved' AND json_extract(payload,'$.root_run_id')=? AND json_extract(payload,'$.attempt_id')=?",{owner,root,attempt}).rows;
    if(rows.empty())return {};if(rows.size()!=1)throw DatabaseError("Model reservation has ambiguous funding provenance");const auto saved=Json::parse(text(rows[0][0]));
    if(!saved.contains("worker_generation"))return {};if(!saved.at("worker_generation").is_string()||saved.at("worker_generation").get_ref<const std::string&>().empty())throw DatabaseError("Invalid model funding generation");return saved.at("worker_generation").get<std::string>();
}
std::string worker_assistant_receipt(Repository& repository,XlangSqlite& db,const AgentWorkerLeaseSpec& lease,const std::string& attempt,bool final,bool planning_root){
    worker_current_scope(repository,db,lease);if(planning_root&&(!final||lease.root_run_id!=lease.owner_run_id||repository.root_budget(lease.root_run_id).spec.policy_id!="native.dynamic-plan"||!repository.dynamic_capabilities(lease.root_run_id).catalogue_finalized))throw Conflict("Worker final planning completion lost its captured root");if(!planning_root&&!worker_agent_owner(repository,lease)&&(final||!worker_planning_attempt(repository,db,lease,attempt)))throw Conflict("Worker completion requires its exact agent admission");
    const auto claim=read_worker_claim(db,lease.root_run_id,attempt);
    if(claim.spec.generation!=lease.generation||claim.spec.owner_run_id!=lease.owner_run_id||claim.state!="responded"||claim.actual_assistant_json.empty())throw Conflict("Worker completion has no captured actual assistant receipt");
    if(final){const auto response=decode_agent_model_completion(claim.actual_response_json);if(response.finish_reason!="stop"||!response.tool_calls.empty())throw Conflict("Worker cannot complete a non-final model response");
        worker_require_tool_turns(db,lease);
        if(!db.execute("SELECT id FROM information WHERE category=? AND json_extract(payload,'$.model.root_run_id')=? AND json_extract(payload,'$.model.owner_run_id')=? AND json_extract(payload,'$.state')!='returned' LIMIT 1",{std::string(worker_read_category),lease.root_run_id,lease.owner_run_id}).rows.empty())throw Conflict("Worker cannot complete with unresolved read ownership");
        if(!db.execute("SELECT id FROM information WHERE category=? AND json_extract(payload,'$.model.root_run_id')=? AND json_extract(payload,'$.model.owner_run_id')=? AND json_extract(payload,'$.state')!='returned' LIMIT 1",{std::string(worker_file_category),lease.root_run_id,lease.owner_run_id}).rows.empty())throw Conflict("Worker cannot complete with unresolved file ownership");
        if(!db.execute("SELECT id FROM information WHERE category=? AND json_extract(payload,'$.model.root_run_id')=? AND json_extract(payload,'$.model.owner_run_id')=? AND json_extract(payload,'$.state')!='returned' LIMIT 1",{std::string(worker_callable_category),lease.root_run_id,lease.owner_run_id}).rows.empty())throw Conflict("Worker cannot complete with unresolved callable ownership");
        const auto call=db.execute("SELECT state,COALESCE(actual_assistant_json,'') FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=? AND attempt_id=?",{lease.root_run_id,lease.owner_run_id,attempt}).rows;
        const auto latest=db.execute("SELECT json_extract(payload,'$.attempt_id') FROM events WHERE run_id=? AND kind='budget.model_call.reserved' AND json_extract(payload,'$.root_run_id')=? ORDER BY seq DESC LIMIT 1",{lease.owner_run_id,lease.root_run_id}).rows;
        if(call.size()!=1||text(call[0][0])!="finished"||text(call[0][1])!=claim.actual_assistant_json||latest.size()!=1||text(latest[0][0])!=attempt||!db.execute("SELECT attempt_id FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=? AND state!='finished' LIMIT 1",{lease.root_run_id,lease.owner_run_id}).rows.empty())throw Conflict("Worker final response is not its latest fully retired attempt");
    }
    return claim.actual_assistant_json;
}
}
AgentWorkerLeaseRecord Repository::admit_agent_worker(const AgentWorkerLeaseSpec& spec){
    (void)worker_spec_json(spec);auto& db=impl_->database;Transaction tx(db);worker_running_scope(*this,db,spec,true);
    if(!db.execute("SELECT id FROM information WHERE category=? AND id=?",{std::string(worker_lease_category),spec.generation}).rows.empty())throw Conflict("Worker generation cannot be reused");
    const auto prior=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_current_category),spec.owner_run_id}).rows;
    if(!prior.empty()){const auto current=read_worker_lease(db,Json::parse(text(prior[0][0])).at("generation").get<std::string>());if(current.spec.owner_run_id!=spec.owner_run_id||current.state!="retired")throw Conflict("Worker owner already has an active generation");}
    AgentWorkerLeaseRecord record{spec,"active",{},1};record.spec.provider_identity_json=Json::parse(spec.provider_identity_json).dump();changed_one(db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?)",{std::string(worker_lease_category),spec.generation,worker_lease_json(record).dump()}));
    changed_one(db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO UPDATE SET payload=excluded.payload",{std::string(worker_current_category),spec.owner_run_id,Json{{"generation",spec.generation}}.dump()}));impl_->event(spec.owner_run_id,"worker.lease.admitted",Json{{"generation",spec.generation},{"root_run_id",spec.root_run_id},{"pid",spec.pid}}.dump());tx.commit();return record;
}
AgentWorkerLeaseRecord Repository::agent_worker_lease(const std::string& generation){return read_worker_lease(impl_->database,generation);}
void Repository::validate_agent_worker_model_window(const AgentWorkerLeaseSpec& lease){
    auto& db=impl_->database;Transaction tx(db);worker_current_scope(*this,db,lease);worker_require_tool_turns(db,lease);tx.commit();
}
AgentWorkerJournalValue Repository::read_agent_worker_journal(const AgentWorkerLeaseSpec& expected,const AgentWorkerJournalRead& query){
    if(query.target.empty()||query.target.size()>256||query.after<0||(query.kind!=AgentWorkerJournalReadKind::events&&query.after!=0))throw std::invalid_argument("Invalid worker journal read");
    auto& db=impl_->database;Transaction tx(db);worker_current_scope(*this,db,expected,true);
    const auto owner=run(expected.owner_run_id);
    AgentWorkerJournalValue result;
    switch(query.kind){
    case AgentWorkerJournalReadKind::run:
        if(query.target!=expected.owner_run_id&&query.target!=expected.root_run_id)throw Conflict("Worker cannot read a foreign run");result=run(query.target);break;
    case AgentWorkerJournalReadKind::session:
        if(query.target!=owner.session_id)throw Conflict("Worker cannot read a foreign session");result=session(query.target);break;
    case AgentWorkerJournalReadKind::history:
        if(query.target!=owner.id)throw Conflict("Worker cannot read another run history");result=run_history(owner.id);break;
    case AgentWorkerJournalReadKind::events:
        if(query.target!=owner.id)throw Conflict("Worker cannot read another run events");result=events(owner.id,query.after);break;
    case AgentWorkerJournalReadKind::child_admission:
        if(query.target!=expected.owner_run_id||(!worker_delegated_owner(*this,expected)&&!worker_graph_owner(*this,expected)))throw Conflict("Worker may inspect only its exact child admission");result=child_admission(query.target);break;
    case AgentWorkerJournalReadKind::root_budget:
        if(query.target!=expected.root_run_id)throw Conflict("Worker cannot read another root budget");result=root_budget(query.target);break;
    default:throw std::invalid_argument("Unsupported worker journal read");
    }
    tx.commit();return result;
}
Event Repository::record_agent_worker_model_event(const AgentWorkerModelClaimSpec& spec,const std::string& kind,const std::string& json){
    identifier(kind);if(!kind.starts_with("model.")||kind.size()>64)throw std::invalid_argument("Worker model event kind differs");(void)object_json(json);auto& db=impl_->database;Transaction tx(db);
    const auto claim=read_worker_claim(db,spec.root_run_id,spec.attempt_id);if(worker_claim_spec_json(claim.spec)!=worker_claim_spec_json(spec)||claim.state!="dispatching")throw Conflict("Model event has no live captured dispatch");
    const auto lease=read_worker_lease(db,spec.generation);worker_current_scope(*this,db,lease.spec);
    const auto ordinal=integer(db.execute("SELECT count(*) FROM information WHERE category=? AND json_extract(payload,'$.spec.root_run_id')=? AND json_extract(payload,'$.spec.attempt_id')=?",{std::string(worker_event_category),spec.root_run_id,spec.attempt_id}).rows.at(0).at(0));const auto key=worker_event_key(spec.root_run_id,spec.attempt_id,ordinal);
    const auto event=impl_->event(spec.owner_run_id,kind,json);const auto receipt=Json{{"version",1},{"spec",worker_claim_spec_json(spec)},{"ordinal",ordinal},{"event_sequence",event.sequence}}.dump();
    changed_one(db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?)",{std::string(worker_event_category),key,receipt}));tx.commit();return event;
}
Event Repository::agent_worker_model_event(const AgentWorkerLeaseSpec& lease,const std::string& attempt,std::int64_t ordinal,const std::string& kind,const std::string& json){
    const auto key=worker_event_key(lease.root_run_id,attempt,ordinal);if(kind.size()>64||json.size()>2*1024*1024)throw std::invalid_argument("Invalid worker model event acknowledgement");auto& db=impl_->database;Transaction tx(db);worker_current_scope(*this,db,lease);
    const auto claim=read_worker_claim(db,lease.root_run_id,attempt);if(claim.spec.owner_run_id!=lease.owner_run_id||claim.spec.generation!=lease.generation||(claim.state!="dispatching"&&claim.state!="responded"))throw Conflict("Worker cannot acknowledge a foreign or uncertain stream");
    const auto receipts=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_event_category),key}).rows;if(receipts.size()!=1)throw NotFound("Captured worker model event not found");const auto receipt=Json::parse(text(receipts[0][0]));const auto sequence=receipt.at("event_sequence").get<std::int64_t>();
    if(receipt!=Json{{"version",1},{"spec",worker_claim_spec_json(claim.spec)},{"ordinal",ordinal},{"event_sequence",sequence}})throw DatabaseError("Stored model event binding differs");const auto rows=db.execute("SELECT run_id,kind,payload FROM events WHERE seq=?",{sequence}).rows;
    if(rows.size()!=1||text(rows[0][0])!=lease.owner_run_id||text(rows[0][1])!=kind||text(rows[0][2])!=json)throw Conflict("Worker event differs from actual backend observation");Event result{sequence,lease.owner_run_id,kind,json};tx.commit();return result;
}
AgentWorkerPlanningClaimRecord Repository::claim_agent_worker_planning(const AgentWorkerLeaseSpec& lease,const std::string& attempt,const std::string& call){
    bounded_identity(attempt);bounded_identity(call,256);auto& db=impl_->database;Transaction tx(db);worker_current_scope(*this,db,lease);
    const auto root=run(lease.root_run_id);const auto budget=root_budget(root.id);
    if(lease.owner_run_id!=root.id||root.graph_root||!root.parent_id.empty()||lease.workspace_id=="workspace:none"||budget.spec.policy_id!="native.dynamic-plan"||budget.spec.policy_revision!=1)throw Conflict("Worker planning requires its captured dynamic root");
    const auto caps=dynamic_capabilities(root.id);const auto segment=dynamic_budget_segment(root.id);
    if(!caps.catalogue_finalized||caps.workspace_identity!=lease.workspace_id||Json::parse(caps.provider_identity_json)!=Json::parse(lease.provider_identity_json)||segment.state!="open")throw Conflict("Worker planning lacks finalized native capabilities and an active segment");
    const auto model=read_worker_claim(db,root.id,attempt);
    if(model.spec.generation!=lease.generation||model.spec.owner_run_id!=lease.owner_run_id||model.state!="responded"||model.actual_assistant_json.empty())throw Conflict("Worker planning lacks its actual captured model receipt");
    const auto reservation=db.execute("SELECT state,purpose,COALESCE(actual_assistant_json,'') FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=? AND attempt_id=?",{root.id,lease.owner_run_id,attempt}).rows;
    const auto latest=db.execute("SELECT json_extract(payload,'$.attempt_id') FROM events WHERE run_id=? AND kind='budget.model_call.reserved' AND json_extract(payload,'$.root_run_id')=? ORDER BY seq DESC LIMIT 1",{lease.owner_run_id,root.id}).rows;
    if(reservation.size()!=1||text(reservation[0][0])!="finished"||text(reservation[0][1])!="inference"||text(reservation[0][2])!=model.actual_assistant_json||latest.size()!=1||text(latest[0][0])!=attempt)throw Conflict("Worker planning requires its latest fully retired inference");
    const auto actual=decode_agent_model_completion(model.actual_response_json);
    if(actual.finish_reason!="tool_calls"||actual.tool_calls.size()!=1||actual.tool_calls.front().id!=call)throw Conflict("Worker planning requires one exact provider planning call");const auto& tool=actual.tool_calls.front();
    if(tool.name!="plan_tasks"&&tool.name!="revise_plan"&&tool.name!="inspect_plan")throw Conflict("Worker planning call is not a native planning tool");(void)object_json(tool.arguments_json,4*1024*1024);
    AgentWorkerPlanningClaimRecord record{model.spec,tool.id,tool.name,tool.arguments_json,caps.backend_identity,segment.id,"entered"};
    const auto inserted=db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{std::string(worker_planning_category),worker_claim_key(root.id,attempt),worker_planning_json(record).dump()});if(inserted.affected_rows!=1)throw Conflict("Worker planning was already entered; no replay is authorized");
    impl_->event(lease.owner_run_id,"worker.planning.entered",Json{{"generation",lease.generation},{"attempt_id",attempt},{"call_id",call},{"segment_id",segment.id}}.dump());tx.commit();return record;
}
std::string Repository::commit_agent_worker_plan_inspection(const AgentWorkerLeaseSpec& lease,const std::string& attempt){
    bounded_identity(attempt);auto& db=impl_->database;Transaction tx(db);worker_current_scope(*this,db,lease);auto record=read_worker_planning(db,lease.root_run_id,attempt);
    if(record.model.generation!=lease.generation||record.model.owner_run_id!=lease.owner_run_id||record.tool_name!="inspect_plan"||!Json::parse(record.arguments_json).empty())throw Conflict("Worker inspection differs from its captured call");
    if(record.state=="inspected"){const auto saved=db.execute("SELECT payload FROM events WHERE run_id=? AND kind='conversation.tool_turn' AND json_extract(payload,'$.inspected_plan_call_id')=? AND json_extract(payload,'$.origin_attempt_id')=?",{lease.owner_run_id,record.call_id,attempt}).rows;const auto report=Json::parse(text(saved.at(0).at(0))).at("inspection_result_json").get<std::string>();tx.commit();return report;}
    if(record.state!="entered")throw Conflict("Uncertain or settled planning cannot become an inspection");
    const auto latest=db.execute("SELECT json_extract(payload,'$.attempt_id') FROM events WHERE run_id=? AND kind='budget.model_call.reserved' AND json_extract(payload,'$.root_run_id')=? ORDER BY seq DESC LIMIT 1",{lease.owner_run_id,lease.root_run_id}).rows;if(latest.size()!=1||text(latest[0][0])!=attempt)throw Conflict("Worker inspection is no longer the latest model attempt");
    const auto caps=dynamic_capabilities(lease.root_run_id);const auto segment=dynamic_budget_segment(lease.root_run_id);if(record.backend_identity!=caps.backend_identity||!caps.catalogue_finalized||record.segment_id!=segment.id||segment.state!="open")throw Conflict("Worker inspection lost its active native generation");
    const auto model=read_worker_claim(db,lease.root_run_id,attempt);if(model.spec.generation!=lease.generation||model.state!="responded"||model.actual_assistant_json.empty())throw Conflict("Worker inspection lost its canonical assistant");
    const auto plan=dynamic_plan_for_owner(lease.owner_run_id);const auto report=plan?dynamic_plan_report(*plan):std::string(R"({"source":"native_dynamic_plan","plan":null})");const auto activity=lease.owner_run_id+":plan:"+record.call_id;
    const auto starts=db.execute("SELECT payload FROM events WHERE run_id=? AND kind='tool.started' AND json_extract(payload,'$.activity_id')=?",{lease.owner_run_id,activity}).rows;
    if(starts.empty())impl_->event(lease.owner_run_id,"tool.started",Json{{"activity_id",activity},{"call_id",record.call_id},{"name","inspect_plan"},{"arguments_json",record.arguments_json}}.dump());
    else {if(starts.size()!=1)throw Conflict("Worker inspection has ambiguous starts");const auto started=Json::parse(text(starts[0][0]));if(started.value("call_id",std::string{})!=record.call_id||started.value("name",std::string{})!="inspect_plan"||(started.contains("arguments_json")?started.at("arguments_json")!=record.arguments_json:(!started.contains("arguments")||started.at("arguments")!=Json::parse(record.arguments_json))))throw Conflict("Worker inspection start differs");}
    if(!db.execute("SELECT seq FROM events WHERE run_id=? AND kind IN ('tool.completed','tool.failed') AND json_extract(payload,'$.activity_id')=?",{lease.owner_run_id,activity}).rows.empty())throw Conflict("Worker inspection already has an unmatched result");
    const auto owner=run(lease.owner_run_id);impl_->event(owner.id,"tool.completed",Json{{"activity_id",activity},{"call_id",record.call_id},{"name","inspect_plan"},{"data",Json::parse(report)}}.dump());impl_->message(owner,"assistant",model.actual_assistant_json);impl_->message(owner,"tool",Json{{"content",report},{"tool_call_id",record.call_id}}.dump());impl_->event(owner.id,"conversation.tool_turn",Json{{"inspected_plan_call_id",record.call_id},{"origin_attempt_id",attempt},{"inspection_result_json",report}}.dump());record.state="inspected";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_planning_json(record).dump(),std::string(worker_planning_category),worker_claim_key(lease.root_run_id,attempt)}));tx.commit();return report;
}
AgentWorkerPlanningClaimRecord Repository::agent_worker_planning_claim(const std::string& root,const std::string& attempt){return read_worker_planning(impl_->database,root,attempt);}
AgentWorkerModelClaimRecord Repository::agent_worker_model_claim(const std::string& root,const std::string& attempt){return read_worker_claim(impl_->database,root,attempt);}
AgentWorkerModelClaimRecord Repository::claim_agent_worker_model(const AgentWorkerModelClaimSpec& spec){
    (void)worker_claim_spec_json(spec);auto& db=impl_->database;Transaction tx(db);const auto lease=read_worker_lease(db,spec.generation);
    if(lease.state!="active"||lease.spec.root_run_id!=spec.root_run_id||lease.spec.owner_run_id!=spec.owner_run_id)throw Conflict("Worker model dispatch has no active scoped lease");worker_running_scope(*this,db,lease.spec);
    const auto current=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_current_category),spec.owner_run_id}).rows;if(current.empty()||Json::parse(text(current[0][0])).at("generation")!=spec.generation)throw Conflict("Worker generation is stale");
    worker_require_tool_turns(db,lease.spec);
    const auto funded=worker_attempt_generation(db,spec.root_run_id,spec.owner_run_id,spec.attempt_id);if(funded&&*funded!=spec.generation)throw Conflict("Worker cannot dispatch another generation's funded attempt");
    if(!funded){
        // Trusted backend-funded compatibility attempts must also originate
        // during this lease. A replacement cannot adopt pre-admission work.
        const auto origin=db.execute("SELECT seq FROM events WHERE run_id=? AND kind='budget.model_call.reserved' AND json_extract(payload,'$.root_run_id')=? AND json_extract(payload,'$.attempt_id')=?",{spec.owner_run_id,spec.root_run_id,spec.attempt_id}).rows;
        const auto admission=db.execute("SELECT seq FROM events WHERE run_id=? AND kind='worker.lease.admitted' AND json_extract(payload,'$.generation')=?",{spec.owner_run_id,spec.generation}).rows;
        if(origin.size()!=1||admission.size()!=1||integer(origin[0][0])<=integer(admission[0][0]))throw Conflict("Worker backend funding predates its current generation");
    }
    const auto call=db.execute("SELECT state,purpose,COALESCE(inference_step_id,'') FROM agent_model_call_reservations WHERE root_run_id=? AND attempt_id=? AND owner_run_id=?",{spec.root_run_id,spec.attempt_id,spec.owner_run_id}).rows;if(call.empty()||text(call[0][0])!="started"||text(call[0][1])!="inference")throw Conflict("Worker requires its exact started inference attempt");
    if(!text(call[0][2]).empty()){const auto projection=db.execute("SELECT input_binding FROM inference_attempt_members WHERE root_run_id=? AND attempt_id=? AND step_id=? AND outcome IS NULL",{spec.root_run_id,spec.attempt_id,text(call[0][2])}).rows;if(projection.empty()||text(projection[0][0])!=spec.request_binding)throw Conflict("Worker inference projection differs from its funded attempt");}
    AgentWorkerModelClaimRecord record{spec,"dispatching",{}, {}};const auto inserted=db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{std::string(worker_claim_category),worker_claim_key(spec.root_run_id,spec.attempt_id),worker_claim_json(record).dump()});if(inserted.affected_rows!=1)throw Conflict("Worker model attempt was already dispatched; no retry is authorized");
    impl_->event(spec.owner_run_id,"worker.model.dispatching",Json{{"generation",spec.generation},{"attempt_id",spec.attempt_id},{"request_binding",spec.request_binding}}.dump());tx.commit();return record;
}

AgentWorkerModelClaimRecord Repository::interrupt_agent_worker_model(const AgentWorkerModelClaimSpec& spec,AgentWorkerModelFailure failure){
    std::string code;switch(failure){case AgentWorkerModelFailure::unknown:break;case AgentWorkerModelFailure::cancelled:code="cancelled";break;case AgentWorkerModelFailure::deadline:code="deadline";break;case AgentWorkerModelFailure::provider_failure:code="provider_failure";break;case AgentWorkerModelFailure::dispatch_error:code="dispatch_error";break;case AgentWorkerModelFailure::context_overflow:code="context_overflow";break;default:throw std::invalid_argument("Invalid backend worker model failure");}
    (void)worker_claim_spec_json(spec);auto& db=impl_->database;Transaction tx(db);auto record=read_worker_claim(db,spec.root_run_id,spec.attempt_id);if(worker_claim_spec_json(record.spec)!=worker_claim_spec_json(spec))throw Conflict("Worker dispatch interruption has a foreign binding");
    if(record.state=="rejected"){if(failure!=AgentWorkerModelFailure::context_overflow)throw Conflict("Known context rejection cannot become uncertainty");tx.commit();return record;}
    if(failure==AgentWorkerModelFailure::context_overflow){
        if(record.state!="dispatching")throw Conflict("Context rejection requires its live dispatch");
        const auto proof=db.execute("SELECT m.step_id FROM inference_attempt_members m JOIN inference_steps s ON s.id=m.step_id WHERE m.root_run_id=? AND m.attempt_id=? AND s.owner_run_id=? AND s.state='context_overflow' AND m.outcome='context_overflow' AND m.failure_code='context_length_exceeded'",{spec.root_run_id,spec.attempt_id,spec.owner_run_id}).rows;
        if(proof.size()!=1)throw Conflict("Context rejection lacks its typed no-output proof");
        record.state="rejected";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_claim_json(record).dump(),std::string(worker_claim_category),worker_claim_key(spec.root_run_id,spec.attempt_id)}));impl_->event(spec.owner_run_id,"worker.model.rejected",Json{{"generation",spec.generation},{"attempt_id",spec.attempt_id},{"failure_code",code}}.dump());tx.commit();return record;
    }
    if(record.state=="responded")throw Conflict("Acknowledged worker response cannot become uncertain");if(record.state=="uncertain"){tx.commit();return record;}record.state="uncertain";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_claim_json(record).dump(),std::string(worker_claim_category),worker_claim_key(spec.root_run_id,spec.attempt_id)}));auto detail=Json{{"generation",spec.generation},{"attempt_id",spec.attempt_id}};if(!code.empty())detail["failure_code"]=code;impl_->event(spec.owner_run_id,"worker.model.uncertain",detail.dump());tx.commit();return record;
}

AgentWorkerModelClaimRecord Repository::record_agent_worker_model_response(const AgentWorkerModelClaimSpec& spec,const std::string& actual,const std::string& assistant){
    (void)worker_claim_spec_json(spec);(void)object_json(actual,8*1024*1024);auto& db=impl_->database;Transaction tx(db);auto record=read_worker_claim(db,spec.root_run_id,spec.attempt_id);if(worker_claim_spec_json(record.spec)!=worker_claim_spec_json(spec))throw Conflict("Worker response has a foreign attempt binding");
    const auto lease=read_worker_lease(db,spec.generation);if(lease.spec.root_run_id!=spec.root_run_id||lease.spec.owner_run_id!=spec.owner_run_id)throw Conflict("Worker response owner differs");
    if(!assistant.empty()){
        const auto receipt=Json::parse(object_json(assistant,8*1024*1024));const auto& elapsed=receipt.at("elapsed_ms");
        if(!elapsed.is_number_integer()||elapsed<0||elapsed>9007199254740991LL)throw std::invalid_argument("Invalid backend model timing");std::optional<std::int64_t> first;
        if(receipt.contains("first_token_ms")){const auto& token=receipt.at("first_token_ms");if(!token.is_number_integer()||token<0||token>elapsed)throw std::invalid_argument("Invalid backend first-token timing");first=token.get<std::int64_t>();}
        const auto identity=Json::parse(lease.spec.provider_identity_json);std::string context;
        if(receipt.contains("provider_context")){if(receipt.at("provider_context")!=identity)throw Conflict("Backend assistant provider context differs");context=receipt.at("provider_context").dump();}
        if(format_agent_model_assistant(decode_agent_model_completion(actual),identity.at("model_id").get<std::string>(),elapsed.get<std::int64_t>(),first,context)!=assistant)throw Conflict("Backend assistant differs from actual model response");
    }
    if(record.state=="rejected")throw Conflict("Rejected worker inference cannot acquire a response");
    if(record.state=="responded"){if(record.actual_response_json!=actual||(!assistant.empty()&&record.actual_assistant_json!=assistant))throw Conflict("Acknowledged worker response changed");tx.commit();return record;}
    record.state="responded";record.actual_response_json=actual;record.response_binding=context_digest(actual);
    if(!assistant.empty()){record.actual_assistant_json=assistant;record.assistant_binding=context_digest(assistant);}
    changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_claim_json(record).dump(),std::string(worker_claim_category),worker_claim_key(spec.root_run_id,spec.attempt_id)}));
    impl_->event(spec.owner_run_id,"worker.model.responded",Json{{"generation",spec.generation},{"attempt_id",spec.attempt_id},{"response_binding",record.response_binding}}.dump());tx.commit();return record;
}
Event Repository::agent_worker_tool_event(const AgentWorkerLeaseSpec& lease,const std::string& attempt,std::int64_t ordinal,bool returned){return agent_worker_tool_event_impl(lease,attempt,ordinal,returned,false);}
Event Repository::agent_worker_workspace_tool_event(const AgentWorkerLeaseSpec& lease,const std::string& attempt,std::int64_t ordinal,bool returned){return agent_worker_tool_event_impl(lease,attempt,ordinal,returned,true);}
Event Repository::agent_worker_tool_event_impl(const AgentWorkerLeaseSpec& lease,const std::string& attempt,std::int64_t ordinal,bool returned,bool allow_files){
    if(ordinal<0||ordinal>=64)throw std::invalid_argument("Invalid worker tool activity ordinal");auto& db=impl_->database;Transaction tx(db);worker_current_scope(*this,db,lease);const auto model=read_worker_claim(db,lease.root_run_id,attempt);if(model.state!="responded"||model.actual_assistant_json.empty()||model.spec.generation!=lease.generation||model.spec.owner_run_id!=lease.owner_run_id)throw Conflict("Worker activity lacks its actual captured model response");const auto actual=decode_agent_model_completion(model.actual_response_json);
    if(static_cast<std::size_t>(ordinal)>=actual.tool_calls.size())throw Conflict("Worker tool activity has no actual provider call");const auto& tool=actual.tool_calls[static_cast<std::size_t>(ordinal)];const bool callable=worker_callable_tool(db,lease,tool.name),file=!callable&&is_agent_file_tool(tool.name),process=!callable&&is_agent_process_tool(tool.name),remote=!callable&&is_agent_remote_tool(tool.name);if((file||process||remote||callable)&&!allow_files)throw Conflict("Read-only worker activity cannot acknowledge file tools");const auto args=Json::parse(object_json(tool.arguments_json,(file||process||remote)?4*1024*1024:2*1024*1024));
    AgentWorkspaceInvocation call{1,lease.owner_run_id,attempt,tool.id,lease.workspace_id,lease.workspace_revision,callable?lease.workspace_id:args.value("folder_id",lease.workspace_id),tool.name,tool.arguments_json};
    AgentFileToolInvocation file_call;file_call.run_id=call.run_id;file_call.attempt_id=call.attempt_id;file_call.call_id=call.call_id;file_call.workspace_id=call.workspace_id;file_call.workspace_revision=call.workspace_revision;file_call.folder_id=call.folder_id;file_call.tool_name=call.tool_name;file_call.arguments_json=call.arguments_json;
    AgentProcessToolInvocation process_call;process_call.run_id=call.run_id;process_call.attempt_id=call.attempt_id;process_call.call_id=call.call_id;process_call.workspace_id=call.workspace_id;process_call.workspace_revision=call.workspace_revision;process_call.folder_id=call.folder_id;process_call.tool_name=call.tool_name;process_call.arguments_json=call.arguments_json;
    AgentRemoteToolInvocation remote_call;remote_call.run_id=call.run_id;remote_call.attempt_id=call.attempt_id;remote_call.call_id=call.call_id;remote_call.workspace_id=call.workspace_id;remote_call.workspace_revision=call.workspace_revision;remote_call.tool_name=call.tool_name;remote_call.arguments_json=call.arguments_json;remote_call.tool_index=static_cast<std::uint32_t>(ordinal);
    AgentCallableInvocation callable_call;callable_call.run_id=call.run_id;callable_call.attempt_id=call.attempt_id;callable_call.call_id=call.call_id;callable_call.workspace_id=call.workspace_id;callable_call.workspace_revision=call.workspace_revision;callable_call.tool_name=call.tool_name;callable_call.arguments_json=call.arguments_json;callable_call.tool_index=static_cast<std::uint32_t>(ordinal);
    const auto authority=callable?worker_callable_authority(*this,db,lease,callable_call):remote?worker_remote_authority(*this,db,lease,remote_call):process?worker_process_authority(*this,db,lease,process_call):file?worker_file_authority(*this,db,lease,file_call):worker_read_authority(*this,db,lease,call);
    const auto key_for=[&](bool completed){return context_digest(Json::array({lease.root_run_id,attempt,ordinal,completed}).dump());};
    std::string kind="tool.started";Json payload={{"activity_id",lease.owner_run_id+":"+attempt+":"+std::to_string(ordinal)},{"call_id",tool.id},{"name",tool.name}};
    if(returned){if(db.execute("SELECT id FROM information WHERE category=? AND id=?",{std::string(worker_tool_event_category),key_for(false)}).rows.empty())throw Conflict("Worker cannot return an unstarted tool activity");if(callable){const auto result=read_worker_callable(db,lease.root_run_id,attempt,tool.id);if(result.state!="returned"||result.invocation.serialize()!=callable_call.serialize()||worker_claim_spec_json(result.model)!=worker_claim_spec_json(authority))throw Conflict("Worker activity lacks its actual returned native callable result");kind=result.success?"tool.completed":"tool.failed";payload["data"]=Json::parse(result.actual_output_json);}else if(remote){const auto result=read_worker_remote(db,lease.root_run_id,attempt,tool.id);if(result.state!="returned"||result.invocation.serialize()!=remote_call.serialize()||worker_claim_spec_json(result.model)!=worker_claim_spec_json(authority))throw Conflict("Worker activity lacks its actual returned native remote result");kind=result.success?"tool.completed":"tool.failed";payload["data"]=Json::parse(result.actual_output_json);}else if(process){const auto result=read_worker_process(db,lease.root_run_id,attempt,tool.id);if(result.state!="returned"||result.invocation.serialize()!=process_call.serialize()||worker_claim_spec_json(result.model)!=worker_claim_spec_json(authority))throw Conflict("Worker activity lacks its actual returned native process result");kind=result.success?"tool.completed":"tool.failed";payload["data"]=Json::parse(result.actual_output_json);}else if(file){const auto result=read_worker_file(db,lease.root_run_id,attempt,tool.id);if(result.state!="returned"||result.invocation.serialize()!=file_call.serialize()||worker_claim_spec_json(result.model)!=worker_claim_spec_json(authority))throw Conflict("Worker activity lacks its actual returned native file result");kind=result.success?"tool.completed":"tool.failed";payload["data"]=Json::parse(result.actual_output_json);}else{const auto read=read_worker_read(db,lease.root_run_id,attempt,tool.id);if(read.state!="returned"||read.invocation.serialize()!=call.serialize()||worker_claim_spec_json(read.model)!=worker_claim_spec_json(authority))throw Conflict("Worker tool activity lacks its actual returned native read");kind=read.success?"tool.completed":"tool.failed";payload["data"]=Json::parse(read.actual_output_json);}}
    else payload["arguments"]=args;
    const auto json=payload.dump(),key=key_for(returned);const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND id=?",{std::string(worker_tool_event_category),key}).rows;
    if(!rows.empty()){const auto saved=Json::parse(text(rows[0][0]));const auto sequence=saved.at("event_sequence").get<std::int64_t>();if(saved!=Json{{"version",1},{"model",worker_claim_spec_json(authority)},{"ordinal",ordinal},{"returned",returned},{"event_sequence",sequence}})throw DatabaseError("Stored worker activity binding differs");const auto events=db.execute("SELECT run_id,kind,payload FROM events WHERE seq=?",{sequence}).rows;if(events.size()!=1||text(events[0][0])!=lease.owner_run_id||text(events[0][1])!=kind||text(events[0][2])!=json)throw DatabaseError("Stored worker activity differs from its actual native fact");tx.commit();return {sequence,lease.owner_run_id,kind,json};}
    const auto event=impl_->event(lease.owner_run_id,kind,json);changed_one(db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?)",{std::string(worker_tool_event_category),key,Json{{"version",1},{"model",worker_claim_spec_json(authority)},{"ordinal",ordinal},{"returned",returned},{"event_sequence",event.sequence}}.dump()}));tx.commit();return event;
}
AgentWorkerToolTurnRecord Repository::commit_agent_worker_read_turn(const AgentWorkerLeaseSpec& lease,const std::string& attempt){
    return commit_agent_worker_tool_turn(lease,attempt,false);
}
AgentWorkerToolTurnRecord Repository::commit_agent_worker_workspace_turn(const AgentWorkerLeaseSpec& lease,const std::string& attempt){
    return commit_agent_worker_tool_turn(lease,attempt,true);
}
AgentWorkerToolTurnRecord Repository::commit_agent_worker_tool_turn(const AgentWorkerLeaseSpec& lease,const std::string& attempt,bool allow_files){
    auto& db=impl_->database;Transaction tx(db);worker_current_scope(*this,db,lease);
    if(!worker_agent_owner(*this,lease)||(allow_files&&!worker_effect_owner(*this,lease)))throw Conflict("Worker tool turn differs from its exact read-only or coding admission");
    const auto model=read_worker_claim(db,lease.root_run_id,attempt);if(model.spec.owner_run_id!=lease.owner_run_id||model.spec.generation!=lease.generation||model.state!="responded"||model.actual_assistant_json.empty())throw Conflict("Worker read turn has no actual assistant receipt");
    const auto actual=decode_agent_model_completion(model.actual_response_json);if(actual.finish_reason!="tool_calls"||actual.tool_calls.empty()||actual.tool_calls.size()>64)throw Conflict("Worker read turn requires a complete supported tool batch");
    // Check the admitted vocabulary even for an already committed turn. A read
    // channel must never acknowledge a coding turn through idempotent lookup.
    for(const auto& call:actual.tool_calls){const bool callable=worker_callable_tool(db,lease,call.name);if((callable&&!allow_files)||(!callable&&!worker_read_tool(call.name)&&!(call.name=="delegate_tasks"&&lease.root_run_id==lease.owner_run_id&&root_budget(lease.root_run_id).spec.policy_id=="native.delegation")&&!(allow_files&&(is_agent_file_tool(call.name)||is_agent_process_tool(call.name)||is_agent_remote_tool(call.name)))))throw Conflict("Worker turn contains an unsupported tool");}
    if(!db.execute("SELECT id FROM information WHERE category=? AND id=?",{std::string(worker_turn_category),worker_claim_key(lease.root_run_id,attempt)}).rows.empty()){const auto prior=read_worker_turn(db,model.spec);tx.commit();return prior;}
    std::set<std::string> calls;std::vector<std::string> outputs;
    for(const auto& call:actual.tool_calls){if(!calls.insert(call.id).second)throw Conflict("Worker read turn has ambiguous provider call identities");
        if(worker_callable_tool(db,lease,call.name)){
            const auto callable=read_worker_callable(db,lease.root_run_id,attempt,call.id);
            if(callable.state!="returned"||worker_claim_spec_json(callable.model)!=worker_claim_spec_json(model.spec)||callable.invocation.tool_name!=call.name||callable.invocation.arguments_json!=call.arguments_json)throw Conflict("Worker workspace turn has an unfinished or changed actual callable result");
            (void)worker_callable_authority(*this,db,lease,callable.invocation);outputs.push_back(Json{{"content",callable.actual_output_json},{"tool_call_id",call.id}}.dump());continue;
        }
        if(is_agent_remote_tool(call.name)){
            const auto remote=read_worker_remote(db,lease.root_run_id,attempt,call.id);
            if(remote.state!="returned"||worker_claim_spec_json(remote.model)!=worker_claim_spec_json(model.spec)||remote.invocation.tool_name!=call.name||remote.invocation.arguments_json!=call.arguments_json)throw Conflict("Worker workspace turn has an unfinished or changed actual remote result");
            (void)worker_remote_authority(*this,db,lease,remote.invocation);outputs.push_back(Json{{"content",remote.actual_output_json},{"tool_call_id",call.id}}.dump());continue;
        }
        if(is_agent_process_tool(call.name)){
            const auto process=read_worker_process(db,lease.root_run_id,attempt,call.id);
            if(process.state!="returned"||worker_claim_spec_json(process.model)!=worker_claim_spec_json(model.spec)||process.invocation.tool_name!=call.name||process.invocation.arguments_json!=call.arguments_json)throw Conflict("Worker workspace turn has an unfinished or changed actual process result");
            (void)worker_process_authority(*this,db,lease,process.invocation);
            outputs.push_back(Json{{"content",process.actual_output_json},{"tool_call_id",call.id}}.dump());continue;
        }
        if(is_agent_file_tool(call.name)){
            const auto file=read_worker_file(db,lease.root_run_id,attempt,call.id);
            if(file.state!="returned"||worker_claim_spec_json(file.model)!=worker_claim_spec_json(model.spec)||file.invocation.tool_name!=call.name||file.invocation.arguments_json!=call.arguments_json)throw Conflict("Worker workspace turn has an unfinished or changed actual file result");
            (void)worker_file_authority(*this,db,lease,file.invocation);
            outputs.push_back(Json{{"content",file.actual_output_json},{"tool_call_id",call.id}}.dump());continue;
        }
        const auto read=read_worker_read(db,lease.root_run_id,attempt,call.id);
        if(read.state!="returned"||worker_claim_spec_json(read.model)!=worker_claim_spec_json(model.spec)||read.invocation.tool_name!=call.name||read.invocation.arguments_json!=call.arguments_json)throw Conflict("Worker read turn has an unfinished or changed actual result");
        (void)worker_read_authority(*this,db,lease,read.invocation);outputs.push_back(Json{{"content",read.actual_output_json},{"tool_call_id",call.id}}.dump());
    }
    const auto current=run(lease.owner_run_id);std::optional<SkillSelections> selected;std::set<std::string> activations;
    if(!db.execute("SELECT run_id FROM run_skills WHERE run_id=?",{current.id}).rows.empty())selected=run_skills(current.id);
    for(std::size_t index=0;index<actual.tool_calls.size();++index){const auto& call=actual.tool_calls[index];if(call.name!="load_skill"||worker_callable_tool(db,lease,call.name))continue;if(!selected)throw Conflict("Worker skill activation lacks captured run selections");activations.insert(call.id);const auto result=Json::parse(Json::parse(outputs[index]).at("content").get<std::string>());if(result.contains("activation")){const auto id=Json::parse(object_json(call.arguments_json,4096)).at("id").get<std::string>();if(std::find(selected->ids.begin(),selected->ids.end(),id)==selected->ids.end())selected->ids.push_back(id);}}
    impl_->apply_skill_turn(*this,current,Json::parse(model.actual_assistant_json),outputs,selected,&activations);AgentWorkerToolTurnRecord turn{model.spec};
    const auto append=[&](const std::string& role,const std::string& json){impl_->message(current,role,json);turn.message_sequences.push_back(integer(db.execute("SELECT message_seq FROM task_messages WHERE run_id=? ORDER BY message_seq DESC LIMIT 1",{current.id}).rows.at(0).at(0)));};
    append("assistant",model.actual_assistant_json);for(const auto& output:outputs)append("tool",output);turn.messages_binding=context_digest(worker_turn_messages(db,turn).dump());
    impl_->event(current.id,"conversation.tool_turn","{}");impl_->event(current.id,"worker.tool_turn.committed",Json{{"generation",lease.generation},{"attempt_id",attempt},{"messages_binding",turn.messages_binding}}.dump());
    changed_one(db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?)",{std::string(worker_turn_category),worker_claim_key(lease.root_run_id,attempt),worker_turn_json(turn).dump()}));tx.commit();return turn;
}
AgentWorkerReadClaimRecord Repository::claim_agent_worker_read(const AgentWorkerLeaseSpec& lease,const AgentWorkspaceInvocation& call){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_read_authority(*this,db,lease,call);AgentWorkerReadClaimRecord record{model,call,"awaiting_dispatch"};
    const auto inserted=db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{std::string(worker_read_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id),worker_read_json(record).dump()});if(inserted.affected_rows!=1)throw Conflict("Worker read was already claimed; no replay is authorized");
    impl_->event(lease.owner_run_id,"worker.read.claimed",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id}}.dump());tx.commit();return record;
}
AgentWorkerReadClaimRecord Repository::start_agent_worker_read(const AgentWorkerLeaseSpec& lease,const AgentWorkspaceInvocation& call){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_read_authority(*this,db,lease,call);auto record=read_worker_read(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(worker_claim_spec_json(record.model)!=worker_claim_spec_json(model)||record.invocation.serialize()!=call.serialize()||record.state!="awaiting_dispatch")throw Conflict("Worker read cannot start again or change its binding");
    record.state="executing";record.dispatched=true;changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_read_json(record).dump(),std::string(worker_read_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.read.dispatching",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id}}.dump());tx.commit();return record;
}
AgentWorkerReadClaimRecord Repository::record_agent_worker_read_result(const AgentWorkerLeaseSpec& lease,const AgentWorkspaceInvocation& call,bool success,const std::string& output){
    (void)object_json(output);auto& db=impl_->database;Transaction tx(db);auto record=read_worker_read(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(read_worker_lease(db,lease.generation).spec!=lease||record.model.generation!=lease.generation||record.model.owner_run_id!=lease.owner_run_id||record.invocation.serialize()!=call.serialize()||(success&&!record.dispatched))throw Conflict("Worker read result differs from its captured dispatch");
    if(record.state=="returned"){if(record.success!=success||record.actual_output_json!=output)throw Conflict("Acknowledged worker read result changed");tx.commit();return record;}
    record.state="returned";record.success=success;record.actual_output_json=output;record.output_binding=context_digest(output);changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_read_json(record).dump(),std::string(worker_read_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.read.returned",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"success",success},{"output_binding",record.output_binding}}.dump());tx.commit();return record;
}
AgentWorkerReadClaimRecord Repository::agent_worker_read_claim(const std::string& root,const std::string& attempt,const std::string& call){return read_worker_read(impl_->database,root,attempt,call);}
AgentWorkerFileClaimRecord Repository::claim_agent_worker_file(const AgentWorkerLeaseSpec& lease,const AgentFileToolInvocation& call){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_file_authority(*this,db,lease,call);AgentWorkerFileClaimRecord record{model,call,"awaiting_dispatch"};
    const auto inserted=db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{std::string(worker_file_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id),worker_file_json(record).dump()});if(inserted.affected_rows!=1)throw Conflict("Worker file call was already claimed; no replay is authorized");
    impl_->event(lease.owner_run_id,"worker.file.claimed",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id}}.dump());tx.commit();return record;
}
AgentWorkerFileClaimRecord Repository::start_agent_worker_file(const AgentWorkerLeaseSpec& lease,const AgentFileToolInvocation& call){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_file_authority(*this,db,lease,call);auto record=read_worker_file(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(worker_claim_spec_json(record.model)!=worker_claim_spec_json(model)||record.invocation.serialize()!=call.serialize()||record.state!="awaiting_dispatch")throw Conflict("Worker file call cannot start again or change its binding");
    record.state="executing";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_file_json(record).dump(),std::string(worker_file_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.file.dispatching",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id}}.dump());tx.commit();return record;
}
AgentWorkerFileClaimRecord Repository::claim_agent_worker_file_effect(const AgentWorkerLeaseSpec& lease,const AgentFileToolInvocation& call,const std::string& id){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_file_authority(*this,db,lease,call);auto record=read_worker_file(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(worker_claim_spec_json(record.model)!=worker_claim_spec_json(model)||record.invocation.serialize()!=call.serialize()||record.state!="executing"||record.operation_ids.size()>=128)throw Conflict("Worker file effect lacks its exact executing invocation");
    const auto op=operation(id);const auto tool=call.tool_name=="create_file"?"create_file":call.tool_name=="edit_file"?"replace_file":"patch_file";
    if(op.state!=OperationState::executing||op.spec.run_id!=lease.owner_run_id||op.spec.workspace!=lease.workspace_id||op.spec.tool!=tool)throw Conflict("Worker file effect lacks its claimed native operation");
    const auto plan=Json::parse(object_json(op.spec.arguments_json)),args=Json::parse(object_json(call.arguments_json,4*1024*1024));
    const auto folder=plan.at("folder_id").get<std::string>();bool allowed=folder==call.folder_id;
    if(call.tool_name=="apply_patch"&&args.contains("patches")&&args.at("patches").is_array())for(const auto& group:args.at("patches"))if(group.is_object()&&group.contains("folder_id")&&group.at("folder_id")==folder)allowed=true;
    if(!allowed||plan.at("workspace_id")!=lease.workspace_id||plan.at("workspace_revision")!=lease.workspace_revision)throw Conflict("Worker file operation differs from the admitted workspace scope");
    const auto binding=Json{{"version",1},{"model",worker_claim_spec_json(model)},{"invocation_binding",context_digest(call.serialize())},{"operation_id",id},{"operation_spec_binding",worker_operation_binding(op)}};
    const auto inserted=db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{std::string(worker_file_effect_category),id,binding.dump()});if(inserted.affected_rows!=1)throw Conflict("Native worker file operation was already bound; no replay is authorized");
    record.operation_ids.push_back(id);changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_file_json(record).dump(),std::string(worker_file_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.file.effect_claimed",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"operation_id",id}}.dump());tx.commit();return record;
}
AgentWorkerFileClaimRecord Repository::record_agent_worker_file_result(const AgentWorkerLeaseSpec& lease,const AgentFileToolInvocation& call,bool success,const std::string& output){
    (void)object_json(output);auto& db=impl_->database;Transaction tx(db);auto record=read_worker_file(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(read_worker_lease(db,lease.generation).spec!=lease||record.model.generation!=lease.generation||record.model.owner_run_id!=lease.owner_run_id||record.invocation.serialize()!=call.serialize()||(success&&(record.operation_ids.empty()||record.state=="awaiting_dispatch")))throw Conflict("Worker file result differs from its captured dispatch");
    if(record.state=="returned"){if(record.success!=success||record.actual_output_json!=output)throw Conflict("Acknowledged worker file result changed");tx.commit();return record;}
    for(const auto& id:record.operation_ids){const auto op=operation(id);const auto binding=worker_file_effect_binding(db,record,id);if(binding.at("operation_spec_binding")!=worker_operation_binding(op)||(op.state!=OperationState::succeeded&&op.state!=OperationState::failed)||(success&&op.state!=OperationState::succeeded))throw Conflict("Worker file result has an unresolved or inconsistent native effect");}
    record.state="returned";record.success=success;record.actual_output_json=output;record.output_binding=context_digest(output);changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_file_json(record).dump(),std::string(worker_file_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.file.returned",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"success",success},{"output_binding",record.output_binding}}.dump());tx.commit();return record;
}
AgentWorkerFileClaimRecord Repository::agent_worker_file_claim(const std::string& root,const std::string& attempt,const std::string& call){return read_worker_file(impl_->database,root,attempt,call);}
AgentWorkerProcessClaimRecord Repository::claim_agent_worker_process(const AgentWorkerLeaseSpec& lease,const AgentProcessToolInvocation& call){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_process_authority(*this,db,lease,call);AgentWorkerProcessClaimRecord record{model,call,"awaiting_dispatch"};
    const auto inserted=db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{std::string(worker_process_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id),worker_process_json(record).dump()});if(inserted.affected_rows!=1)throw Conflict("Worker process call was already claimed; no replay is authorized");
    impl_->event(lease.owner_run_id,"worker.process.claimed",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id}}.dump());tx.commit();return record;
}
AgentWorkerProcessClaimRecord Repository::start_agent_worker_process(const AgentWorkerLeaseSpec& lease,const AgentProcessToolInvocation& call){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_process_authority(*this,db,lease,call);auto record=read_worker_process(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(worker_claim_spec_json(record.model)!=worker_claim_spec_json(model)||record.invocation.serialize()!=call.serialize()||record.state!="awaiting_dispatch")throw Conflict("Worker process call cannot start again or change its binding");
    record.state="executing";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_process_json(record).dump(),std::string(worker_process_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.process.dispatching",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id}}.dump());tx.commit();return record;
}
AgentWorkerProcessClaimRecord Repository::claim_agent_worker_process_effect(const AgentWorkerLeaseSpec& lease,const AgentProcessToolInvocation& call,const std::string& id){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_process_authority(*this,db,lease,call);auto record=read_worker_process(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(worker_claim_spec_json(record.model)!=worker_claim_spec_json(model)||record.invocation.serialize()!=call.serialize()||record.state!="executing"||!record.operation_ids.empty())throw Conflict("Worker process effect lacks its exact executing invocation");
    const auto op=operation(id);const auto plan=Json::parse(object_json(op.spec.arguments_json)),args=Json::parse(object_json(call.arguments_json,4*1024*1024));
    if(op.spec.workspace!=lease.workspace_id||op.spec.tool!="run_process")throw Conflict("Worker process has no matching native job");
    if(call.tool_name=="run_process"){
        if(op.state!=OperationState::executing||op.spec.run_id!=lease.owner_run_id||plan.at("folder_id")!=call.folder_id||plan.at("workspace_id")!=lease.workspace_id||plan.at("workspace_revision")!=lease.workspace_revision||plan.at("profile_id")!=args.at("profile")||plan.value("background",false)!=args.value("background",false)||plan.value("persistent",false)!=args.value("persistent",false))throw Conflict("Worker process operation differs from its admitted scope");
        const auto& actual_args=plan.at("arguments");const auto& requested_args=args.at("arguments");if(!actual_args.is_array()||!requested_args.is_array()||actual_args.size()<requested_args.size()||!std::equal(requested_args.rbegin(),requested_args.rend(),actual_args.rbegin()))throw Conflict("Worker process arguments differ from the admitted provider call");
    }else{
        if(args.at("job_id")!=id||call.folder_id!=lease.workspace_id||!plan.value("background",false))throw Conflict("Worker control differs from its captured job");
        const bool same_run=op.spec.run_id==lease.owner_run_id;const bool shared_service=plan.value("persistent",false)&&run(op.spec.run_id).session_id==run(lease.owner_run_id).session_id&&plan.at("workspace_id")==lease.workspace_id;
        if((call.tool_name=="wait_process"&&!same_run&&!shared_service)||(call.tool_name=="stop_process"&&!shared_service))throw Conflict("Worker control cannot acquire another job owner");
    }
    const auto binding=Json{{"version",1},{"model",worker_claim_spec_json(model)},{"invocation_binding",context_digest(call.serialize())},{"operation_id",id},{"operation_spec_binding",worker_operation_binding(op)}};
    const auto inserted=db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{std::string(worker_process_effect_category),worker_process_effect_key(call,id),binding.dump()});if(inserted.affected_rows!=1)throw Conflict("Native worker process operation was already bound; no replay is authorized");
    record.operation_ids.push_back(id);changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_process_json(record).dump(),std::string(worker_process_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.process.effect_claimed",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"operation_id",id}}.dump());tx.commit();return record;
}
AgentWorkerProcessClaimRecord Repository::record_agent_worker_process_result(const AgentWorkerLeaseSpec& lease,const AgentProcessToolInvocation& call,bool success,const std::string& output){
    (void)object_json(output);auto& db=impl_->database;Transaction tx(db);auto record=read_worker_process(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(read_worker_lease(db,lease.generation).spec!=lease||record.model.generation!=lease.generation||record.model.owner_run_id!=lease.owner_run_id||record.invocation.serialize()!=call.serialize()||(success&&(record.operation_ids.empty()||record.state=="awaiting_dispatch")))throw Conflict("Worker process result differs from its captured dispatch");
    if(record.state=="returned"){if(record.success!=success||record.actual_output_json!=output)throw Conflict("Acknowledged worker process result changed");tx.commit();return record;}
    for(const auto& id:record.operation_ids){
        const auto op=operation(id);const auto binding=worker_process_effect_binding(db,record,id);
        if(binding.at("operation_spec_binding")!=worker_operation_binding(op))throw Conflict("Worker process result changed its native effect binding");
        if(call.tool_name=="run_process"){
            const bool background=Json::parse(op.spec.arguments_json).value("background",false);
            const bool terminal=op.state==OperationState::succeeded||op.state==OperationState::failed;
            if((background&&!terminal&&op.state!=OperationState::executing)||(!background&&(!terminal||(success&&op.state!=OperationState::succeeded))))throw Conflict("Worker process result has an unresolved or inconsistent native effect");
        }
    }
    record.state="returned";record.success=success;record.actual_output_json=output;record.output_binding=context_digest(output);changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_process_json(record).dump(),std::string(worker_process_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.process.returned",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"success",success},{"output_binding",record.output_binding}}.dump());tx.commit();return record;
}
AgentWorkerProcessClaimRecord Repository::agent_worker_process_claim(const std::string& root,const std::string& attempt,const std::string& call){return read_worker_process(impl_->database,root,attempt,call);}
AgentWorkerRemoteClaimRecord Repository::claim_agent_worker_remote(const AgentWorkerLeaseSpec& lease,const AgentRemoteToolInvocation& call){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_remote_authority(*this,db,lease,call);AgentWorkerRemoteClaimRecord record{model,call,"awaiting_dispatch"};
    const auto inserted=db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{std::string(worker_remote_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id),worker_remote_json(record).dump()});if(inserted.affected_rows!=1)throw Conflict("Worker remote call was already claimed; no replay is authorized");
    impl_->event(lease.owner_run_id,"worker.remote.claimed",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id}}.dump());tx.commit();return record;
}
AgentWorkerRemoteClaimRecord Repository::start_agent_worker_remote(const AgentWorkerLeaseSpec& lease,const AgentRemoteToolInvocation& call){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_remote_authority(*this,db,lease,call);auto record=read_worker_remote(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(worker_claim_spec_json(record.model)!=worker_claim_spec_json(model)||record.invocation.serialize()!=call.serialize()||record.state!="awaiting_dispatch")throw Conflict("Worker remote call cannot start again or change its binding");
    record.state="executing";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_remote_json(record).dump(),std::string(worker_remote_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.remote.dispatching",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id}}.dump());tx.commit();return record;
}
AgentWorkerRemoteClaimRecord Repository::claim_agent_worker_remote_effect(const AgentWorkerLeaseSpec& lease,const AgentRemoteToolInvocation& call,const std::string& id){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_remote_authority(*this,db,lease,call);auto record=read_worker_remote(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(worker_claim_spec_json(record.model)!=worker_claim_spec_json(model)||record.invocation.serialize()!=call.serialize()||record.state!="executing"||!record.operation_ids.empty())throw Conflict("Worker remote effect lacks its exact executing invocation");
    const auto op=operation(id);const auto plan=Json::parse(object_json(op.spec.arguments_json));
    const auto expected_tool=call.tool_name.starts_with("mcp_")?"mcp_tool":"a2a_delegate";
    if(op.state!=OperationState::executing||op.spec.run_id!=lease.owner_run_id||op.spec.workspace!=lease.workspace_id||op.spec.tool!=expected_tool||plan.at("alias")!=call.tool_name||Json::parse(object_json(plan.at("arguments_json").get<std::string>(),4*1024*1024))!=Json::parse(object_json(call.arguments_json,4*1024*1024)))throw Conflict("Worker remote native operation differs from its captured call");
    const auto binding=Json{{"version",1},{"model",worker_claim_spec_json(model)},{"invocation_binding",context_digest(call.serialize())},{"operation_id",id},{"operation_spec_binding",worker_operation_binding(op)}};
    const auto inserted=db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{std::string(worker_remote_effect_category),worker_remote_effect_key(call,id),binding.dump()});if(inserted.affected_rows!=1)throw Conflict("Native worker remote operation was already bound; no replay is authorized");
    record.operation_ids.push_back(id);changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_remote_json(record).dump(),std::string(worker_remote_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.remote.effect_claimed",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"operation_id",id}}.dump());tx.commit();return record;
}
AgentWorkerRemoteClaimRecord Repository::record_agent_worker_remote_result(const AgentWorkerLeaseSpec& lease,const AgentRemoteToolInvocation& call,bool success,const std::string& output){
    (void)object_json(output);auto& db=impl_->database;Transaction tx(db);auto record=read_worker_remote(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(read_worker_lease(db,lease.generation).spec!=lease||record.model.generation!=lease.generation||record.model.owner_run_id!=lease.owner_run_id||record.invocation.serialize()!=call.serialize()||(success&&(record.operation_ids.empty()||record.state=="awaiting_dispatch")))throw Conflict("Worker remote result differs from its captured dispatch");
    if(record.state=="returned"){if(record.success!=success||record.actual_output_json!=output)throw Conflict("Acknowledged worker remote result changed");tx.commit();return record;}
    for(const auto& id:record.operation_ids){const auto op=operation(id);const auto binding=worker_remote_effect_binding(db,record,id);if(binding.at("operation_spec_binding")!=worker_operation_binding(op)||(op.state!=OperationState::succeeded&&op.state!=OperationState::failed)||(success&&op.state!=OperationState::succeeded))throw Conflict("Worker remote result has an unresolved or inconsistent native effect");}

    record.state="returned";record.success=success;record.actual_output_json=output;record.output_binding=context_digest(output);changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_remote_json(record).dump(),std::string(worker_remote_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.remote.returned",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"success",success},{"output_binding",record.output_binding}}.dump());tx.commit();return record;
}
AgentWorkerRemoteClaimRecord Repository::agent_worker_remote_claim(const std::string& root,const std::string& attempt,const std::string& call){return read_worker_remote(impl_->database,root,attempt,call);}
void Repository::admit_agent_worker_callable_catalogue(const AgentWorkerLeaseSpec& lease,const std::vector<ModelToolDefinition>& tools){
    if(tools.empty()||tools.size()>64)throw std::invalid_argument("Invalid callable catalogue size");auto ordered=tools;std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.name<b.name;});Json definitions=Json::array();std::string previous;for(const auto& tool:ordered){if(tool.name.empty()||tool.name.size()>128||tool.name.find('\0')!=std::string::npos||tool.name==previous||tool.description.size()>65536||tool.input_schema_json.size()>65536)throw std::invalid_argument("Invalid callable catalogue entry");(void)object_json(tool.input_schema_json,65536);previous=tool.name;definitions.push_back({{"name",tool.name},{"description",tool.description},{"input_schema_json",tool.input_schema_json}});}
    auto& db=impl_->database;Transaction tx(db);if(read_worker_lease(db,lease.generation).spec!=lease)throw Conflict("Callable catalogue differs from actual lease");const auto value=Json{{"version",1},{"lease",worker_spec_json(lease)},{"tools",definitions}}.dump();const auto rows=db.execute("SELECT payload FROM information WHERE category='native-agent-worker-callable-catalogues' AND id=?",{lease.generation}).rows;if(!rows.empty()){if(text(rows[0][0])!=value)throw Conflict("Callable catalogue cannot change after admission");tx.commit();return;}
    worker_current_scope(*this,db,lease,true);if(!worker_effect_owner(*this,lease))throw Conflict("Callable catalogue requires ordinary root ownership");changed_one(db.execute("INSERT INTO information(category,id,payload) VALUES('native-agent-worker-callable-catalogues',?,?)",{lease.generation,value}));impl_->event(lease.owner_run_id,"worker.callable.catalogue",Json{{"generation",lease.generation},{"tool_count",tools.size()}}.dump());tx.commit();
}
AgentWorkerCallableClaimRecord Repository::claim_agent_worker_callable(const AgentWorkerLeaseSpec& lease,const AgentCallableInvocation& call){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_callable_authority(*this,db,lease,call);AgentWorkerCallableClaimRecord record{model,call,"awaiting_dispatch"};
    const auto inserted=db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{std::string(worker_callable_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id),worker_callable_json(record).dump()});if(inserted.affected_rows!=1)throw Conflict("Worker callable call was already claimed; no replay is authorized");
    impl_->event(lease.owner_run_id,"worker.callable.claimed",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id}}.dump());tx.commit();return record;
}
AgentWorkerCallableClaimRecord Repository::start_agent_worker_callable(const AgentWorkerLeaseSpec& lease,const AgentCallableInvocation& call){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_callable_authority(*this,db,lease,call);auto record=read_worker_callable(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(worker_claim_spec_json(record.model)!=worker_claim_spec_json(model)||record.invocation.serialize()!=call.serialize()||record.state!="awaiting_dispatch")throw Conflict("Worker callable call cannot start again or change its binding");
    record.state="executing";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_callable_json(record).dump(),std::string(worker_callable_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.callable.dispatching",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id}}.dump());tx.commit();return record;
}
AgentWorkerCallableClaimRecord Repository::claim_agent_worker_callable_effect(const AgentWorkerLeaseSpec& lease,const AgentCallableInvocation& call,const std::string& id){
    auto& db=impl_->database;Transaction tx(db);const auto model=worker_callable_authority(*this,db,lease,call);auto record=read_worker_callable(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(worker_claim_spec_json(record.model)!=worker_claim_spec_json(model)||record.invocation.serialize()!=call.serialize()||record.state!="executing"||!record.operation_ids.empty())throw Conflict("Worker callable effect lacks its exact executing invocation");
    const auto op=operation(id);const auto plan=Json::parse(object_json(op.spec.arguments_json));
    const auto expected_tool="xlang3_tool";
    if(op.state!=OperationState::executing||op.spec.run_id!=lease.owner_run_id||op.spec.workspace!=lease.workspace_id||op.spec.tool!=expected_tool||plan.at("alias")!=call.tool_name||plan.at("arguments")!=Json::parse(object_json(call.arguments_json,4*1024*1024)))throw Conflict("Worker callable native operation differs from its captured call");
    const auto binding=Json{{"version",1},{"model",worker_claim_spec_json(model)},{"invocation_binding",context_digest(call.serialize())},{"operation_id",id},{"operation_spec_binding",worker_operation_binding(op)}};
    const auto inserted=db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{std::string(worker_callable_effect_category),worker_callable_effect_key(call,id),binding.dump()});if(inserted.affected_rows!=1)throw Conflict("Native worker callable operation was already bound; no replay is authorized");
    record.operation_ids.push_back(id);changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_callable_json(record).dump(),std::string(worker_callable_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.callable.effect_claimed",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"operation_id",id}}.dump());tx.commit();return record;
}
AgentWorkerCallableClaimRecord Repository::record_agent_worker_callable_result(const AgentWorkerLeaseSpec& lease,const AgentCallableInvocation& call,bool success,const std::string& output){
    worker_callable_output(output);auto& db=impl_->database;Transaction tx(db);auto record=read_worker_callable(db,lease.root_run_id,call.attempt_id,call.call_id);
    if(read_worker_lease(db,lease.generation).spec!=lease||record.model.generation!=lease.generation||record.model.owner_run_id!=lease.owner_run_id||record.invocation.serialize()!=call.serialize()||(success&&(record.operation_ids.empty()||record.state=="awaiting_dispatch")))throw Conflict("Worker callable result differs from its captured dispatch");
    if(record.state=="returned"){if(record.success!=success||record.actual_output_json!=output)throw Conflict("Acknowledged worker callable result changed");tx.commit();return record;}
    for(const auto& id:record.operation_ids){const auto op=operation(id);const auto binding=worker_callable_effect_binding(db,record,id);if(binding.at("operation_spec_binding")!=worker_operation_binding(op)||(op.state!=OperationState::succeeded&&op.state!=OperationState::failed)||(success&&op.state!=OperationState::succeeded))throw Conflict("Worker callable result has an unresolved or inconsistent native effect");}

    record.state="returned";record.success=success;record.actual_output_json=output;record.output_binding=context_digest(output);changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_callable_json(record).dump(),std::string(worker_callable_category),worker_tool_call_key(lease.root_run_id,call.attempt_id,call.call_id)}));impl_->event(lease.owner_run_id,"worker.callable.returned",Json{{"generation",lease.generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"success",success},{"output_binding",record.output_binding}}.dump());tx.commit();return record;
}
AgentWorkerCallableClaimRecord Repository::agent_worker_callable_claim(const std::string& root,const std::string& attempt,const std::string& call){return read_worker_callable(impl_->database,root,attempt,call);}
AgentWorkerLeaseRecord Repository::Impl::retire_worker(const std::string& generation,const std::string& reason){
    const std::set<std::string> reasons{"completed","cancelled","worker_exit","backend_recovery","dispatch_error"};if(!reasons.contains(reason))throw std::invalid_argument("Invalid worker retirement reason");auto& db=database;auto lease=read_worker_lease(db,generation);
    if(lease.state=="retired"){if(lease.retirement_reason!=reason)throw Conflict("Worker retirement observation changed");return lease;}
    if(lease.revision>=9007199254740991)throw std::overflow_error("Worker lease revision exhausted");std::int64_t uncertain=0;
    const auto planning=db.execute("SELECT payload FROM information WHERE category=? AND json_extract(payload,'$.model.generation')=?",{std::string(worker_planning_category),generation}).rows;
    for(const auto& row:planning){const auto saved=Json::parse(text(row[0]));const auto attempt=saved.at("model").at("attempt_id").get<std::string>();auto record=read_worker_planning(db,lease.spec.root_run_id,attempt);if(record.state!="entered")continue;record.state="uncertain";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_planning_json(record).dump(),std::string(worker_planning_category),worker_claim_key(lease.spec.root_run_id,attempt)}));event(lease.spec.owner_run_id,"worker.planning.uncertain",Json{{"generation",generation},{"attempt_id",attempt},{"call_id",record.call_id}}.dump());}
    const auto files=db.execute("SELECT payload FROM information WHERE category=? AND json_extract(payload,'$.model.generation')=?",{std::string(worker_file_category),generation}).rows;
    for(const auto& row:files){const auto saved=Json::parse(text(row[0]));const auto call=AgentFileToolInvocation::parse(saved.at("invocation").get<std::string>());auto record=read_worker_file(db,lease.spec.root_run_id,call.attempt_id,call.call_id);if(record.state=="returned"||record.state=="uncertain")continue;record.state="uncertain";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_file_json(record).dump(),std::string(worker_file_category),worker_tool_call_key(lease.spec.root_run_id,call.attempt_id,call.call_id)}));event(lease.spec.owner_run_id,"worker.file.uncertain",Json{{"generation",generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"operation_ids",record.operation_ids}}.dump());}
    const auto processes=db.execute("SELECT payload FROM information WHERE category=? AND json_extract(payload,'$.model.generation')=?",{std::string(worker_process_category),generation}).rows;
    for(const auto& row:processes){const auto saved=Json::parse(text(row[0]));const auto call=AgentProcessToolInvocation::parse(saved.at("invocation").get<std::string>());auto record=read_worker_process(db,lease.spec.root_run_id,call.attempt_id,call.call_id);if(record.state=="returned"||record.state=="uncertain")continue;record.state="uncertain";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_process_json(record).dump(),std::string(worker_process_category),worker_tool_call_key(lease.spec.root_run_id,call.attempt_id,call.call_id)}));event(lease.spec.owner_run_id,"worker.process.uncertain",Json{{"generation",generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"operation_ids",record.operation_ids}}.dump());}
    const auto remotes=db.execute("SELECT payload FROM information WHERE category=? AND json_extract(payload,'$.model.generation')=?",{std::string(worker_remote_category),generation}).rows;
    const auto callables=db.execute("SELECT payload FROM information WHERE category=? AND json_extract(payload,'$.model.generation')=?",{std::string(worker_callable_category),generation}).rows;
    for(const auto& row:remotes){const auto saved=Json::parse(text(row[0]));const auto call=AgentRemoteToolInvocation::parse(saved.at("invocation").get<std::string>());auto record=read_worker_remote(db,lease.spec.root_run_id,call.attempt_id,call.call_id);if(record.state=="returned"||record.state=="uncertain")continue;record.state="uncertain";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_remote_json(record).dump(),std::string(worker_remote_category),worker_tool_call_key(lease.spec.root_run_id,call.attempt_id,call.call_id)}));event(lease.spec.owner_run_id,"worker.remote.uncertain",Json{{"generation",generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"operation_ids",record.operation_ids}}.dump());}
    for(const auto& row:callables){const auto saved=Json::parse(text(row[0]));const auto call=AgentCallableInvocation::parse(saved.at("invocation").get<std::string>());auto record=read_worker_callable(db,lease.spec.root_run_id,call.attempt_id,call.call_id);if(record.state=="returned"||record.state=="uncertain")continue;record.state="uncertain";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_callable_json(record).dump(),std::string(worker_callable_category),worker_tool_call_key(lease.spec.root_run_id,call.attempt_id,call.call_id)}));event(lease.spec.owner_run_id,"worker.callable.uncertain",Json{{"generation",generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"operation_ids",record.operation_ids}}.dump());}
    const auto reads=db.execute("SELECT payload FROM information WHERE category=? AND json_extract(payload,'$.model.generation')=?",{std::string(worker_read_category),generation}).rows;
    for(const auto& row:reads){const auto saved=Json::parse(text(row[0]));const auto& invocation=saved.at("invocation").get<std::string>();const auto call=AgentWorkspaceInvocation::parse(invocation);auto record=read_worker_read(db,lease.spec.root_run_id,call.attempt_id,call.call_id);if(record.state=="returned"||record.state=="uncertain")continue;record.state="uncertain";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_read_json(record).dump(),std::string(worker_read_category),worker_tool_call_key(lease.spec.root_run_id,call.attempt_id,call.call_id)}));event(lease.spec.owner_run_id,"worker.read.uncertain",Json{{"generation",generation},{"attempt_id",call.attempt_id},{"call_id",call.call_id},{"dispatched",record.dispatched}}.dump());}
    const auto rows=db.execute("SELECT payload FROM information WHERE category=? AND json_extract(payload,'$.spec.generation')=?",{std::string(worker_claim_category),generation}).rows;
    for(const auto& row:rows){const auto saved=Json::parse(text(row[0]));auto record=read_worker_claim(db,saved.at("spec").at("root_run_id").get<std::string>(),saved.at("spec").at("attempt_id").get<std::string>());if(record.state!="dispatching")continue;record.state="uncertain";changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_claim_json(record).dump(),std::string(worker_claim_category),worker_claim_key(record.spec.root_run_id,record.spec.attempt_id)}));++uncertain;}
    lease.state="retired";lease.retirement_reason=reason;++lease.revision;changed_one(db.execute("UPDATE information SET payload=? WHERE category=? AND id=?",{worker_lease_json(lease).dump(),std::string(worker_lease_category),generation}));event(lease.spec.owner_run_id,"worker.lease.retired",Json{{"generation",generation},{"reason",reason},{"uncertain_model_dispatches",uncertain}}.dump());return lease;
}

AgentWorkerLeaseRecord Repository::retire_agent_worker(const std::string& generation,const std::string& reason){Transaction tx(impl_->database);auto record=impl_->retire_worker(generation,reason);tx.commit();return record;}

Run Repository::terminate_agent_worker(const AgentWorkerLeaseSpec& lease,RunState outcome,const std::string& reason){
    const std::set<std::string> failures{"deadline","provider_failure","worker_exit","dispatch_error"};
    if(!((outcome==RunState::cancelled&&reason=="cancelled")||(outcome==RunState::failed&&failures.contains(reason))))
        throw std::invalid_argument("Invalid worker terminal observation");
    auto& db=impl_->database;Transaction tx(db);worker_current_scope(*this,db,lease,true);
    auto current=run(lease.owner_run_id);
    if(!worker_agent_owner(*this,lease)||current.graph_root||
       !db.execute("SELECT root_run_id FROM dynamic_capabilities WHERE root_run_id=?",{current.id}).rows.empty())
        throw Conflict("Worker termination requires an ordinary direct root");
    impl_->root_boundary(current,outcome);
    // This initial read-worker boundary cannot release executing effects, even
    // background processes. Uncertain effects remain facts under their owners.
    if(impl_->has_operations(current.id,"'executing'"))throw Conflict("Worker termination still owns executing effects");
    changed_one(db.execute("UPDATE runs SET state=? WHERE id=? AND state=?",{state_name(outcome),current.id,state_name(current.state)}));
    impl_->cancel_waiting(current.id);
    const auto attempts=db.execute("SELECT attempt_id,role,state FROM agent_model_call_reservations WHERE root_run_id=? AND owner_run_id=? AND state IN ('reserved','started') ORDER BY attempt_id",{lease.root_run_id,lease.owner_run_id}).rows;
    for(const auto& row:attempts){
        changed_one(db.execute("UPDATE agent_model_call_reservations SET state='interrupted' WHERE root_run_id=? AND attempt_id=? AND owner_run_id=? AND state=?",{lease.root_run_id,text(row[0]),lease.owner_run_id,text(row[2])}));
        impl_->event(current.id,"budget.model_call.interrupted",Json{{"root_run_id",lease.root_run_id},{"attempt_id",text(row[0])},{"role",text(row[1])},{"was_started",text(row[2])=="started"},{"generation",lease.generation},{"reason",reason}}.dump());
    }
    // The caller has physically drained this exact child and joined its
    // transports. Preserve completed facts; close only unfinished typed members
    // owned by this ordinary root. Charges and captured model receipts remain.
    const auto context_members=db.execute("UPDATE inference_attempt_members SET outcome='interrupted',failure_code='interrupted' WHERE outcome IS NULL AND step_id IN (SELECT id FROM inference_steps WHERE root_run_id=? AND owner_run_id=? AND state IN ('reserved','started','context_overflow') AND successful_attempt_id IS NULL)",{lease.root_run_id,lease.owner_run_id}).affected_rows;
    const auto context_steps=db.execute("UPDATE inference_steps SET state='interrupted' WHERE root_run_id=? AND owner_run_id=? AND state IN ('reserved','started','context_overflow') AND successful_attempt_id IS NULL",{lease.root_run_id,lease.owner_run_id}).affected_rows;
    if(context_members||context_steps)impl_->event(current.id,"worker.context.interrupted",Json{{"generation",lease.generation},{"members",context_members},{"steps",context_steps},{"reason",reason},{"replayed",false}}.dump());
    impl_->retire_worker(lease.generation,outcome==RunState::cancelled?"cancelled":reason=="worker_exit"?"worker_exit":"dispatch_error");
    impl_->event(current.id,"run."+state_name(outcome),Json{{"reason",reason},{"generation",lease.generation}}.dump());
    tx.commit();current.state=outcome;return current;
}

void Repository::put_information(const std::string& category,const std::string& id,const std::string& json) {
    identifier(category); identifier(id);
    if(category==backend_owner_category||category==legacy_owner_category||category==refresh_category)throw std::invalid_argument("Use typed native owner control");
    if(worker_private_category(category))throw std::invalid_argument("Use typed native worker control");
    if(category=="secrets" || category=="credentials") throw std::invalid_argument("Use the encrypted credential repository");
    Transaction transaction(impl_->database);
    changed_one(impl_->database.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO UPDATE SET payload=excluded.payload",{category,id,json}));
    transaction.commit();
}
void Repository::compare_information(const std::string& category,const std::string& id,const std::string& json,const std::optional<std::string>& expected) {
    identifier(category);identifier(id);
    if(category==legacy_owner_category||category==refresh_category)throw std::invalid_argument("Use typed native owner control");
    if(category==backend_owner_category)throw std::invalid_argument("Use typed native owner control");
    if(worker_private_category(category))throw std::invalid_argument("Use typed native worker control");
    if(category=="secrets"||category=="credentials")throw std::invalid_argument("Use the encrypted credential repository");
    Transaction transaction(impl_->database);
    const auto result=expected
        ?impl_->database.execute("UPDATE information SET payload=? WHERE category=? AND id=? AND payload=?",{json,category,id,*expected})
        :impl_->database.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?) ON CONFLICT(category,id) DO NOTHING",{category,id,json});
    if(result.affected_rows!=1)throw Conflict("Information changed before publication");
    transaction.commit();
}
std::string Repository::information(const std::string& category,const std::string& id) {
    if(worker_private_category(category))throw std::invalid_argument("Use typed native worker control");
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
    auto& db=impl_->database; Transaction transaction(db);refresh_fence(db,scope,id);
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
    return std::move(resolve_credential_snapshot(scope,id,purpose).secret);
}
ResolvedCredential Repository::resolve_credential_snapshot(const std::string& scope,const std::string& id,const std::string& purpose) {
    // Purpose is supplied by the calling connector, not accepted from public views.
    credential_context(scope,id,purpose,1);
    const auto rows=impl_->database.execute("SELECT purpose,revision,protection,ciphertext,label FROM credentials WHERE scope=? AND id=?",{scope,id}).rows;
    if(rows.empty()) throw NotFound("Credential not found");
    const auto& row=rows[0];
    if(text(row[0])!=purpose) throw Conflict("Credential purpose differs");
    return {{scope,id,purpose,text(row[4]),integer(row[1])},reveal_secret({text(row[2]),std::get<SqlBytes>(row[3])},credential_context(scope,id,purpose,integer(row[1])))};
}
void Repository::delete_credential(const std::string& scope,const std::string& id,std::int64_t expected_revision) {
    identifier(scope);identifier(id);
    if(expected_revision<=0) throw std::invalid_argument("Invalid credential revision");
    auto& db=impl_->database;Transaction transaction(db);refresh_fence(db,scope,id);
    const auto rows=db.execute("SELECT revision FROM credentials WHERE scope=? AND id=?",{scope,id}).rows;
    if(rows.empty()) throw NotFound("Credential not found");
    if(integer(rows[0][0])!=expected_revision) throw Conflict("Credential revision changed");
    changed_one(db.execute("INSERT INTO retired_credentials(scope,id) VALUES(?,?)",{scope,id}));
    changed_one(db.execute("DELETE FROM credentials WHERE scope=? AND id=? AND revision=?",{scope,id,expected_revision}));
    transaction.commit();
}
McpOAuthRefreshClaim Repository::claim_mcp_oauth_refresh(const McpOAuthRefreshSpec& spec){
    refresh_spec(spec);auto& db=impl_->database;Transaction tx(db);const auto owner=backend_owner();if(owner.quiesced)throw BackendQuiesced("Native backend is quiesced");
    const auto existing=db.execute("SELECT id FROM information WHERE category=? AND id=?",{std::string(refresh_category),spec.request_id}).rows;
    if(!existing.empty()){auto record=refresh_read(db,spec.request_id);if(record.binding!=spec)throw Conflict("MCP refresh request identity changed");tx.commit();return {std::move(record),{}};}
    const auto records=refresh_records(db);if(records.size()>=4096)throw Conflict("MCP refresh receipt capacity exceeded");refresh_fence(db,spec.scope,spec.credential_id);require_database_idle(db,true);refresh_configuration(db,spec);
    auto grant=resolve_credential_snapshot(spec.scope,spec.credential_id,spec.purpose);if(grant.metadata.revision!=spec.credential_revision)throw Conflict("MCP refresh credential revision changed");
    McpOAuthRefreshRecord record{spec,owner.generation,"prepared",now_ms()};
    changed_one(db.execute("INSERT INTO information(category,id,payload) VALUES(?,?,?)",{std::string(refresh_category),spec.request_id,refresh_json(record).dump()}));tx.commit();return {std::move(record),std::move(grant)};
}
McpOAuthRefreshRecord Repository::mcp_oauth_refresh(const std::string& id){return refresh_read(impl_->database,id);}
McpOAuthRefreshRecord Repository::dispatch_mcp_oauth_refresh(const std::string& id,const std::string& generation){
    auto& db=impl_->database;Transaction tx(db);const auto before=refresh_read(db,id);const auto owner=backend_owner();
    if(owner.quiesced)throw BackendQuiesced("Native backend is quiesced");if(owner.generation!=generation||before.generation!=generation||before.state!="prepared")throw Conflict("MCP refresh dispatch owner changed");refresh_configuration(db,before.binding);
    auto grant=resolve_credential_snapshot(before.binding.scope,before.binding.credential_id,before.binding.purpose);if(grant.metadata.revision!=before.binding.credential_revision)throw Conflict("MCP refresh credential revision changed");
    auto after=before;after.state="dispatched";after.dispatched_unix_ms=now_ms();refresh_update(db,before,after);tx.commit();return after;
}
McpOAuthRefreshRecord Repository::abandon_mcp_oauth_refresh(const std::string& id,const std::string& generation){
    auto& db=impl_->database;Transaction tx(db);const auto before=refresh_read(db,id);const auto owner=backend_owner();
    if(owner.quiesced)throw BackendQuiesced("Native backend is quiesced");if(owner.generation!=generation||before.generation!=generation||(before.state!="prepared"&&before.state!="dispatched"))throw Conflict("MCP refresh retirement owner changed");
    auto after=before;after.state=before.state=="prepared"?"cancelled":"uncertain";after.settled_unix_ms=now_ms();refresh_update(db,before,after);tx.commit();return after;
}
McpOAuthRefreshRecord Repository::publish_mcp_oauth_refresh(const std::string& id,const std::string& generation,const SecretBytes& complete_grant){
    if(complete_grant.view().empty()||complete_grant.view().size()>128*1024)throw std::invalid_argument("Invalid complete MCP refresh grant");
    auto& db=impl_->database;Transaction tx(db);const auto before=refresh_read(db,id);const auto owner=backend_owner();
    if(owner.quiesced)throw BackendQuiesced("Native backend is quiesced");if(owner.generation!=generation||before.generation!=generation||before.state!="dispatched")throw Conflict("MCP refresh publication owner changed");refresh_configuration(db,before.binding);
    const auto& binding=before.binding;const auto snapshot=resolve_credential_snapshot(binding.scope,binding.credential_id,binding.purpose);if(snapshot.metadata.revision!=binding.credential_revision)throw Conflict("MCP refresh credential revision changed");
    const auto revision=binding.credential_revision+1;const auto encrypted=protect_secret(complete_grant,credential_context(binding.scope,binding.credential_id,binding.purpose,revision));
    changed_one(db.execute("UPDATE credentials SET revision=?,protection=?,ciphertext=? WHERE scope=? AND id=? AND purpose=? AND revision=?",{revision,encrypted.protection,encrypted.ciphertext,binding.scope,binding.credential_id,binding.purpose,binding.credential_revision}));
    auto after=before;after.state="committed";after.published_revision=revision;after.settled_unix_ms=now_ms();refresh_update(db,before,after);tx.commit();return after;
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
    const auto resource_offset=resources.size()&&!resources.front().starts_with("workspace:")?0:1;
    for(std::size_t index=0;index<resources.size();++index)changed_one(db.execute("INSERT INTO operation_resources(operation_id,resource,position,state) VALUES(?,?,?,'awaiting_approval')",{id,resources[index],static_cast<std::int64_t>(index)-resource_offset}));
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
std::vector<Operation> Repository::pending_operations_for_session(const std::string& id) {
    session(id);std::vector<Operation> result;
    const auto rows=impl_->database.execute("SELECT o.id,o.run_id,o.workspace,o.tool,o.arguments,o.state,o.expires_ms,o.result,o.decision_actor FROM operations o JOIN runs r ON r.id=o.run_id WHERE r.session_id=? AND o.state='awaiting_approval' ORDER BY o.rowid LIMIT 257",{id}).rows;
    if(rows.size()>256)throw Conflict("Session has too many pending permission requests; resolve requests before listing them");
    for(const auto& row:rows)result.push_back(decode_operation(row,impl_->database));
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
            const auto blocked=db.execute("SELECT o.state,o.id FROM operation_resources r JOIN operations o ON o.id=r.operation_id WHERE r.resource=? AND o.state IN ('executing','uncertain') AND o.id<>? ORDER BY CASE o.state WHEN 'uncertain' THEN 0 ELSE 1 END LIMIT 1",{resource,id}).rows;
            if(!blocked.empty()) {
                if(text(blocked[0][0])=="uncertain")throw WorkspaceEffectUncertain("Effect resource is quarantined by uncertain operation "+text(blocked[0][1]));
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
McpInputRequest Repository::publish_mcp_input_request(const McpInputRequestSpec& input){
    auto spec=input;identifier(spec.id);identifier(spec.run_id);identifier(spec.operation_id);identifier(spec.server_config_id);identifier(spec.peer_tool);identifier(spec.request_key);
    if(spec.id.size()>128||spec.server_config_id.size()>128||spec.peer_tool.size()>128||spec.request_key.size()>128||spec.config_revision<=0)
        throw std::invalid_argument("MCP input identity exceeds its limit");
    spec.request_json=object_json(spec.request_json,65536);const auto now=now_ms();
    if(spec.expires_unix_ms<=now||spec.expires_unix_ms>now+3600000)throw std::invalid_argument("MCP input expiry must be within one hour");
    auto& db=impl_->database;Transaction transaction(db);const auto operation_record=operation(spec.operation_id);
    if(operation_record.spec.run_id!=spec.run_id||operation_record.spec.tool!="mcp_tool"||operation_record.state!=OperationState::executing||run(spec.run_id).state!=RunState::running)
        throw Conflict("MCP input requires its executing operation and active run");
    const auto pending=integer(db.execute("SELECT count(*) FROM mcp_input_requests WHERE run_id=? AND state='waiting'",{spec.run_id}).rows.at(0).at(0));
    if(pending>=32)throw Conflict("Run has too many pending MCP input requests");
    const auto payload=Json{{"request_id",spec.id},{"operation_id",spec.operation_id},{"server_config_id",spec.server_config_id},
        {"config_revision",spec.config_revision},{"peer_tool",spec.peer_tool},{"request_key",spec.request_key},
        {"request_json",spec.request_json},{"expires_unix_ms",spec.expires_unix_ms}}.dump();
    const auto published=impl_->event(spec.run_id,"mcp.input.requested",payload);
    changed_one(db.execute("INSERT INTO mcp_input_requests(id,run_id,operation_id,server_config_id,config_revision,peer_tool,request_key,request_json,state,expires_ms,published_event_seq) VALUES(?,?,?,?,?,?,?,?,'waiting',?,?)",
        {spec.id,spec.run_id,spec.operation_id,spec.server_config_id,spec.config_revision,spec.peer_tool,spec.request_key,spec.request_json,spec.expires_unix_ms,published.sequence}));
    const auto rows=db.execute("SELECT id,run_id,operation_id,server_config_id,config_revision,peer_tool,request_key,request_json,state,response_json,actor,expires_ms,published_event_seq,settled_event_seq FROM mcp_input_requests WHERE id=?",{spec.id}).rows;
    transaction.commit();return decode_mcp_input_request(rows.at(0));
}
McpInputRequest Repository::mcp_input_request(const std::string& id){
    identifier(id);const auto rows=impl_->database.execute("SELECT id,run_id,operation_id,server_config_id,config_revision,peer_tool,request_key,request_json,state,response_json,actor,expires_ms,published_event_seq,settled_event_seq FROM mcp_input_requests WHERE id=?",{id}).rows;
    if(rows.empty())throw NotFound("MCP input request not found");return decode_mcp_input_request(rows[0]);
}
std::vector<McpInputRequest> Repository::mcp_input_requests(const std::string& run_id){
    run(run_id);std::vector<McpInputRequest> result;for(const auto& row:impl_->database.execute("SELECT id,run_id,operation_id,server_config_id,config_revision,peer_tool,request_key,request_json,state,response_json,actor,expires_ms,published_event_seq,settled_event_seq FROM mcp_input_requests WHERE run_id=? AND state='waiting' ORDER BY rowid LIMIT 33",{run_id}).rows){if(result.size()>=32)throw Conflict("Run has too many pending MCP input requests");result.push_back(decode_mcp_input_request(row));}return result;
}
McpInputRequest Repository::answer_mcp_input_request(const std::string& run_id,const std::string& id,const std::string& source,const std::string& actor){
    identifier(run_id);identifier(id);identifier(actor);if(actor.size()>256)throw std::invalid_argument("MCP input actor exceeds its limit");const auto response=object_json(source,16384);
    auto& db=impl_->database;Transaction transaction(db);auto request=mcp_input_request(id);
    if(request.run_id!=run_id)throw NotFound("MCP input request does not belong to this run");if(request.state!="waiting")throw Conflict("MCP input request is no longer waiting");
    if(request.expires_unix_ms<=now_ms())throw Conflict("MCP input request expired");
    if(run(run_id).state!=RunState::running||operation(request.operation_id).state!=OperationState::executing)throw Conflict("MCP input owner is no longer active");
    const auto event=impl_->event(run_id,"mcp.input.answered",Json{{"request_id",id},{"operation_id",request.operation_id},{"actor",actor},{"response_json",response}}.dump());
    changed_one(db.execute("UPDATE mcp_input_requests SET state='answered',response_json=?,actor=?,settled_event_seq=? WHERE id=? AND run_id=? AND state='waiting'",{response,actor,event.sequence,id,run_id}));
    request.state="answered";request.response_json=response;request.actor=actor;request.answered_event_seq=event.sequence;transaction.commit();return request;
}
McpInputRequest Repository::retire_mcp_input_request(const std::string& id,const std::string& reason){
    identifier(id);if(reason!="cancelled"&&reason!="expired"&&reason!="server_restart"&&reason!="owner_ended")throw std::invalid_argument("Invalid MCP input retirement reason");
    auto& db=impl_->database;Transaction transaction(db);auto request=mcp_input_request(id);if(request.state!="waiting"){transaction.commit();return request;}
    const std::string next=reason=="expired"?"expired":"cancelled";const auto event=impl_->event(request.run_id,"mcp.input."+next,Json{{"request_id",id},{"operation_id",request.operation_id},{"reason",reason}}.dump());
    changed_one(db.execute("UPDATE mcp_input_requests SET state=?,settled_event_seq=? WHERE id=? AND state='waiting'",{next,event.sequence,id}));request.state=next;request.answered_event_seq=event.sequence;transaction.commit();return request;
}
Operation Repository::reconcile_interrupted_process(const std::string& id,const std::string& actor,const std::string& note) {
    identifier(actor);if(actor.size()>4096)throw std::invalid_argument("Controller identity exceeds its limit");
    if(note.size()<16||note.size()>1024||note.find_first_of("\r\n")!=std::string::npos||note.find('\0')!=std::string::npos)throw std::invalid_argument("Reconciliation note must be 16 to 1,024 characters on one line");
    try{(void)Json(actor).dump();(void)Json(note).dump();}catch(const Json::exception&){throw std::invalid_argument("Invalid reconciliation audit text");}
    auto& db=impl_->database;Transaction transaction(db);auto result=operation(id);
    if(result.state!=OperationState::uncertain||result.spec.tool!="run_process")throw Conflict("Only an uncertain process operation can be reconciled here");
    const auto owner=run(result.spec.run_id);if(owner.state!=RunState::failed&&owner.state!=RunState::cancelled)throw Conflict("An active process owner cannot be reconciled");
    Json receipt;try{receipt=Json::parse(result.result_json);}catch(const Json::exception&){throw DatabaseError("Saved process receipt is invalid");}
    if(!receipt.is_object()||receipt.value("process_tree_retired",false)!=true||!receipt.contains("output_artifact")||!receipt["output_artifact"].is_object()||
       receipt["output_artifact"].value("operation_id",std::string{})!=id||receipt["output_artifact"].value("state",std::string{})!="complete")
        throw Conflict("Process receipt does not prove a retired process tree and complete output capture");
    const auto artifact=db.execute("SELECT run_id,state FROM process_output_artifacts WHERE operation_id=?",{id}).rows;
    if(artifact.size()!=1||text(artifact[0][0])!=result.spec.run_id||text(artifact[0][1])!="complete")throw Conflict("Complete process output artifact is unavailable");
    const auto timestamp=now_ms();receipt["reconciliation"]={{"actor",actor},{"note",note},{"reconciled_unix_ms",timestamp},{"acknowledges_possible_side_effects",true}};
    const auto encoded=receipt.dump();if(encoded.size()>1024*1024)throw DatabaseError("Reconciled process receipt exceeds its limit");
    changed_one(db.execute("UPDATE operations SET state='failed',result=? WHERE id=? AND state='uncertain'",{encoded,id}));
    result.state=OperationState::failed;result.result_json=encoded;impl_->operation_state_event(result);
    impl_->event(result.spec.run_id,"operation.reconciled",Json{{"operation_id",id},{"tool",result.spec.tool},{"actor",actor},{"reconciled_unix_ms",timestamp},{"acknowledges_possible_side_effects",true}}.dump());
    transaction.commit();return result;
}
std::size_t Repository::recover_interrupted(const BackendLease& owner) {
    if(!owner.covers(impl_->path)) throw Conflict("Recovery requires this database's owner lease");
    auto& db=impl_->database; Transaction transaction(db);
    // Prove clean pauses before recovery changes any owner, effect or transport state.
    std::set<std::string> preserved_pauses;
    for(const auto& row:db.execute("SELECT r.id FROM runs r WHERE r.parent_run_id IS NULL AND r.state='paused' AND EXISTS(SELECT 1 FROM graph_invocations gi WHERE gi.root_run_id=r.id)").rows){const auto closed=impl_->closed_paused_graph_tree(text(row[0]));preserved_pauses.insert(closed.begin(),closed.end());}
    auto roots=db.execute("SELECT id FROM runs WHERE state IN ('queued','running')").rows;
    // Only a fully closed, typed human pause survives. A paused run without
    // this complete proof is interrupted rather than silently adopted.
    for(const auto& row:db.execute("SELECT r.id FROM runs r JOIN dynamic_capabilities a ON a.root_run_id=r.id WHERE r.state='paused'").rows){
        const auto& id=text(row[0]);const auto clean=db.execute("SELECT p.id FROM dynamic_owner_pauses h JOIN dynamic_plans p ON p.id=h.plan_id JOIN dynamic_plan_calls c ON c.id=h.plan_call_id JOIN agent_budget_segments s ON s.id=h.segment_id JOIN dynamic_capabilities a ON a.root_run_id=h.root_run_id WHERE h.root_run_id=? AND h.state='paused' AND p.root_run_id=h.root_run_id AND p.state='waiting_human' AND c.root_run_id=h.root_run_id AND c.plan_id=p.id AND c.state='accepted' AND c.result_json IS NULL AND c.conversation_commit_seq IS NULL AND c.continuation_attempt_id IS NULL AND s.root_run_id=h.root_run_id AND s.state='closed' AND s.remaining_active_ms>0 AND a.catalogue_finalized=1 AND NOT EXISTS(SELECT 1 FROM dynamic_plan_nodes n WHERE n.plan_id=p.id AND n.state IN ('claimed','uncertain')) AND NOT EXISTS(SELECT 1 FROM agent_budget_segments b WHERE b.root_run_id=h.root_run_id AND b.ordinal>s.ordinal) AND NOT EXISTS(SELECT 1 FROM agent_model_call_reservations m WHERE m.root_run_id=h.root_run_id AND m.state IN ('reserved','started')) AND NOT EXISTS(SELECT 1 FROM operations o JOIN runs j ON j.id=o.run_id WHERE (j.id=h.root_run_id OR j.parent_run_id=h.root_run_id) AND o.state IN ('awaiting_approval','ready','executing','uncertain')) AND NOT EXISTS(SELECT 1 FROM delegation_batches b WHERE b.parent_run_id=h.root_run_id AND b.state IN ('accepted','working'))",{id}).rows;
        bool preserve=!clean.empty();if(preserve)try{(void)dynamic_owner_pause(id);}catch(...){preserve=false;}
        if(preserve&&(!db.execute("SELECT id FROM context_compactions WHERE root_run_id=? AND state IN ('reserved','started','received')",{id}).rows.empty()||!db.execute("SELECT id FROM context_measures WHERE root_run_id=? AND state IN ('reserved','started')",{id}).rows.empty()))preserve=false;
        if(preserve){const auto pin_rows=db.execute("SELECT context_pin FROM dynamic_owner_pauses WHERE root_run_id=? AND state='paused'",{id}).rows;if(pin_rows.empty())preserve=false;else if(std::holds_alternative<std::nullptr_t>(pin_rows[0][0])){if(!db.execute("SELECT id FROM inference_steps WHERE root_run_id=? AND owner_run_id=?",{id,id}).rows.empty())preserve=false;}else {try{validate_context_pause(db,run(id),dynamic_capabilities(id),decode_context_pause(text(pin_rows[0][0])));}catch(const ContextUnavailable&){preserve=false;}catch(const ContextCapacityExceeded&){preserve=false;}}}
        if(!preserve)roots.push_back(row);
    }
    for(const auto& row:db.execute("SELECT r.id FROM runs r JOIN graph_context_owners g ON g.root_run_id=r.id WHERE r.state='paused'").rows){bool preserve=true;try{const auto graph_scope=graph_context_owner(text(row[0]));if(!graph_scope.segment||graph_scope.segment->state!="closed"||graph_scope.segment->remaining_active_ms<1)preserve=false;else if(!db.execute("SELECT run_id FROM graph_invocations WHERE root_run_id=? LIMIT 1",{text(row[0])}).rows.empty()){std::set<std::string> runnable;const auto closed=impl_->closed_paused_graph_tree(text(row[0]),true,&runnable);if(closed.empty())preserve=false;else preserved_pauses.insert(closed.begin(),closed.end());}}catch(const ContextUnavailable&){preserve=false;}catch(const ContextCapacityExceeded&){preserve=false;}catch(const Conflict&){preserve=false;}if(!preserve)roots.push_back(row);}
    for(const auto& row:roots) {
        const auto& id=text(row[0]);
        const auto executing=db.execute("SELECT id,run_id,workspace,tool,arguments,state,expires_ms,result,decision_actor FROM operations WHERE run_id=? AND state='executing'",{id}).rows;
        for(const auto& value:executing) {
            auto operation=decode_operation(value,db);
            changed_one(db.execute("UPDATE operations SET state='uncertain',result=? WHERE id=? AND state='executing'",{std::string(R"({"reason":"server_restart"})"),operation.id}));
            db.execute("UPDATE process_output_artifacts SET state='incomplete' WHERE operation_id=? AND state='capturing'",{operation.id});
            operation.state=OperationState::uncertain;impl_->operation_state_event(operation);
        }
        for(const auto& request:db.execute("SELECT id,operation_id FROM mcp_input_requests WHERE run_id=? AND state='waiting'",{id}).rows){
            const auto request_id=text(request[0]),operation_id=text(request[1]);const auto event=impl_->event(id,"mcp.input.cancelled",Json{{"request_id",request_id},{"operation_id",operation_id},{"reason","server_restart"}}.dump());
            changed_one(db.execute("UPDATE mcp_input_requests SET state='cancelled',settled_event_seq=? WHERE id=? AND state='waiting'",{event.sequence,request_id}));
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
    // A provider acknowledgement remains in its immutable dedicated receipt.
    // Restart never dispatches maintenance, count or funded inference again.
    for(const auto& row:db.execute("SELECT id FROM context_compactions WHERE state IN ('reserved','started','received')").rows){const auto c=read_compaction(db,text(row[0]));changed_one(db.execute("UPDATE context_compactions SET state='interrupted',failure_code='interrupted' WHERE id=? AND state=?",{c.id,c.state}));context_journal(db,c,"context.compaction.interrupted",Json{{"compaction_id",c.id},{"reason","server_restart"},{"receipt_retained",c.state=="received"},{"replayed",false}});if(c.manual)changed_one(db.execute("UPDATE context_manual_requests SET state='interrupted',failure_code='interrupted' WHERE id=? AND state='claimed'",{*c.manual}));}
    db.execute("UPDATE context_measures SET state='interrupted',failure_code='interrupted' WHERE state IN ('reserved','started')");
    db.execute("UPDATE context_manual_requests SET state='interrupted',failure_code='interrupted' WHERE state='pending' AND owner_id IN (SELECT id FROM context_idle_owners WHERE state='active')");
    db.execute("UPDATE context_idle_owners SET state='interrupted',failure_code='interrupted' WHERE state='active'");
    db.execute("UPDATE inference_attempt_members SET outcome='interrupted',failure_code='interrupted' WHERE outcome IS NULL AND step_id IN (SELECT id FROM inference_steps WHERE state IN ('reserved','started','context_overflow'))");
    db.execute("UPDATE inference_steps SET state='interrupted' WHERE state IN ('reserved','started','context_overflow')");
    for(const auto& row:db.execute("SELECT s.root_run_id,s.id FROM agent_budget_segments s JOIN runs r ON r.id=s.root_run_id WHERE s.state='open'").rows){const auto e=impl_->event(text(row[0]),"budget.segment.interrupted",Json{{"segment_id",text(row[1])},{"reason","server_restart"},{"elapsed_known",false}}.dump());changed_one(db.execute("UPDATE agent_budget_segments SET state='interrupted',closed_event_seq=? WHERE id=? AND state='open'",{e.sequence,text(row[1])}));}
    for(const auto& row:db.execute("SELECT p.id,p.root_run_id FROM dynamic_plans p JOIN runs r ON r.id=p.root_run_id WHERE r.state='failed' AND (p.state IN ('active','waiting_human') OR (p.state='uncertain' AND (p.planned_children_reserved>0 OR p.planned_humans_reserved>0 OR EXISTS(SELECT 1 FROM dynamic_plan_nodes n WHERE n.plan_id=p.id AND n.state IN ('pending','blocked','claimed','waiting_human')) OR EXISTS(SELECT 1 FROM dynamic_plan_calls c WHERE c.plan_id=p.id AND c.state IN ('accepted','report_ready','turn_committed') AND c.continuation_attempt_id IS NULL))))").rows){
        const auto plan=text(row[0]),root=text(row[1]);bool uncertain=!db.execute("SELECT label FROM dynamic_plan_nodes WHERE plan_id=? AND (state='uncertain' OR effect_state='uncertain')",{plan}).rows.empty();
         for(const auto& n:db.execute("SELECT label,child_run_id,definition_json,backend_node_id FROM dynamic_plan_nodes WHERE plan_id=? AND state='claimed'",{plan}).rows){
             const auto child=text(n[1]);const auto actual=run(child);if(!terminal(actual.state))throw DatabaseError("Recovered dynamic child remains active");const auto effect=dynamic_effect_state(operations(child));uncertain|=effect=="uncertain";const auto definition=Json::parse(text(n[2]));const bool deterministic=definition.value("type",std::string{})=="tool";
             Json observed={{"child_run_id",child},{"label",text(n[0])},{"child_state",state_name(actual.state)},{"effect_state",effect},{"error",{{"code","server_restart"}}}};
             if(actual.state==RunState::completed&&deterministic){
                 const auto started=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='tool.started' ORDER BY seq",{child}).rows;const auto completed=db.execute("SELECT seq,payload FROM events WHERE run_id=? AND kind='tool.completed' ORDER BY seq",{child}).rows;
                 if(started.size()!=1||completed.size()!=1||integer(completed[0][0])<=integer(started[0][0]))throw DatabaseError("Recovered dynamic tool lacks its ordered actual receipt");
                 const auto before=Json::parse(text(started[0][1])),after=Json::parse(text(completed[0][1]));const auto tool=definition.at("tool").get<std::string>();
                 if(before.value("name",std::string{})!=tool||before.value("source",std::string{})!="dynamic_plan"||before.value("plan_node_id",std::string{})!=text(n[3])||before.value("arguments_json",std::string{})!=definition.at("arguments_json").get<std::string>()||after.value("name",std::string{})!=tool||after.value("source",std::string{})!="dynamic_plan"||after.value("plan_node_id",std::string{})!=text(n[3])||!after.contains("data"))throw DatabaseError("Recovered dynamic tool receipt differs from its immutable definition");
                 observed.erase("error");observed["tool"]=tool;observed["data"]=after["data"];
             }else if(actual.state==RunState::completed){const auto history=run_history(child);if(history.empty()||history.back().role!="assistant")throw DatabaseError("Recovered completed dynamic agent lacks actual history");const auto answer=Json::parse(history.back().json);for(const auto* key:{"content","model","usage","elapsed_ms","first_token_ms"})if(answer.contains(key))observed[key]=answer[key];}
             if(observed.dump().size()>32768){observed={{"child_run_id",child},{"label",text(n[0])},{"child_state",state_name(actual.state)},{"effect_state",effect},{"error",{{"code","result_limit_exceeded"}}},{"history_ref","/v1/runs/"+root+"/children/"+child+"/history"}};if(deterministic)observed["tool"]=definition.value("tool",std::string{});}
            const auto outcome=observed.dump();const auto e=impl_->event(child,"plan.child.settled",outcome);changed_one(db.execute("UPDATE dynamic_plan_nodes SET state=?,child_state=?,effect_state=?,outcome_json=?,settled_event_seq=? WHERE plan_id=? AND label=? AND state='claimed'",{std::string(effect)=="uncertain"?"uncertain":"settled",state_name(actual.state),effect,outcome,e.sequence,plan,text(n[0])}));
        }
        for(const auto& h:db.execute("SELECT id,label FROM dynamic_human_requests WHERE plan_id=? AND state='waiting'",{plan}).rows){const auto event=impl_->event(root,"plan.human.cancelled",Json{{"request_id",text(h[0])},{"reason","server_restart"}}.dump());changed_one(db.execute("UPDATE dynamic_human_requests SET state='cancelled',input_event_seq=? WHERE id=? AND state='waiting'",{event.sequence,text(h[0])}));const auto outcome=Json{{"human_state","cancelled"},{"request_id",text(h[0])}}.dump();const auto settled=impl_->event(root,"plan.human.settled",outcome);changed_one(db.execute("UPDATE dynamic_plan_nodes SET state='settled',outcome_json=?,settled_event_seq=? WHERE plan_id=? AND label=? AND state='waiting_human'",{outcome,settled.sequence,plan,text(h[1])}));}
        db.execute("UPDATE dynamic_plan_nodes SET state='cancelled',protected_definition=1 WHERE plan_id=? AND state IN ('pending','blocked')",{plan});changed_one(db.execute("UPDATE dynamic_plans SET state=?,state_sequence=state_sequence+1,planned_children_reserved=0,planned_humans_reserved=0 WHERE id=?",{uncertain?"uncertain":"interrupted",plan}));
        const auto holds=integer(db.execute("SELECT count(*) FROM dynamic_plan_calls WHERE root_run_id=? AND continuation_attempt_id IS NULL AND state IN ('accepted','report_ready','turn_committed')",{root}).rows.at(0).at(0));db.execute("UPDATE dynamic_plan_calls SET state='interrupted' WHERE root_run_id=? AND state IN ('accepted','report_ready','turn_committed') AND continuation_attempt_id IS NULL",{root});changed_one(db.execute("UPDATE agent_execution_budgets SET planned_children_reserved=0,parent_calls_held=parent_calls_held-?,revision=revision+1 WHERE root_run_id=? AND parent_calls_held>=?",{holds,root,holds}));db.execute("UPDATE dynamic_owner_pauses SET state='retired' WHERE root_run_id=?",{root});db.execute("UPDATE dynamic_owner_pause_waits SET state='retired' WHERE root_run_id=?",{root});impl_->event(root,"plan.interrupted",Json{{"plan_id",plan},{"reason","server_restart"},{"replayed",false}}.dump());
    }
    for(const auto& row:db.execute("SELECT run_id FROM graph_roots").rows){if(preserved_pauses.contains(text(row[0])))continue;auto root=graph_run(text(row[0]));const GraphCoordinator recovered(GraphPlan(root.specification_json),root.checkpoint_json);if(recovered.checkpoint()!=root.checkpoint_json){impl_->graph_checkpoint(root,recovered);impl_->event(root.run.id,"graph.recovered",Json{{"checkpoint_revision",root.checkpoint_revision},{"reason","interrupted_nodes_not_replayed"}}.dump());}}
    // This database's exclusive BackendLease was proved above. Retire orphan
    // process generations in the same transaction as run/attempt recovery;
    // preserve acknowledged responses and never refund or reissue a claim.
    for(const auto& row:db.execute("SELECT id FROM information WHERE category=? AND json_extract(payload,'$.state')='active'",{std::string(worker_lease_category)}).rows)impl_->retire_worker(text(row[0]),"backend_recovery");
    transaction.commit(); return roots.size();
}
}
