#include "agentflow/agent_permission_grants.hpp"
#include "agentflow/agent_permissions.hpp"
#include "agentflow/context_records.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <chrono>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr auto catalog_category="native-agent-permission-grants";
constexpr auto request_category="native-agent-permission-requests";
constexpr std::int64_t max_revision=9007199254740991LL;

void text(const std::string& value,std::size_t maximum,const char* message,bool empty=false){
    if((!empty&&value.empty())||value.size()>maximum||value.find('\0')!=std::string::npos)throw std::invalid_argument(message);
}
bool askable_read_action(const std::string& action){return action=="read_file"||action=="list_files"||action=="search_files"||action=="find_files"||action=="glob_files"||action=="language_server";}
Json grant_json(const AgentPermissionGrant& grant){
    return {{"id",grant.id},{"workspace_id",grant.workspace_id},{"policy_binding",grant.policy_binding},{"action",grant.action},
        {"resource",grant.resource},{"created_unix_ms",grant.created_unix_ms}};
}
AgentPermissionGrant decode_grant(const Json& value){
    if(!value.is_object()||value.size()!=6||!value.contains("id")||!value["id"].is_string()||
       !value.contains("workspace_id")||!value["workspace_id"].is_string()||
       !value.contains("policy_binding")||!value["policy_binding"].is_string()||
       !value.contains("action")||!value["action"].is_string()||
       !value.contains("resource")||!value["resource"].is_string()||
       !value.contains("created_unix_ms")||!value["created_unix_ms"].is_number_integer())
        throw DatabaseError("Saved agent permission grant is invalid");
    AgentPermissionGrant result{value["id"].get<std::string>(),value["workspace_id"].get<std::string>(),value["policy_binding"].get<std::string>(),
        value["action"].get<std::string>(),value["resource"].get<std::string>(),value["created_unix_ms"].get<std::int64_t>()};
    try{text(result.id,128,"Invalid saved agent permission grant ID");text(result.workspace_id,32768,"Invalid saved grant workspace");text(result.policy_binding,64,"Invalid saved grant policy");text(result.action,128,"Invalid saved grant action");text(result.resource,4096,"Invalid saved grant resource");}
    catch(const std::invalid_argument&){throw DatabaseError("Saved agent permission grant is invalid");}
    if(result.created_unix_ms<=0)throw DatabaseError("Saved agent permission grant timestamp is invalid");
    return result;
}
AgentPermissionGrantSnapshot decode_catalog(const std::string& raw){
    Json value;try{value=Json::parse(raw);}catch(const Json::exception&){throw DatabaseError("Saved agent permission grants are corrupt");}
    if(!value.is_object()||value.size()!=3||!value.contains("version")||value["version"]!=1||
       !value.contains("revision")||!value["revision"].is_number_integer()||value["revision"]<0||value["revision"]>max_revision||
       !value.contains("grants")||!value["grants"].is_array()||value["grants"].size()>256)
        throw DatabaseError("Saved agent permission grants are invalid");
    AgentPermissionGrantSnapshot result;result.revision=value["revision"].get<std::int64_t>();
    for(const auto& item:value["grants"]){auto grant=decode_grant(item);if(std::any_of(result.grants.begin(),result.grants.end(),[&](const auto& old){return old.id==grant.id;}))throw DatabaseError("Saved agent permission grant IDs are duplicated");result.grants.push_back(std::move(grant));}
    return result;
}
std::string encode_catalog(const AgentPermissionGrantSnapshot& snapshot){
    Json grants=Json::array();for(const auto& grant:snapshot.grants)grants.push_back(grant_json(grant));
    return Json{{"version",1},{"revision",snapshot.revision},{"grants",std::move(grants)}}.dump();
}
std::pair<AgentPermissionGrantSnapshot,std::optional<std::string>> read_catalog(PersistenceService& persistence,const std::string& session){
    try{auto raw=persistence.information(catalog_category,session).get();return {decode_catalog(raw),std::move(raw)};}
    catch(const NotFound&){return {{},std::nullopt};}
}
}

std::string agent_permission_policy_binding(const std::vector<std::vector<AgentPermissionRule>>& inherited,
                                             const std::vector<AgentPermissionRule>& selected){
    const auto encode_rules=[](const std::vector<AgentPermissionRule>& rules){Json value=Json::array();for(const auto& rule:rules)value.push_back({{"action",rule.action},{"resource",rule.resource},{"effect",rule.effect}});return value;};
    Json layers=Json::array();for(const auto& layer:inherited)layers.push_back(encode_rules(layer));
    return context_digest(Json{{"version",1},{"inherited",std::move(layers)},{"selected",encode_rules(selected)}}.dump());
}
std::string agent_permission_grant_identifier(const std::string& workspace_id,const std::string& policy_binding,
    const std::string& action,const std::string& resource){
    text(workspace_id,32768,"Invalid permission grant workspace");text(policy_binding,64,"Invalid permission grant policy");
    text(action,128,"Invalid permission grant action");text(resource,4096,"Invalid permission grant resource");
    return "grant_"+context_digest(Json{{"version",1},{"workspace_id",workspace_id},{"policy_binding",policy_binding},{"action",action},{"resource",resource}}.dump());
}

void AgentPermissionGrantStore::record_request(const AgentPermissionAskContext& request){
    text(request.operation_id,128,"Invalid permission request operation ID");text(request.session_id,128,"Invalid permission request session");
    text(request.workspace_id,32768,"Invalid permission request workspace");text(request.policy_binding,64,"Invalid permission policy binding");
    text(request.action,128,"Invalid permission request action");text(request.resource,4096,"Invalid permission request resource");text(request.grant_id,128,"Invalid permission grant ID");
    if(!askable_read_action(request.action)||request.grant_id!=agent_permission_grant_identifier(request.workspace_id,request.policy_binding,request.action,request.resource))throw std::invalid_argument("Permission grant request is not an eligible read scope");
    const auto encoded=Json{{"version",1},{"operation_id",request.operation_id},{"session_id",request.session_id},
        {"workspace_id",request.workspace_id},{"policy_binding",request.policy_binding},{"action",request.action},
        {"resource",request.resource},{"grant_id",request.grant_id}}.dump();
    try{persistence_.compare_information(request_category,request.operation_id,encoded,std::nullopt).get();}
    catch(const Conflict&){const auto prior=persistence_.information(request_category,request.operation_id).get();if(prior!=encoded)throw;}
}
std::optional<AgentPermissionAskContext> AgentPermissionGrantStore::request(const std::string& operation_id){
    text(operation_id,128,"Invalid permission request operation ID");std::string raw;try{raw=persistence_.information(request_category,operation_id).get();}catch(const NotFound&){return std::nullopt;}
    Json value;try{value=Json::parse(raw);}catch(const Json::exception&){throw DatabaseError("Saved agent permission request is corrupt");}
    if(!value.is_object()||value.size()!=8||value.value("version",0)!=1||value.value("operation_id",std::string{})!=operation_id||
       !value.contains("session_id")||!value["session_id"].is_string()||!value.contains("workspace_id")||!value["workspace_id"].is_string()||
       !value.contains("policy_binding")||!value["policy_binding"].is_string()||!value.contains("action")||!value["action"].is_string()||
       !value.contains("resource")||!value["resource"].is_string()||!value.contains("grant_id")||!value["grant_id"].is_string())
        throw DatabaseError("Saved agent permission request is invalid");
    AgentPermissionAskContext result{operation_id,value["session_id"].get<std::string>(),value["workspace_id"].get<std::string>(),
        value["policy_binding"].get<std::string>(),value["action"].get<std::string>(),value["resource"].get<std::string>(),value["grant_id"].get<std::string>()};
    try{text(result.session_id,128,"Invalid permission request session");text(result.workspace_id,32768,"Invalid permission request workspace");text(result.policy_binding,64,"Invalid permission policy binding");text(result.action,128,"Invalid permission request action");text(result.resource,4096,"Invalid permission request resource");text(result.grant_id,128,"Invalid permission grant ID");}
    catch(const std::invalid_argument&){throw DatabaseError("Saved agent permission request is invalid");}
    if(!askable_read_action(result.action)||result.grant_id!=agent_permission_grant_identifier(result.workspace_id,result.policy_binding,result.action,result.resource))throw DatabaseError("Saved agent permission request is invalid");
    return result;
}
bool AgentPermissionGrantStore::allows(const std::string& session_id,const std::string& workspace_id,
    const std::string& policy_binding,const std::string& action,const std::string& resource){
    const auto snapshot=list(session_id);return std::any_of(snapshot.grants.begin(),snapshot.grants.end(),[&](const auto& grant){
        return grant.workspace_id==workspace_id&&grant.policy_binding==policy_binding&&grant.action==action&&grant.resource==resource;
    });
}
AgentPermissionGrantSnapshot AgentPermissionGrantStore::list(const std::string& session_id){
    text(session_id,128,"Invalid permission grant session");return read_catalog(persistence_,session_id).first;
}
AgentPermissionGrantSnapshot AgentPermissionGrantStore::grant(const std::string& session_id,const std::string& operation_id){
    const auto context=request(operation_id);if(!context||context->session_id!=session_id)throw NotFound("Agent permission request not found in this session");
    for(int attempt=0;attempt<8;++attempt){auto [snapshot,raw]=read_catalog(persistence_,session_id);if(snapshot.revision==max_revision)throw Conflict("Agent permission grant revision exhausted");
        const auto existing=std::find_if(snapshot.grants.begin(),snapshot.grants.end(),[&](const auto& value){return value.id==context->grant_id;});
        if(existing!=snapshot.grants.end())return snapshot;
        if(snapshot.grants.size()>=256)throw Conflict("Session has too many saved agent permission grants");
        snapshot.grants.push_back({context->grant_id,context->workspace_id,context->policy_binding,context->action,context->resource,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()});
        ++snapshot.revision;try{persistence_.compare_information(catalog_category,session_id,encode_catalog(snapshot),std::move(raw)).get();return snapshot;}
        catch(const Conflict&){if(attempt==7)throw;}
    }
    throw Conflict("Agent permission grants changed concurrently");
}
AgentPermissionGrantSnapshot AgentPermissionGrantStore::revoke(const std::string& session_id,const std::string& grant_id,std::int64_t expected_revision){
    text(session_id,128,"Invalid permission grant session");text(grant_id,128,"Invalid permission grant ID");if(expected_revision<0||expected_revision>=max_revision)throw std::invalid_argument("Invalid permission grant revision");
    for(int attempt=0;attempt<8;++attempt){auto [snapshot,raw]=read_catalog(persistence_,session_id);if(snapshot.revision!=expected_revision)throw Conflict("Agent permission grants changed; refresh before revoking");
        const auto found=std::find_if(snapshot.grants.begin(),snapshot.grants.end(),[&](const auto& value){return value.id==grant_id;});if(found==snapshot.grants.end())throw NotFound("Saved agent permission grant not found");
        snapshot.grants.erase(found);++snapshot.revision;try{persistence_.compare_information(catalog_category,session_id,encode_catalog(snapshot),std::move(raw)).get();return snapshot;}
        catch(const Conflict&){if(attempt==7)throw;}
    }
    throw Conflict("Agent permission grants changed concurrently");
}
}
