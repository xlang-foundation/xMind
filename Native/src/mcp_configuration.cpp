#include "agentflow/mcp_configuration.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/mcp_http_transport.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <map>
#include <set>
#include <limits>
#include <array>
#include <sstream>
#include <iomanip>

namespace agentflow {
namespace {
using Json=nlohmann::json;
void fields(const Json& value,std::initializer_list<const char*> allowed) {
    if(!value.is_object())throw std::invalid_argument("MCP configuration must contain objects");
    for(auto item=value.begin();item!=value.end();++item){bool known=false;for(const auto* key:allowed)if(item.key()==key)known=true;if(!known)throw std::invalid_argument("Unknown MCP configuration field");}
}
std::string text(const Json& value,const char* key,std::size_t limit) {
    if(!value.contains(key) || !value[key].is_string())throw std::invalid_argument("Missing MCP configuration text");
    auto result=value[key].get<std::string>();if(result.empty() || result.size()>limit || result.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid MCP configuration text");return result;
}
void identity(const std::string& value) {
    if(value.empty() || value.size()>64)throw std::invalid_argument("Invalid MCP configuration identity");
    for(const auto byte:value)if(!((byte>='a' && byte<='z') || (byte>='A' && byte<='Z') || (byte>='0' && byte<='9') || byte=='_' || byte=='-' || byte=='.'))throw std::invalid_argument("Invalid MCP configuration identity");
}
std::string environment_name(std::string value) {
    if(value.empty() || value.size()>128)throw std::invalid_argument("Invalid MCP credential environment name");
    for(std::size_t i=0;i<value.size();++i){auto& byte=value[i];if(byte>='a' && byte<='z')byte=static_cast<char>(byte-'a'+'A');if(!(byte=='_' || (byte>='A' && byte<='Z') || (i && byte>='0' && byte<='9')))throw std::invalid_argument("Invalid MCP credential environment name");}
    if(value=="SYSTEMROOT" || value=="TEMP" || value=="TMP")throw std::invalid_argument("System environment cannot be a credential target");return value;
}
McpServerSetting setting(const Json& value,bool stored) {
    const auto transport=text(value,"transport",16);
    if(transport=="stdio")fields(value,{"id","revision","transport","enabled","executable","working_directory","arguments","credentials"});
    else if(transport=="http")fields(value,{"id","revision","transport","enabled","endpoint","credential","oauth"});
    else throw std::invalid_argument("MCP transport is not implemented");
    if(!stored && value.contains("revision"))throw std::invalid_argument("MCP revisions are owned by the backend");
    McpServerSetting result;result.id=text(value,"id",64);identity(result.id);
    result.transport=transport;
    if(transport=="stdio") {
        result.executable=text(value,"executable",32768);result.working_directory=text(value,"working_directory",32768);
        if(!std::filesystem::u8path(result.executable).is_absolute() || !std::filesystem::u8path(result.working_directory).is_absolute())throw std::invalid_argument("MCP executable and directory must be absolute backend paths");
    } else {
        result.endpoint=text(value,"endpoint",8192);validate_mcp_http_endpoint(result.endpoint);
        if(value.contains("credential")){
            fields(value["credential"],{"scope","id"});const auto scope=text(value["credential"],"scope",128);
            if(scope!="server")throw std::invalid_argument("MCP credential scope requires implemented server authorization");
            result.bearer=McpBearerCredential{scope,text(value["credential"],"id",256)};
        }
        if(value.contains("oauth")){
            if(result.bearer)throw std::invalid_argument("MCP HTTP requires one credential method");
            const auto& oauth=value["oauth"];fields(oauth,{"scope","id","issuer","client_id","callback"});
            result.oauth=McpOAuthCredential{text(oauth,"scope",128),text(oauth,"id",256),text(oauth,"issuer",8192),text(oauth,"client_id",2048)};
            if(oauth.contains("callback")){
                const auto& callback=oauth["callback"];fields(callback,{"path","port"});
                if(callback.contains("path"))result.oauth->callback.path=text(callback,"path",128);
                if(callback.contains("port")){
                    if(!callback["port"].is_number_integer()||callback["port"]<0||callback["port"]>65535)throw std::invalid_argument("Invalid native OAuth callback port");
                    result.oauth->callback.port=callback["port"].get<std::uint16_t>();
                }
            }
            (void)mcp_credential_purpose(result,"OAUTH");
        }
    }
    if(value.contains("enabled")){if(!value["enabled"].is_boolean())throw std::invalid_argument("Invalid MCP enabled flag");result.enabled=value["enabled"].get<bool>();}
    if(stored){if(!value.contains("revision") || !value["revision"].is_number_integer() || value["revision"]<1 || value["revision"]>std::numeric_limits<std::int64_t>::max())throw std::invalid_argument("Invalid stored MCP revision");result.revision=value["revision"].get<std::int64_t>();}
    if(value.contains("arguments")){
        if(!value["arguments"].is_array() || value["arguments"].size()>64)throw std::invalid_argument("MCP argument count exceeds limits");
        for(const auto& argument:value["arguments"]){if(!argument.is_string())throw std::invalid_argument("MCP arguments must be literal text");auto bytes=argument.get<std::string>();if(bytes.size()>8192 || bytes.find('\0')!=std::string::npos)throw std::invalid_argument("MCP argument exceeds limits");result.arguments.push_back(std::move(bytes));}
    }
    if(value.contains("credentials")){
        if(!value["credentials"].is_array() || value["credentials"].size()>32)throw std::invalid_argument("MCP credential reference count exceeds limits");std::set<std::string> names;
        for(const auto& credential:value["credentials"]){fields(credential,{"name","scope","id"});auto name=environment_name(text(credential,"name",128));if(!names.insert(name).second)throw std::invalid_argument("Duplicate MCP credential environment name");const auto scope=text(credential,"scope",128);if(scope!="server")throw std::invalid_argument("MCP credential scope requires implemented server authorization");result.credentials.push_back({std::move(name),scope,text(credential,"id",256)});}
    }
    return result;
}
Json encode(const McpServerSetting& value,bool stored=true) {
    Json credentials=Json::array();for(const auto& reference:value.credentials)credentials.push_back({{"name",reference.name},{"scope",reference.scope},{"id",reference.id}});
    Json result{{"id",value.id},{"transport",value.transport},{"enabled",value.enabled}};
    if(value.transport=="http"){
        result["endpoint"]=value.endpoint;
        if(value.bearer)result["credential"]={{"scope",value.bearer->scope},{"id",value.bearer->id}};
        if(value.oauth){
            result["oauth"]={{"scope",value.oauth->scope},{"id",value.oauth->id},{"issuer",value.oauth->issuer},{"client_id",value.oauth->client_id}};
            if(value.oauth->callback.path!="/oauth/callback"||value.oauth->callback.port)result["oauth"]["callback"]={{"path",value.oauth->callback.path},{"port",value.oauth->callback.port}};
        }
    }else {result["executable"]=value.executable;result["working_directory"]=value.working_directory;result["arguments"]=value.arguments;result["credentials"]=std::move(credentials);}
    if(stored)result["revision"]=value.revision;return result;
}
Json parse(const std::string& source) {
    if(source.size()>256*1024)throw std::invalid_argument("MCP configuration exceeds limits");
    try{return Json::parse(mcp_compact_object(source));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid MCP configuration JSON");}
}
Json stored(PersistenceService& store) {
    try{auto value=parse(store.information("native-mcp","servers").get());fields(value,{"version","servers","retired_ids"});if(!value.contains("version") || value["version"]!=1 || !value.contains("retired_ids") || !value["retired_ids"].is_array() || value["retired_ids"].size()>4096)throw std::invalid_argument("Invalid stored MCP configuration version");return value;}
    catch(const NotFound&){return {{"version",1},{"servers",Json::array()},{"retired_ids",Json::array()}};}
}
std::vector<McpServerSetting> list(const Json& value,bool persisted) {
    if(!value.contains("servers") || !value["servers"].is_array() || value["servers"].size()>16)throw std::invalid_argument("MCP server count exceeds limits");std::set<std::string> ids;std::vector<McpServerSetting> result;
    for(const auto& item:value["servers"]){auto parsed=setting(item,persisted);if(!ids.insert(parsed.id).second)throw std::invalid_argument("Duplicate MCP configuration identity");result.push_back(std::move(parsed));}return result;
}
}
std::vector<McpServerSetting> McpConfigurationStore::load(){const auto value=stored(store_);auto values=list(value,true);std::set<std::string> ids,retired;for(const auto& setting:values)ids.insert(setting.id);for(const auto& item:value["retired_ids"]){if(!item.is_string())throw std::invalid_argument("Invalid retired MCP identity");const auto id=item.get<std::string>();identity(id);if(!retired.insert(id).second || ids.contains(id))throw std::invalid_argument("Invalid retired MCP identity");}return values;}
std::vector<McpServerSetting> McpConfigurationStore::apply(const std::string& source) {
    const auto desired=parse(source);fields(desired,{"servers"});auto values=list(desired,false);const auto previous=stored(store_);
    std::map<std::string,McpServerSetting> old;for(const auto& value:list(previous,true))old.emplace(value.id,value);
    std::set<std::string> retired,active;
    for(const auto& id:previous["retired_ids"]){if(!id.is_string())throw std::invalid_argument("Invalid retired MCP identity");const auto name=id.get<std::string>();identity(name);if(!retired.insert(name).second || old.contains(name))throw std::invalid_argument("Invalid retired MCP identity");}
    Json saved=Json::array();
    for(auto& value:values){if(retired.contains(value.id))throw Conflict("MCP configuration identity is retired");active.insert(value.id);const auto found=old.find(value.id);value.revision=1;
        if(found!=old.end()){value.revision=found->second.revision;if(encode(value,false)!=encode(found->second,false)){if(value.revision==std::numeric_limits<std::int64_t>::max())throw Conflict("MCP configuration revision exhausted");++value.revision;}}
        saved.push_back(encode(value));}
    for(const auto& [id,value]:old){(void)value;if(!active.contains(id))retired.insert(id);}if(retired.size()>4096)throw Conflict("Retired MCP configuration limit reached");
    const auto encoded=Json{{"version",1},{"servers",saved},{"retired_ids",retired}}.dump();if(encoded.size()>256*1024)throw Conflict("Stored MCP configuration exceeds limits");
    store_.put_information("native-mcp","servers",encoded).get();return values;
}
std::string mcp_server_setting_json(const McpServerSetting& value){const auto encoded=encode(value);(void)setting(encoded,true);return encoded.dump();}
std::string mcp_credential_purpose(const McpServerSetting& setting,const std::string& name) {
    std::string source;
    if(setting.transport=="http") {
        validate_mcp_http_endpoint(setting.endpoint);
        if(name=="OAUTH"){
            if(!setting.oauth||setting.bearer||setting.oauth->scope!="server"||setting.oauth->id.empty()||setting.oauth->id.size()>256||setting.oauth->issuer.empty()||setting.oauth->issuer.size()>8192||setting.oauth->client_id.empty()||setting.oauth->client_id.size()>2048)throw std::invalid_argument("Invalid MCP OAuth credential binding");
            validate_mcp_http_endpoint(setting.oauth->issuer);
            const auto secure=[](std::string url){for(auto& c:url)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return url.starts_with("https://");};
            if(!secure(setting.endpoint)||!secure(setting.oauth->issuer)||setting.oauth->issuer.find('?')!=std::string::npos)throw std::invalid_argument("MCP OAuth requires HTTPS resource and issuer");
            for(unsigned char c:setting.oauth->client_id)if(c<0x20||c==0x7f)throw std::invalid_argument("Invalid MCP OAuth client identity");
            validate_mcp_oauth_loopback_path(setting.oauth->callback.path);
            identity(setting.id);
            Json binding{{"id",setting.id},{"transport","http"},{"resource",setting.endpoint},{"issuer",setting.oauth->issuer},{"client_id",setting.oauth->client_id}};
            if(setting.oauth->callback.path!="/oauth/callback"||setting.oauth->callback.port)binding["callback"]={{"path",setting.oauth->callback.path},{"port",setting.oauth->callback.port}};
            source=binding.dump();
        }else if(name=="BEARER"&&!setting.oauth)source=Json{{"id",setting.id},{"transport","http"},{"endpoint",setting.endpoint},{"header","Authorization: Bearer"}}.dump();
        else throw std::invalid_argument("Invalid HTTP MCP credential target");
    }else if(setting.transport=="stdio")source=Json{{"id",setting.id},{"executable",setting.executable},{"directory",setting.working_directory},{"arguments",setting.arguments},{"environment",environment_name(name)}}.dump();
    else throw std::invalid_argument("Unsupported MCP credential transport");
    BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<UCHAR,32> bytes{};if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot bind MCP credential purpose");
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(source.data())),static_cast<ULONG>(source.size()),bytes.data(),static_cast<ULONG>(bytes.size()));BCryptCloseAlgorithmProvider(algorithm,0);if(status<0)throw std::runtime_error("Cannot bind MCP credential purpose");
    std::ostringstream value;value<<(setting.transport=="http"?(name=="OAUTH"?"mcp-http-oauth:":"mcp-http-bearer:"):"mcp-environment:")<<std::hex<<std::setfill('0');for(const auto byte:bytes)value<<std::setw(2)<<static_cast<unsigned>(byte);return value.str();
}
}
