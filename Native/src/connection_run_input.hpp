#pragma once
#include "agentflow/connection_binding.hpp"
#include "nlohmann/json.hpp"
#include <array>
#include <initializer_list>
#include <algorithm>
#include <string_view>
namespace agentflow::connection_input {
using Json=nlohmann::json;
inline std::string string_field(const Json& value,const char* key,std::size_t max=4096) {
    if(!value.contains(key) || !value[key].is_string()) throw std::invalid_argument("Missing or invalid string field");
    auto result=value[key].get<std::string>();
    if(result.empty() || result.size()>max || result.find('\0')!=std::string::npos) throw std::invalid_argument("String field exceeds limits");
    return result;
}
inline std::pair<OperationDecision,bool> permission_reply(const Json& value){if(!value.is_object())throw std::invalid_argument("Invalid permission reply");for(const auto& item:value.items())if(item.key()!="decision"&&item.key()!="grant_resource")throw std::invalid_argument("Unknown permission reply field");const auto decision=string_field(value,"decision",8);if((decision!="allow"&&decision!="deny")||(value.contains("grant_resource")&&!value["grant_resource"].is_boolean())||(value.value("grant_resource",false)&&decision!="allow"))throw std::invalid_argument("Invalid permission reply");return {decision=="allow"?OperationDecision::allow:OperationDecision::deny,value.value("grant_resource",false)};}
inline ConnectionAgentSelectionRequest agent_selection(const Json& value){
    if(!value.is_object()||value.size()!=3||!value.contains("agent_id")||(!value["agent_id"].is_null()&&!value["agent_id"].is_string())||!value.contains("agent_revision")||!value["agent_revision"].is_number_integer()||value["agent_revision"]<0||value["agent_revision"]>9007199254740991LL||!value.contains("expected_revision")||!value["expected_revision"].is_number_integer()||value["expected_revision"]<0||value["expected_revision"]>=9007199254740991LL)throw std::invalid_argument("Agent selection requires an ID, definition revision and expected session revision");
    ConnectionAgentSelectionRequest result;result.agent_revision=value["agent_revision"].get<std::int64_t>();result.revision=value["expected_revision"].get<std::int64_t>();
    if(value["agent_id"].is_string()){result.id=string_field(value,"agent_id",64);if(result.agent_revision<1||result.id->find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid named agent selection");}else if(result.agent_revision!=0)throw std::invalid_argument("Clearing agent selection requires definition revision zero");return result;
}
inline std::int64_t provider_revision(const Json& value){if(!value.contains("expected_revision")||!value["expected_revision"].is_number_integer()||value["expected_revision"]<0||value["expected_revision"]>=9007199254740991LL)throw std::invalid_argument("Invalid provider profile revision");return value["expected_revision"].get<std::int64_t>();}
inline void provider_fields(const Json& value,std::initializer_list<const char*> allowed){if(!value.is_object())throw std::invalid_argument("Invalid provider request");for(const auto& item:value.items())if(std::none_of(allowed.begin(),allowed.end(),[&](auto key){return item.key()==key;}))throw std::invalid_argument("Unknown provider request field");}
inline SecretBytes provider_key(const Json& value){if(!value.contains("api_key"))return SecretBytes(std::span<const std::uint8_t>{});if(!value["api_key"].is_string())throw std::invalid_argument("Invalid provider credential");const auto& key=value["api_key"].get_ref<const std::string&>();if(key.size()>32768||key.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid provider credential");return SecretBytes({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()});}
inline BoundConnectionProviders save_provider(CapturedLocalConnection& state,const Json& value){provider_fields(value,{"id","route_id","model","api_key","expected_revision","activate"});if(value.contains("activate")&&!value["activate"].is_boolean())throw std::invalid_argument("Invalid provider activation");return state.save_provider(string_field(value,"id",256),string_field(value,"route_id",256),string_field(value,"model",256),provider_key(value),provider_revision(value),value.value("activate",false));}
inline BoundConnectionProviderModels discover_provider_models(CapturedLocalConnection& state,const Json& value){provider_fields(value,{"id","route_id","api_key","expected_revision"});return state.discover_provider_models(string_field(value,"id",256),string_field(value,"route_id",256),provider_key(value),provider_revision(value));}
inline BoundConnectionProviders import_providers(CapturedLocalConnection& state,const Json& value){provider_fields(value,{"path","expected_revision"});return state.import_providers(string_field(value,"path",32768),provider_revision(value));}
inline std::optional<ProviderProfileAdmission> profile_admission(const Json& value){
    const bool named=value.contains("provider_profile_id"),versioned=value.contains("expected_provider_revision");
    if(named!=versioned)throw std::invalid_argument("Provider profile and revision must be supplied together");
    if(!named)return {};
    if(!value["provider_profile_id"].is_string()||!value["expected_provider_revision"].is_number_integer()||value["expected_provider_revision"]<0||value["expected_provider_revision"]>9007199254740991)throw std::invalid_argument("Invalid provider profile admission binding");
    const auto id=value["provider_profile_id"].get<std::string>();
    if(id.empty()||id.size()>256||id.starts_with("sk-")||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)throw std::invalid_argument("Invalid provider profile admission identity");
    return ProviderProfileAdmission{id,value["expected_provider_revision"].get<std::int64_t>()};
}
inline ConnectionModelSelectionRequest model_selection(const Json& value){
    if(!value.is_object()||value.size()!=4||!value.contains("model_id")||(!value["model_id"].is_null()&&!value["model_id"].is_string())||!value.contains("expected_revision")||!value["expected_revision"].is_number_integer()||value["expected_revision"]<0||value["expected_revision"]>=9007199254740991LL)throw std::invalid_argument("Model selection requires model, provider binding and session revision");
    const auto profile=profile_admission(value);if(!profile)throw std::invalid_argument("Model selection requires provider binding");std::optional<std::string> model;if(value["model_id"].is_string())model=string_field(value,"model_id",256);return {std::move(model),*profile,value["expected_revision"].get<std::int64_t>()};
}
inline std::optional<WorkspaceAdmission> workspace_admission(const Json& value){
    const bool named=value.contains("expected_workspace_id"),versioned=value.contains("expected_workspace_authority_id");
    if(named!=versioned)throw std::invalid_argument("Workspace identity and authority must be supplied together");
    if(!named){if(value.contains("expected_workspace_revision"))throw std::invalid_argument("Workspace revision requires workspace identity and authority");return {};}
    const auto id=string_field(value,"expected_workspace_id",256),authority=string_field(value,"expected_workspace_authority_id",32);
    if(id.empty()||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos||
       authority.size()!=32||authority.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::invalid_argument("Invalid workspace admission identity");
    std::int64_t revision=0;if(value.contains("expected_workspace_revision")){const auto& raw=value.at("expected_workspace_revision");if(!raw.is_number_integer()||raw<1||raw>9007199254740991LL)throw std::invalid_argument("Invalid workspace membership revision");revision=raw.get<std::int64_t>();}
    return WorkspaceAdmission{id,authority,revision};
}
inline std::vector<PromptFileAttachment> prompt_file_attachments(const Json& value){
    std::vector<PromptFileAttachment> result;if(!value.contains("files"))return result;
    const auto& files=value.at("files");if(!files.is_array()||files.size()>32)throw std::invalid_argument("Prompt files must be an array of at most 32 workspace files");
    result.reserve(files.size());for(const auto& file:files){
        if(!file.is_object()||file.empty()||file.size()>5)throw std::invalid_argument("Invalid prompt file attachment");
        for(auto it=file.begin();it!=file.end();++it)if(it.key()!="path"&&it.key()!="folder_id"&&it.key()!="name"&&it.key()!="description"&&it.key()!="mention")throw std::invalid_argument("Unknown prompt file attachment field");
        const auto field=[&](const char* key,std::size_t limit){if(!file.contains(key))return std::string{};if(!file.at(key).is_string())throw std::invalid_argument("Invalid prompt file attachment metadata");const auto text=file.at(key).get<std::string>();if(text.size()>limit||text.find('\0')!=std::string::npos)throw std::invalid_argument("Prompt file attachment metadata exceeds limits");return text;};
        PromptFileAttachment parsed;parsed.path=field("path",4096);if(parsed.path.empty())throw std::invalid_argument("Prompt file attachment path is required");
        parsed.folder_id=field("folder_id",256);parsed.name=field("name",512);parsed.description=field("description",4096);parsed.mention=field("mention",512);result.push_back(std::move(parsed));
    }return result;
}
inline std::string identifier(const std::string& value) {
    if(value.empty() || value.size()>128) throw std::invalid_argument("Invalid ID");
    for(unsigned char c:value) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-')) throw std::invalid_argument("Invalid ID");
    return value;
}
inline ConnectionRunRequest run_request(const Json& value,std::string default_id={}){
    if(!value.is_object())throw std::invalid_argument("Expected a run request object");
    constexpr std::array<std::string_view,11> fields{"id","session_id","prompt","files","model_id","provider_profile_id","expected_provider_revision","expected_workspace_id","expected_workspace_authority_id","expected_workspace_revision","inbox_id"};
    for(auto it=value.begin();it!=value.end();++it)if(std::find(fields.begin(),fields.end(),it.key())==fields.end())throw std::invalid_argument("Unknown run request field");
    ConnectionRunRequest result;result.id=value.contains("id")?identifier(string_field(value,"id",128)):identifier(default_id);result.session=identifier(string_field(value,"session_id",128));result.prompt=string_field(value,"prompt",1024*1024);result.model=value.contains("model_id")?string_field(value,"model_id",256):std::string{};result.provider=profile_admission(value);result.workspace=workspace_admission(value);result.files=prompt_file_attachments(value);if(value.contains("inbox_id"))result.inbox=string_field(value,"inbox_id",128);return result;
}
inline ConnectionFindRequest find_request(const Json& value){if(!value.is_object())throw std::invalid_argument("Expected filename search object");for(auto it=value.begin();it!=value.end();++it)if(it.key()!="query"&&it.key()!="path"&&it.key()!="type"&&it.key()!="limit"&&it.key()!="hidden"&&it.key()!="respect_ignore")throw std::invalid_argument("Unknown filename search field");ConnectionFindRequest result;result.query=string_field(value,"query",4096);if(value.contains("path"))result.path=string_field(value,"path",32768);if(value.contains("type"))result.type=string_field(value,"type",16);if(value.contains("limit")){if(!value["limit"].is_number_integer()||value["limit"]<1||value["limit"]>1000)throw std::invalid_argument("Invalid filename search limit");result.limit=value["limit"].get<std::size_t>();}for(const auto* field:{"hidden","respect_ignore"})if(value.contains(field)&&!value[field].is_boolean())throw std::invalid_argument("Invalid filename search boolean");result.hidden=value.value("hidden",false);result.respect_ignore=value.value("respect_ignore",true);return result;}
inline ConnectionCommandRequest command_request(const Json& value,std::string session,std::string default_id={}){
    if(!value.is_object())throw std::invalid_argument("Expected command request object");for(auto it=value.begin();it!=value.end();++it)if(it.key()!="id"&&it.key()!="command_id"&&it.key()!="arguments"&&it.key()!="model_id"&&it.key()!="provider_profile_id"&&it.key()!="expected_provider_revision"&&it.key()!="graph_id"&&it.key()!="graph_revision"&&it.key()!="expected_workspace_id"&&it.key()!="expected_workspace_authority_id")throw std::invalid_argument("Unknown command field");
    ConnectionCommandRequest request;request.id=value.contains("id")?string_field(value,"id",128):std::move(default_id);if(request.id.empty()||session.empty()||session.size()>128)throw std::invalid_argument("Command requires run and session identity");request.id=identifier(request.id);request.session=identifier(session);request.command=string_field(value,"command_id",256);if(value.contains("arguments")){if(!value["arguments"].is_string())throw std::invalid_argument("Command arguments must be text");request.arguments=value["arguments"].get<std::string>();if(request.arguments.size()>8192||request.arguments.find('\0')!=std::string::npos)throw std::invalid_argument("Command arguments exceed their bounds");}if(value.contains("model_id"))request.model=string_field(value,"model_id",256);const auto workspace=workspace_admission(value);if(!workspace)throw std::invalid_argument("Command requires workspace authority");request.workspace=*workspace;request.provider=profile_admission(value);const bool graph=value.contains("graph_id");if(graph!=value.contains("graph_revision"))throw std::invalid_argument("Graph command requires exact revision");if(graph){request.graph=string_field(value,"graph_id",64);if(!value["graph_revision"].is_number_integer()||value["graph_revision"]<1||value["graph_revision"]>=9007199254740991LL)throw std::invalid_argument("Invalid graph command revision");request.graph_revision=value["graph_revision"].get<std::int64_t>();}return request;
}
inline ConnectionGraphRequest graph_request(Json value,std::string default_id={}){
    if(!value.is_object())throw std::invalid_argument("Expected graph request object");
    auto graph=string_field(value,"graph_id",64);if(!value.contains("graph_revision")||!value["graph_revision"].is_number_integer()||value["graph_revision"]<1||value["graph_revision"]>9007199254740991LL)throw std::invalid_argument("Invalid graph revision");
    const auto revision=value["graph_revision"].get<std::int64_t>();value.erase("graph_id");value.erase("graph_revision");if(value.contains("inbox_id"))throw std::invalid_argument("Captured graph queue admission is unavailable");return {run_request(value,std::move(default_id)),std::move(graph),revision};
}

inline ConnectionContextRequest context_request(const Json& value,std::string default_id={}){
    if(!value.is_object()||value.size()>5)throw std::invalid_argument("Invalid context request");for(const auto& item:value.items())if(item.key()!="id"&&item.key()!="model_id"&&item.key()!="expected_head_revision"&&item.key()!="provider_profile_id"&&item.key()!="expected_provider_revision")throw std::invalid_argument("Unknown context request field");
    if(!value.contains("expected_head_revision")||!value["expected_head_revision"].is_number_integer()||value["expected_head_revision"]<0||value["expected_head_revision"]>9007199254740991LL)throw std::invalid_argument("Invalid observed context head revision");
    ConnectionContextRequest result;result.id=identifier(value.contains("id")?string_field(value,"id",128):default_id);result.model=value.contains("model_id")?string_field(value,"model_id",256):std::string{};result.head=value["expected_head_revision"].get<std::int64_t>();result.provider=profile_admission(value);return result;
}
inline ConnectionSkillSelectionRequest skill_selection(const Json& value){
    if(!value.is_object()||value.size()!=4||!value.contains("ids")||!value["ids"].is_array()||value["ids"].size()>8||!value.contains("expected_revision")||!value["expected_revision"].is_number_integer()||value["expected_revision"]<0||value["expected_revision"]>=9007199254740991LL)throw std::invalid_argument("Skill changes require exact ids, revision and workspace authority");
    const auto workspace=workspace_admission(value);if(!workspace)throw std::invalid_argument("Skill changes require workspace authority");ConnectionSkillSelectionRequest result;result.workspace=*workspace;result.revision=value["expected_revision"].get<std::int64_t>();for(const auto& id:value["ids"]){if(!id.is_string())throw std::invalid_argument("Skill ids must be strings");result.ids.push_back(id.get<std::string>());}validate_skill_selections({result.workspace.workspace_id,result.ids,{}});return result;
}
}
