#include "agentflow/agent_authority.hpp"
#include "agentflow/agent_runner.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace agentflow {
namespace {
using Json=nlohmann::json;
using Reference=std::tuple<std::string,std::string,std::string>;
void text(const std::string& value,std::size_t bound,bool required=true){
    if((required&&value.empty())||value.size()>bound||value.find('\0')!=std::string::npos)
        throw std::invalid_argument("Invalid private agent authority binding");
    try{(void)Json(value).dump();}catch(const Json::type_error&){throw std::invalid_argument("Invalid UTF-8 agent authority binding");}
}
std::set<Reference> references(const AgentSettings& settings){
    if(settings.mcp_servers.size()>16)throw std::invalid_argument("Agent authority MCP count exceeds limits");
    std::set<Reference> result;
    if(settings.credential)result.emplace(settings.credential->scope,settings.credential->id,settings.credential->purpose);
    for(const auto& server:settings.mcp_servers){
        text(server.id,64);text(server.executable,32768);text(server.working_directory,32768);
        if(server.arguments.size()>64||server.credentials.size()>32||server.revision<1)
            throw std::invalid_argument("Agent authority MCP configuration exceeds limits");
        for(const auto& argument:server.arguments)text(argument,4096,false);
        for(const auto& credential:server.credentials){text(credential.name,128);text(credential.scope,256);text(credential.id,256);
            if(server.enabled)
            result.emplace(credential.scope,credential.id,mcp_credential_purpose(server,credential.name));
        }
    }
    for(const auto& [scope,id,purpose]:result){text(scope,256);text(id,256);text(purpose,4096);}
    return result;
}
std::string wire(ProviderWire value){
    switch(value){case ProviderWire::chat_completions:return "chat-completions";
    case ProviderWire::responses:return "responses";case ProviderWire::anthropic_messages:return "anthropic-messages";
    case ProviderWire::gemini_generate_content:return "gemini-generate-content";}
    throw std::invalid_argument("Invalid agent authority provider wire");
}
int capability(Capability value){
    switch(value){case Capability::unknown:return 0;case Capability::unsupported:return 1;case Capability::supported:return 2;}
    throw std::invalid_argument("Invalid agent authority capability");
}
int effort(ReasoningEffort value){
    switch(value){case ReasoningEffort::none:return 0;case ReasoningEffort::minimal:return 1;
    case ReasoningEffort::low:return 2;case ReasoningEffort::medium:return 3;case ReasoningEffort::high:return 4;
    case ReasoningEffort::xhigh:return 5;case ReasoningEffort::max:return 6;}
    throw std::invalid_argument("Invalid agent authority reasoning policy");
}
std::string digest(const std::string& bytes){
    if(bytes.size()>1024*1024)throw std::invalid_argument("Private agent authority binding exceeds limits");
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        throw DatabaseError("Cannot bind admitted agent authority");
    std::array<UCHAR,32> value{};
    const auto status=BCryptHash(algorithm,nullptr,0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),
        value.data(),static_cast<ULONG>(value.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)throw DatabaseError("Cannot bind admitted agent authority");
    std::ostringstream result;result<<std::hex<<std::setfill('0');
    for(const auto byte:value)result<<std::setw(2)<<static_cast<unsigned>(byte);
    return result.str();
}
}
std::vector<AgentAuthorityCredentialVersion> agent_authority_credentials(
    PersistenceService& store,const AgentSettings& settings){
    std::set<std::string> scopes;std::vector<CredentialMetadata> available;
    for(const auto& [scope,id,purpose]:references(settings)){
        (void)id;(void)purpose;
        if(scopes.insert(scope).second){auto values=store.credentials(scope).get();
            if(std::any_of(values.begin(),values.end(),[&](const auto& value){return value.scope!=scope;}))
                throw Conflict("Configured agent credential metadata scope changed");
            available.insert(available.end(),values.begin(),values.end());}
    }
    return select_agent_authority_credentials(settings,available);
}
std::vector<AgentAuthorityCredentialVersion> select_agent_authority_credentials(
    const AgentSettings& settings,const std::vector<CredentialMetadata>& available){
    std::vector<AgentAuthorityCredentialVersion> result;
    for(const auto& [scope,id,purpose]:references(settings)){
        const CredentialMetadata* match=nullptr;
        for(const auto& value:available)if(value.scope==scope&&value.id==id){
            if(match||value.purpose!=purpose||value.revision<1||value.revision>9007199254740991)
                throw Conflict("Configured agent credential metadata changed");
            match=&value;
        }
        if(!match)throw Conflict("Configured agent credential metadata is unavailable");
        result.push_back({scope,id,purpose,match->revision});
    }
    return result;
}
std::string agent_authority_identity(const AgentSettings& settings,const std::string& selected_model,
    const std::string& workspace,const std::vector<AgentAuthorityCredentialVersion>& credentials){
    const auto& model=selected_model.empty()?settings.provider.model:selected_model;
    text(model,256);text(workspace,4096);text(settings.provider.endpoint,4096);
    text(settings.instructions,65536,false);text(settings.instruction_policy.instructions,32768,false);
    if(!settings.workspace)throw std::invalid_argument("Planning authority requires a configured workspace");
    if(settings.selectable_models.size()>64||settings.process_profiles.size()>16)
        throw std::invalid_argument("Private agent authority catalogue exceeds limits");
    for(const auto& selectable:settings.selectable_models)text(selectable,256);
    for(const auto& profile:settings.process_profiles){text(profile.id,64);text(profile.executable,32768);text(profile.executable_id,4096);
        if(profile.prefix_arguments.size()>32||profile.revision<1||profile.max_timeout.count()<1||profile.max_timeout.count()>600000)
            throw std::invalid_argument("Private agent authority process policy exceeds limits");
        for(const auto& argument:profile.prefix_arguments)text(argument,4096,false);
    }
    if(settings.provider.tools!=Capability::supported||settings.max_turns<1||settings.max_turns>128||
        settings.run_timeout.count()<1||settings.run_timeout.count()>3600000||
        (model!=settings.provider.model&&std::find(settings.selectable_models.begin(),settings.selectable_models.end(),model)==settings.selectable_models.end()))
        throw std::invalid_argument("Invalid admitted planning execution policy");
    text(*settings.workspace,32768);
    const auto expected=references(settings);std::map<Reference,std::int64_t> versions;
    for(const auto& value:credentials){
        const Reference reference{value.scope,value.id,value.purpose};
        if(!expected.contains(reference)||value.revision<1||value.revision>9007199254740991||
            !versions.emplace(reference,value.revision).second)
            throw std::invalid_argument("Invalid exact agent credential metadata binding");
    }
    if(versions.size()!=expected.size())throw std::invalid_argument("Incomplete agent credential metadata binding");
    Json encoded={{"schema",1},{"workspace_identity",workspace},{"workspace",*settings.workspace},
        {"instructions",settings.instructions},{"instruction_policy",{{"revision",settings.instruction_policy.revision},{"instructions",settings.instruction_policy.instructions}}},
        {"approved_edits",settings.approved_edits},{"max_turns",settings.max_turns},
        {"run_timeout_ms",settings.run_timeout.count()},{"selectable_models",settings.selectable_models},
        {"provider",{{"endpoint",settings.provider.endpoint},{"wire",wire(settings.provider.wire)},{"model",model},
            {"tools",capability(settings.provider.tools)},{"stream_usage",capability(settings.provider.stream_usage)},
            {"output_limit",capability(settings.provider.output_limit)},{"reasoning",capability(settings.provider.reasoning)},
            {"deadline_ms",settings.provider.deadline.count()},{"idle_timeout_ms",settings.provider.idle_timeout.count()}}},
        {"credential_versions",Json::array()},{"process_profiles",Json::array()},{"mcp_servers",Json::array()}};
    encoded["max_output_tokens"]=settings.max_output_tokens?Json(*settings.max_output_tokens):Json(nullptr);
    encoded["provider"]["reasoning_effort"]=settings.provider.reasoning_effort?Json(effort(*settings.provider.reasoning_effort)):Json(nullptr);
    encoded["provider_identity"]=settings.provider_identity?Json{{"id",settings.provider_identity->profile_id},
        {"route",settings.provider_identity->route_id},{"provider",settings.provider_identity->provider},
        {"revision",settings.provider_identity->profile_revision}}:Json(nullptr);
    encoded["provider_credential"]=settings.credential?Json{{"scope",settings.credential->scope},{"id",settings.credential->id},
        {"purpose",settings.credential->purpose}}:Json(nullptr);
    encoded["delegation"]=settings.delegation?Json{{"preset",settings.delegation->preset_id},{"revision",settings.delegation->preset_revision},
        {"max_children",settings.delegation->max_children},{"max_parallel",settings.delegation->max_parallel},
        {"max_model_calls",settings.delegation->max_model_calls},{"max_leaf_turns",settings.delegation->max_leaf_turns}}:Json(nullptr);
    encoded["planning"]=settings.planning?Json{{"max_nodes",settings.planning->max_nodes},
        {"max_revisions",settings.planning->max_revisions},{"max_humans",settings.planning->max_humans},
        {"change_bytes",settings.planning->change_bytes},{"human_expiry_ms",settings.planning->human_expiry_ms},
        {"max_parent_turns",settings.planning->max_parent_turns}}:Json(nullptr);
    for(const auto& [reference,revision]:versions){const auto& [scope,id,purpose]=reference;
        encoded["credential_versions"].push_back({{"scope",scope},{"id",id},{"purpose",purpose},{"revision",revision}});}
    for(const auto& profile:settings.process_profiles){
        text(profile.executable_id,4096);
        encoded["process_profiles"].push_back({{"id",profile.id},
        {"revision",profile.revision},{"executable",profile.executable},{"executable_id",profile.executable_id},
        {"prefix_arguments",profile.prefix_arguments},{"max_timeout_ms",profile.max_timeout.count()}});
    }
    for(const auto& server:settings.mcp_servers){Json references=Json::array();
        for(const auto& credential:server.credentials)references.push_back({{"name",credential.name},{"scope",credential.scope},{"id",credential.id}});
        encoded["mcp_servers"].push_back({{"id",server.id},{"revision",server.revision},{"enabled",server.enabled},
            {"executable",server.executable},{"working_directory",server.working_directory},{"arguments",server.arguments},
            {"credential_references",std::move(references)}});
    }
    try{return digest(encoded.dump());}catch(const Json::type_error&){throw std::invalid_argument("Invalid UTF-8 agent authority binding");}
}
}
