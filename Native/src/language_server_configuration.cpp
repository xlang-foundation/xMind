#include "agentflow/language_server_configuration.hpp"
#include "agentflow/authoring_document.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/process.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::int64_t maximum=9007199254740991;
constexpr const char* credential_scope="server";

void fields(const Json& value,std::initializer_list<const char*> allowed){
    if(!value.is_object())throw std::invalid_argument("Language-server configuration entries must be objects");
    for(auto it=value.begin();it!=value.end();++it){bool known=false;for(const auto* key:allowed)if(it.key()==key)known=true;if(!known)throw std::invalid_argument("Unknown language-server configuration field");}
}
std::string string(const Json& value,const char* key,std::size_t limit,bool optional=false){
    if(!value.contains(key)){if(optional)return {};throw std::invalid_argument("Missing language-server configuration text");}
    if(!value[key].is_string())throw std::invalid_argument("Invalid language-server configuration text");
    const auto result=value[key].get<std::string>();
    if((!optional&&result.empty())||result.size()>limit||result.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid language-server configuration text");
    return result;
}
void identity(const std::string& value){if(value.empty()||value.size()>64||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid language-server profile identity");}
std::string parse_input(const std::string& source){
    if(source.size()>256*1024)throw std::invalid_argument("Language-server configuration exceeds limits");
    const auto first=source.find_first_not_of(" \t\r\n");if(first==std::string::npos)throw std::invalid_argument("Empty language-server configuration");
    if(source[first]=='{'||source[first]=='['){try{return mcp_compact_object(source);}catch(const McpProtocolError&){throw std::invalid_argument("Invalid language-server configuration JSON");}}
    try{return authoring_yaml_to_json(source);}catch(const std::invalid_argument&){throw std::invalid_argument("Invalid language-server configuration YAML");}
}
std::string lower(std::string value){for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
void read_environment(const Json& value,LanguageServerProfile& result){
    if(!value.is_object()||value.size()>64)throw std::invalid_argument("Invalid language-server environment");
    std::set<std::string> names;
    for(auto it=value.begin();it!=value.end();++it){
        if(it.key().empty()||it.key().size()>256||it.key().find('=')!=std::string::npos||it.key().find('\0')!=std::string::npos||!it.value().is_string())throw std::invalid_argument("Invalid language-server environment entry");
        const auto name=lower(it.key()),entry=it.value().get<std::string>();
        if(!names.insert(name).second||entry.size()>32768||entry.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid language-server environment entry");
        result.environment.emplace_back(it.key(),entry);
    }
}
std::string environment_purpose(const std::string& profile_id){return "language-server.environment."+profile_id;}
std::string new_environment_id(const LanguageServerProfile& profile){return "language-server-env-"+profile.id+"-r"+std::to_string(profile.revision);}
std::string environment_json(const LanguageServerProfile& profile){Json value=Json::object();for(const auto& [key,entry]:profile.environment)value[key]=entry;return value.dump();}
class WipedString {
public:
    explicit WipedString(std::string value):value_(std::move(value)){}
    ~WipedString(){std::fill(value_.begin(),value_.end(),'\0');}
    const std::string& value()const{return value_;}
private:std::string value_;
};
void store_environment(PersistenceService& store,LanguageServerProfile& profile){
    if(profile.environment.empty()){profile.environment_credential_id.clear();return;}
    profile.environment_credential_id=new_environment_id(profile);
    const auto payload=environment_json(profile);
    SecretBytes secret(std::span(reinterpret_cast<const std::uint8_t*>(payload.data()),payload.size()));
    store.put_credential(credential_scope,profile.environment_credential_id,environment_purpose(profile.id),"Native language-server environment",std::move(secret),0).get();
}
void load_environment(PersistenceService& store,std::vector<LanguageServerProfile>& profiles){
    for(auto& profile:profiles){
        if(profile.environment_credential_id.empty())continue;
        auto secret=store.resolve_credential(credential_scope,profile.environment_credential_id,environment_purpose(profile.id)).get();
        const auto bytes=secret.view();WipedString payload(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
        auto value=Json::parse(payload.value());profile.environment.clear();read_environment(value,profile);
        for(auto it=value.begin();it!=value.end();++it)if(it.value().is_string()){auto& entry=it.value().get_ref<Json::string_t&>();std::fill(entry.begin(),entry.end(),'\0');}
    }
}
LanguageServerProfile profile(const Json& value,bool stored){
    fields(value,{"id","executable","language_id","extensions","arguments","environment","environment_credential_id","request_timeout_ms","revision","executable_id"});
    if(!stored&&(value.contains("revision")||value.contains("executable_id")||value.contains("environment_credential_id")))throw std::invalid_argument("Language-server revision, credential reference and executable identity are backend-owned");
    if(stored&&value.contains("environment"))throw std::invalid_argument("Plaintext language-server environment is not allowed in profile metadata");
    LanguageServerProfile result;result.id=string(value,"id",64);identity(result.id);result.executable=string(value,"executable",32768);result.language_id=string(value,"language_id",64);
    if(result.language_id.find_first_of(" \t\r\n")!=std::string::npos)throw std::invalid_argument("Invalid language-server language ID");
    const auto executable=std::filesystem::u8path(result.executable);if(!executable.is_absolute()||!std::filesystem::is_regular_file(executable))throw std::invalid_argument("Language-server executable must be an absolute existing file");
    if(!value.contains("extensions")||!value["extensions"].is_array()||value["extensions"].empty()||value["extensions"].size()>32)throw std::invalid_argument("Invalid language-server extensions");
    for(const auto& item:value["extensions"]){
        if(!item.is_string())throw std::invalid_argument("Language-server extensions must be strings");const auto extension=item.get<std::string>();
        if(extension.size()<2||extension.size()>16||extension.front()!='.'||extension.find_first_of("/\\")!=std::string::npos||extension.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid language-server extension");
        if(std::any_of(result.extensions.begin(),result.extensions.end(),[&](const auto& existing){return lower(existing)==lower(extension);}))throw std::invalid_argument("Duplicate language-server extension");result.extensions.push_back(extension);
    }
    if(value.contains("arguments")){if(!value["arguments"].is_array()||value["arguments"].size()>32)throw std::invalid_argument("Invalid language-server arguments");for(const auto& item:value["arguments"]){if(!item.is_string()||item.get<std::string>().size()>4096||item.get<std::string>().find('\0')!=std::string::npos)throw std::invalid_argument("Invalid language-server argument");result.arguments.push_back(item.get<std::string>());}}
    if(!stored&&value.contains("environment"))read_environment(value["environment"],result);
    if(!value.contains("request_timeout_ms")||!value["request_timeout_ms"].is_number_integer()||value["request_timeout_ms"]<1000||value["request_timeout_ms"]>120000)throw std::invalid_argument("Invalid language-server request timeout");
    result.request_timeout=std::chrono::milliseconds(value["request_timeout_ms"].get<std::int64_t>());
    if(stored){
        if(!value.contains("revision")||!value["revision"].is_number_integer()||value["revision"]<1||value["revision"]>maximum)throw std::invalid_argument("Invalid stored language-server revision");
        result.revision=value["revision"].get<std::int64_t>();result.executable_id=string(value,"executable_id",512);
        if(!result.executable_id.starts_with("windows-local-executable-v1:"))throw std::invalid_argument("Invalid stored language-server executable identity");
        if(value.contains("environment_credential_id")){result.environment_credential_id=string(value,"environment_credential_id",256);if(!result.environment_credential_id.starts_with("language-server-env-"))throw std::invalid_argument("Invalid language-server environment credential reference");}
    }else result.executable_id=ForegroundProcess::executable_identity(result.executable);
    return result;
}
Json encode(const LanguageServerProfile& profile,bool stored){
    Json result={{"id",profile.id},{"executable",profile.executable},{"executable_id",profile.executable_id},{"language_id",profile.language_id},{"extensions",profile.extensions},{"arguments",profile.arguments},{"request_timeout_ms",profile.request_timeout.count()}};
    if(stored){if(!profile.environment_credential_id.empty())result["environment_credential_id"]=profile.environment_credential_id;result["revision"]=profile.revision;}
    else{Json environment=Json::object();for(const auto& [key,value]:profile.environment)environment[key]=value;result["environment"]=std::move(environment);}
    return result;
}
Json saved(PersistenceService& store){
    try{const auto value=Json::parse(store.information("native-language-servers","profiles").get());fields(value,{"version","revision","profiles","retired_ids"});if(value.value("version",0)!=1||!value.contains("revision")||!value["revision"].is_number_integer()||value["revision"]<0||value["revision"]>maximum||!value.contains("profiles")||!value["profiles"].is_array()||value["profiles"].size()>16||!value.contains("retired_ids")||!value["retired_ids"].is_array()||value["retired_ids"].size()>4096)throw std::invalid_argument("Invalid saved language-server configuration");return value;}
    catch(const NotFound&){return {{"version",1},{"revision",0},{"profiles",Json::array()},{"retired_ids",Json::array()}};}
}
std::set<std::string> retired(const Json& data,const std::vector<LanguageServerProfile>& profiles){
    std::set<std::string> active,all;for(const auto& profile:profiles)active.insert(profile.id);for(const auto& item:data["retired_ids"]){if(!item.is_string())throw std::invalid_argument("Invalid retired language-server profile");const auto value=item.get<std::string>();identity(value);if(!all.insert(value).second||active.contains(value))throw std::invalid_argument("Invalid retired language-server profile");}return all;
}
std::vector<LanguageServerProfile> list(const Json& data,bool stored){
    if(!data.contains("profiles")||!data["profiles"].is_array()||data["profiles"].size()>16)throw std::invalid_argument("Invalid language-server profile count");
    std::vector<LanguageServerProfile> result;std::set<std::string> seen;for(const auto& item:data["profiles"]){auto profile_value=profile(item,stored);if(!seen.insert(profile_value.id).second)throw std::invalid_argument("Duplicate language-server profile");result.push_back(std::move(profile_value));}return result;
}
void remove_unused_environment_credentials(PersistenceService& store,const std::vector<LanguageServerProfile>& before,const std::vector<LanguageServerProfile>& after){
    std::set<std::string> retained;for(const auto& profile:after)if(!profile.environment_credential_id.empty())retained.insert(profile.environment_credential_id);
    for(const auto& profile:before)if(!profile.environment_credential_id.empty()&&!retained.contains(profile.environment_credential_id)){
        for(const auto& credential:store.credentials(credential_scope).get())if(credential.id==profile.environment_credential_id){store.delete_credential(credential_scope,credential.id,credential.revision).get();break;}
    }
}
}

std::vector<LanguageServerProfile> LanguageServerConfigurationStore::load(){
    const auto data=saved(store_);auto profiles=list(data,true);(void)retired(data,profiles);load_environment(store_,profiles);
    for(const auto& profile:profiles)if(ForegroundProcess::executable_identity(profile.executable)!=profile.executable_id)throw std::invalid_argument("Registered language-server executable changed; reimport its trusted profile");
    return profiles;
}
std::vector<LanguageServerProfile> LanguageServerConfigurationStore::apply(const std::string& source){
    const auto desired=Json::parse(parse_input(source));fields(desired,{"profiles"});auto profiles=list(desired,false);const auto before=saved(store_);auto previous=list(before,true);(void)retired(before,previous);load_environment(store_,previous);
    std::map<std::string,LanguageServerProfile> old;for(const auto& profile:previous)old.emplace(profile.id,profile);auto retired_ids=retired(before,previous);std::set<std::string> active;bool changed=false;
    for(auto& profile:profiles){
        if(retired_ids.contains(profile.id))throw Conflict("Language-server profile identity is retired");active.insert(profile.id);const auto found=old.find(profile.id);
        if(found==old.end()){profile.revision=1;changed=true;}
        else{profile.revision=found->second.revision;if(encode(profile,false)!=encode(found->second,false)){if(profile.revision>=maximum)throw Conflict("Language-server profile revision exhausted");++profile.revision;changed=true;}}
        if(found!=old.end()&&profile.environment==found->second.environment)profile.environment_credential_id=found->second.environment_credential_id;
        else if(!profile.environment.empty())store_environment(store_,profile);
    }
    for(const auto& [id,profile]:old)if(!active.contains(id)){(void)profile;retired_ids.insert(id);changed=true;}
    if(retired_ids.size()>4096)throw Conflict("Retired language-server profile limit reached");
    const auto previous_revision=before["revision"].get<std::int64_t>();if(changed&&previous_revision>=maximum)throw Conflict("Language-server configuration revision exhausted");
    Json entries=Json::array();for(const auto& profile:profiles)entries.push_back(encode(profile,true));const auto record=Json{{"version",1},{"revision",changed?previous_revision+1:previous_revision},{"profiles",std::move(entries)},{"retired_ids",retired_ids}}.dump();
    if(record.size()>256*1024)throw Conflict("Stored language-server configuration exceeds limits");store_.put_information("native-language-servers","profiles",record).get();remove_unused_environment_credentials(store_,previous,profiles);return profiles;
}
}
