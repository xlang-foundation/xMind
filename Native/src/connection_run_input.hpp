#pragma once
#include "agentflow/connection_binding.hpp"
#include "nlohmann/json.hpp"
#include <array>
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
inline ConnectionGraphRequest graph_request(Json value,std::string default_id={}){
    if(!value.is_object())throw std::invalid_argument("Expected graph request object");
    auto graph=string_field(value,"graph_id",64);if(!value.contains("graph_revision")||!value["graph_revision"].is_number_integer()||value["graph_revision"]<1||value["graph_revision"]>9007199254740991LL)throw std::invalid_argument("Invalid graph revision");
    const auto revision=value["graph_revision"].get<std::int64_t>();value.erase("graph_id");value.erase("graph_revision");if(value.contains("inbox_id"))throw std::invalid_argument("Captured graph queue admission is unavailable");return {run_request(value,std::move(default_id)),std::move(graph),revision};
}
}
