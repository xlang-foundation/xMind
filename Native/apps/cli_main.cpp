#include "agentflow/console_transport.hpp"
#include "agentflow/program_entries.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <thread>
#include <limits>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <set>
#include <vector>
#include <algorithm>
#include <string_view>
#include <array>
#include <random>
#include <map>
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include "agentflow/mcp_http_transport.hpp"
#endif

namespace {
#if defined(_WIN32)
std::string utf8_argument(std::wstring_view value){
    if(value.empty())return {};if(value.size()>32768)throw std::invalid_argument("Unicode CLI text exceeds limits");const auto length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(length<1)throw std::invalid_argument("Invalid Unicode CLI text");std::string result(static_cast<std::size_t>(length),'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),length,nullptr,nullptr)!=length)throw std::invalid_argument("Invalid Unicode CLI text");return result;
}
#endif
std::string provider_profile_identity(const std::string& value){
    if(value.empty()||value.size()>256||value.starts_with("sk-")||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)
        throw std::invalid_argument("Invalid provider profile identity");
    return value;
}
 nlohmann::json provider_admission_binding(const nlohmann::json& metadata){
    using Json=nlohmann::json;
    if(!metadata.is_object()||!metadata.contains("revision")||!metadata["revision"].is_number_integer()||metadata["revision"]<0||metadata["revision"]>9007199254740991||!metadata.contains("active")||!metadata["active"].is_string()||!metadata.contains("profiles")||!metadata["profiles"].is_array())throw std::runtime_error("Invalid backend provider admission metadata");
    const auto id=metadata["active"].get<std::string>();if(id.size()>256||id.starts_with("sk-")||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)throw std::runtime_error("Invalid backend provider profile identity");
    bool exists=id.empty();for(const auto& profile:metadata["profiles"])if(profile.is_object()&&profile.value("id",std::string{})==id)exists=true;if(!exists)throw std::runtime_error("Active backend provider profile is missing");
    return Json{{"provider_profile_id",id},{"expected_provider_revision",metadata["revision"]}};
 }
std::int64_t event_cursor(const std::string& source) {
    std::int64_t value=0;const auto parsed=std::from_chars(source.data(),source.data()+source.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=source.data()+source.size() || value<0)throw std::invalid_argument("Invalid cursor");
    return value;
}
std::int64_t provider_revision(const std::string& source){
    const auto value=event_cursor(source);if(value>9007199254740991)throw std::invalid_argument("Invalid provider revision");return value;
}
std::int64_t plan_revision(const std::string& source){const auto value=provider_revision(source);if(value<1)throw std::invalid_argument("Plan revision and state sequence must be positive");return value;}
std::string mcp_identity(const std::string& value,bool server=false){
    const std::string_view allowed=server?"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-":"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    if(value.empty()||value.size()>(server?64:128)||value.find_first_not_of(allowed)!=std::string::npos)throw std::invalid_argument("Invalid MCP sign-in identity");return value;
}
void mcp_fields(const nlohmann::json& value,std::initializer_list<const char*> fields){if(!value.is_object()||value.size()!=fields.size())throw std::runtime_error("Invalid MCP sign-in metadata");for(const auto* field:fields)if(!value.contains(field))throw std::runtime_error("Invalid MCP sign-in metadata");}
std::int64_t mcp_integer(const nlohmann::json& value,std::int64_t minimum=0){if(!value.is_number_integer()||value<minimum||value>9007199254740991LL)throw std::runtime_error("Invalid MCP sign-in revision or expiry");return value.get<std::int64_t>();}
nlohmann::json mcp_parse(const std::string& source){
    if(source.empty()||source.size()>65536)throw std::runtime_error("Invalid MCP sign-in metadata");
    try{std::vector<std::set<std::string>> keys;return nlohmann::json::parse(source,[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json& parsed){if(depth>6)throw std::runtime_error("Invalid metadata depth");if(event==nlohmann::json::parse_event_t::object_start)keys.emplace_back();else if(event==nlohmann::json::parse_event_t::object_end)keys.pop_back();else if(event==nlohmann::json::parse_event_t::key&&!keys.back().insert(parsed.get<std::string>()).second)throw std::runtime_error("Duplicate metadata field");return true;});}catch(...){throw std::runtime_error("Invalid MCP sign-in metadata");}
}
void mcp_https_link(const std::string& value){
    auto scheme=value.substr(0,8);for(auto& c:scheme)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    if(scheme!="https://"||value.size()>16384)throw std::runtime_error("Invalid MCP sign-in URL");for(unsigned char c:value)if(c<=32||c==127||c=='#'||c=='\\')throw std::runtime_error("Invalid MCP sign-in URL");
#if defined(_WIN32)
    try{agentflow::validate_mcp_http_endpoint(value.substr(0,value.find('?')));}catch(...){throw std::runtime_error("Invalid MCP sign-in URL");}
#else
    const auto end=value.find_first_of("/?",8);const auto authority=value.substr(8,end==std::string::npos?end:end-8);if(authority.empty()||authority.find('@')!=std::string::npos)throw std::runtime_error("Invalid MCP sign-in URL");
#endif
}
bool mcp_terminal(const std::string& state){return state=="connected"||state=="failed"||state=="denied"||state=="cancelled"||state=="expired";}
nlohmann::json mcp_servers(const nlohmann::json& value){
    mcp_fields(value,{"servers"});if(!value["servers"].is_array()||value["servers"].size()>16)throw std::runtime_error("Invalid MCP server catalogue");std::set<std::string> ids;
    for(const auto& server:value["servers"]){mcp_fields(server,{"id","config_revision","credential_revision","enabled","configured","state","expires_unix_ms"});if(!server["id"].is_string()||!server["state"].is_string()||!server["enabled"].is_boolean()||!server["configured"].is_boolean())throw std::runtime_error("Invalid MCP server catalogue");const auto id=mcp_identity(server["id"].get<std::string>(),true);if(!ids.insert(id).second)throw std::runtime_error("Duplicate MCP server identity");mcp_integer(server["config_revision"],1);mcp_integer(server["credential_revision"]);if(!server["expires_unix_ms"].is_null())mcp_integer(server["expires_unix_ms"],1);const auto state=server["state"].get<std::string>();if(state!="not_configured"&&state!="disabled"&&state!="authorized"&&state!="needs_login"&&state!="unavailable")throw std::runtime_error("Invalid MCP server state");}return value;
}
nlohmann::json mcp_attempt(const nlohmann::json& value,const nlohmann::json& binding=nlohmann::json::object()){
    mcp_fields(value,{"id","server_id","state","config_revision","credential_revision","authorization_url","expires_unix_ms","reason","cancellation_requested"});for(const auto* field:{"id","server_id","state"})if(!value[field].is_string())throw std::runtime_error("Invalid MCP sign-in attempt");mcp_identity(value["id"].get<std::string>());mcp_identity(value["server_id"].get<std::string>(),true);mcp_integer(value["config_revision"],1);mcp_integer(value["credential_revision"]);mcp_integer(value["expires_unix_ms"],1);if(!value["cancellation_requested"].is_boolean())throw std::runtime_error("Invalid MCP sign-in attempt");
    const auto state=value["state"].get<std::string>();if(!mcp_terminal(state)&&state!="discovering"&&state!="awaiting_callback"&&state!="exchanging")throw std::runtime_error("Invalid MCP sign-in state");
    if(state=="connected")mcp_integer(value["credential_revision"],1);
    for(const auto* field:{"id","server_id","config_revision"})if(binding.contains(field)&&binding[field]!=value[field])throw std::runtime_error("MCP sign-in ownership changed");
    if(binding.contains("credential_revision")&&(state=="connected"?value["credential_revision"]<=binding["credential_revision"]:value["credential_revision"]!=binding["credential_revision"]))throw std::runtime_error("MCP sign-in credential revision changed");
    const std::map<std::string,std::set<std::string>> reasons={{"failed",{"backend_quiesced","configuration_or_credential_changed","protocol_rejected","authorization_failed","refresh_uncertain","refresh_recovery_required"}},{"denied",{"authorization_failed","access_denied","interaction_required","login_required","consent_required","temporarily_unavailable","server_error","invalid_request","unauthorized_client","unsupported_response_type","invalid_scope"}},{"cancelled",{"cancelled"}},{"expired",{"deadline_exceeded"}}};const auto reason=reasons.find(state);if(reason==reasons.end()){if(!value["reason"].is_null())throw std::runtime_error("Invalid MCP sign-in reason");}else if(!value["reason"].is_string()||!reason->second.contains(value["reason"].get<std::string>()))throw std::runtime_error("Invalid MCP sign-in reason");
    if(state=="awaiting_callback"){if(!value["authorization_url"].is_string())throw std::runtime_error("Invalid MCP sign-in URL");mcp_https_link(value["authorization_url"].get<std::string>());}else if(!value["authorization_url"].is_null())throw std::runtime_error("Unexpected MCP sign-in URL");return value;
}
nlohmann::json mcp_request(agentflow::ConsoleTransport& client,const httplib::Headers& headers,const std::string& path,const nlohmann::json* body=nullptr){
    auto response=body?client.Post(path,headers,body->dump(),"application/json"):client.Get(path,headers);if(!response)throw std::runtime_error("MCP sign-in reply unavailable. Inspect the recorded request ID; no automatic retry.");if(response->status!=(body?202:200))throw std::runtime_error("Backend rejected MCP sign-in request (HTTP "+std::to_string(response->status)+"). No automatic retry.");return mcp_parse(response->body);
}
std::string mcp_request_id(){
    std::array<unsigned char,32> bytes{};
#if defined(_WIN32)
    if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot generate MCP sign-in request identity");
#else
    std::random_device random;for(auto& byte:bytes)byte=static_cast<unsigned char>(random());
#endif
    constexpr char hex[]="0123456789abcdef";std::string value;for(auto byte:bytes){value+=hex[byte>>4];value+=hex[byte&15];}return value;
}
nlohmann::json mcp_command(agentflow::ConsoleTransport& client,const httplib::Headers& headers,const std::string& command,const std::string& identity){
    const std::string root="/v1/mcp/authorization";
    if(command=="mcp-auth")return mcp_servers(mcp_request(client,headers,root+"/servers"));
    if(command=="mcp-login"||command=="mcp-refresh"){
        const bool renewal=command=="mcp-refresh";
        mcp_identity(identity,true);const auto catalogue=mcp_servers(mcp_request(client,headers,root+"/servers"));const auto& servers=catalogue["servers"];const auto found=std::find_if(servers.begin(),servers.end(),[&](const auto& server){return server["id"]==identity;});if(found==servers.end()||!(*found)["enabled"].get<bool>()||!(*found)["configured"].get<bool>()||(renewal?((*found)["credential_revision"].get<std::int64_t>()<1||((*found)["state"]!="needs_login"&&(*found)["state"]!="authorized")):(*found)["state"]!="needs_login"))throw std::runtime_error(renewal?"Select a configured MCP server with an existing grant; inspect mcp-auth.":"Select a configured MCP server requiring sign-in; inspect mcp-auth.");
        const auto id=mcp_request_id();const nlohmann::json binding={{"id",id},{"server_id",identity},{"config_revision",(*found)["config_revision"]},{"credential_revision",(*found)["credential_revision"]}},body={{"request_id",id},{"server_id",identity},{"expected_config_revision",binding["config_revision"]},{"expected_credential_revision",binding["credential_revision"]}};
        std::cerr<<"MCP "<<(renewal?"renewal":"login")<<" request "<<id<<" for "<<identity<<". If the reply is lost, use mcp-login-status "<<id<<"; do not restart automatically.\n"<<std::flush;if(!std::cerr)throw std::runtime_error("Cannot publish MCP observation identity; no request sent");return mcp_attempt(mcp_request(client,headers,root+(renewal?"/renewals":"/attempts"),&body),binding);
    }
    mcp_identity(identity);auto value=mcp_attempt(mcp_request(client,headers,root+"/attempts/"+identity),{{"id",identity}});
    if(command=="mcp-login-cancel"&&!mcp_terminal(value["state"].get<std::string>())){const nlohmann::json body=nlohmann::json::object(),binding={{"id",identity},{"server_id",value["server_id"]},{"config_revision",value["config_revision"]},{"credential_revision",value["credential_revision"]}};value=mcp_attempt(mcp_request(client,headers,root+"/attempts/"+identity+"/cancel",&body),binding);}
    if(command=="mcp-login-open"){
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();if(value["state"]!="awaiting_callback"||value["cancellation_requested"].get<bool>()||mcp_integer(value["expires_unix_ms"],1)<=now)throw std::runtime_error("No current MCP sign-in page; inspect mcp-login-status.");
#if defined(_WIN32)
        const auto url=value["authorization_url"].get<std::string>();const auto length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),static_cast<int>(url.size()),nullptr,0);if(length<1)throw std::runtime_error("Invalid MCP sign-in URL");std::wstring wide(length,L'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),static_cast<int>(url.size()),wide.data(),length)!=length)throw std::runtime_error("Invalid MCP sign-in URL");if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",wide.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error("Cannot open the MCP sign-in page; use the validated HTTPS URL from status.");
#else
        throw std::runtime_error("Opening a sign-in page is unavailable on this platform; use its validated HTTPS URL.");
#endif
    }
    return value;
}
int mcp_watch(agentflow::ConsoleTransport& client,const httplib::Headers& headers,const std::string& id){
    mcp_identity(id);nlohmann::json binding=nlohmann::json::object();std::string previous;const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(6);
    do{const auto value=mcp_attempt(mcp_request(client,headers,"/v1/mcp/authorization/attempts/"+id),binding.empty()?nlohmann::json{{"id",id}}:binding);const auto state=value["state"].get<std::string>(),encoded=value.dump();if(encoded!=previous){std::cout<<encoded<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("MCP observation output unavailable; backend sign-in continues");previous=encoded;}if(mcp_terminal(state))return state=="connected"?0:1;if(binding.empty())binding={{"id",id},{"server_id",value["server_id"]},{"config_revision",value["config_revision"]},{"credential_revision",value["credential_revision"]}};std::this_thread::sleep_for(std::chrono::milliseconds(500));}while(std::chrono::steady_clock::now()<deadline);throw std::runtime_error("MCP observation detached at its deadline; backend sign-in was not cancelled");
}
void skill_ids(const nlohmann::json& value){
    if(!value.is_array()||value.size()>8)throw std::invalid_argument("Select at most eight skills");std::set<std::string> unique;
    for(const auto& item:value){if(!item.is_string())throw std::invalid_argument("Skill ids must be strings");const auto& id=item.get_ref<const std::string&>();if(id.empty()||id.size()>256||id=="."||id==".."||id.find_first_of("/\\:")!=std::string::npos||id.find('\0')!=std::string::npos||!unique.insert(id).second)throw std::invalid_argument("Invalid or duplicate skill id");}
}
void skill_snapshot(const nlohmann::json& value,const std::string& session){
    if(!value.is_object()||value.size()!=7||value.value("session_id",std::string{})!=session||!value.contains("workspace_id")||!value["workspace_id"].is_string()||!value.contains("authority_id")||!value["authority_id"].is_string()||!value.contains("revision")||!value["revision"].is_number_integer()||value["revision"]<0||value["revision"]>9007199254740991LL||!value.contains("editable")||!value["editable"].is_boolean()||!value.contains("ids")||!value.contains("manual_ids"))throw std::invalid_argument("Invalid session skill snapshot");
    const auto workspace=value["workspace_id"].get<std::string>(),authority=value["authority_id"].get<std::string>();if(workspace.empty()||workspace.size()>256||workspace.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos||authority.size()!=32||authority.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("Invalid skill workspace binding");
    skill_ids(value["ids"]);skill_ids(value["manual_ids"]);for(const auto& id:value["manual_ids"])if(std::find(value["ids"].begin(),value["ids"].end(),id)==value["ids"].end())throw std::invalid_argument("Invalid skill attachment provenance");
}
nlohmann::json skill_change(const nlohmann::json& observed,const std::string& session,const nlohmann::json& ids){
    skill_snapshot(observed,session);skill_ids(ids);if(observed["revision"]>=9007199254740991LL)throw std::invalid_argument("Session skill revision cannot advance");if(!observed["editable"].get<bool>())throw std::invalid_argument("Wait for this conversation to become idle before changing skills");
    return {{"ids",ids},{"expected_revision",observed["revision"]},{"expected_workspace_id",observed["workspace_id"]},{"expected_workspace_authority_id",observed["authority_id"]}};
}
void skill_acknowledgement(const nlohmann::json& value,const std::string& session,const nlohmann::json& request){
    skill_snapshot(value,session);if(value["workspace_id"]!=request["expected_workspace_id"]||value["authority_id"]!=request["expected_workspace_authority_id"]||value["revision"].get<std::int64_t>()!=request["expected_revision"].get<std::int64_t>()+1||value["ids"].size()!=request["ids"].size()||value["manual_ids"].size()!=request["ids"].size())throw std::runtime_error("Skill acknowledgement changed binding or selection");
    for(const auto& id:request["ids"])if(std::find(value["ids"].begin(),value["ids"].end(),id)==value["ids"].end()||std::find(value["manual_ids"].begin(),value["manual_ids"].end(),id)==value["manual_ids"].end())throw std::runtime_error("Skill acknowledgement changed explicit attachment");
}
nlohmann::json read_skill_snapshot(const std::string& filename){
    std::ifstream file(std::filesystem::u8path(filename),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read session skill snapshot file");std::string source;char byte;while(file.get(byte)){if(source.size()>=8192)throw std::invalid_argument("Session skill snapshot exceeds 8 KiB");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read session skill snapshot file");
    try{
#if defined(_WIN32)
        if(source.size()>=2&&static_cast<unsigned char>(source[0])==255&&static_cast<unsigned char>(source[1])==254){if(source.size()%2)throw std::invalid_argument("Invalid UTF-16 snapshot");std::wstring wide;for(std::size_t i=2;i<source.size();i+=2)wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(source[i])|(static_cast<unsigned char>(source[i+1])<<8)));source=utf8_argument(wide);if(source.size()>8192)throw std::invalid_argument("Snapshot exceeds limits");}
#endif
        std::vector<std::set<std::string>> fields;return nlohmann::json::parse(source,[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json& value){if(depth>4)throw std::invalid_argument("Invalid skill snapshot nesting");if(event==nlohmann::json::parse_event_t::object_start)fields.emplace_back();else if(event==nlohmann::json::parse_event_t::object_end)fields.pop_back();else if(event==nlohmann::json::parse_event_t::key&&!fields.back().insert(value.get<std::string>()).second)throw std::invalid_argument("Duplicate skill snapshot field");return true;});}catch(const std::exception&){throw std::invalid_argument("Invalid session skill snapshot JSON");}
}
void validate_plan_input(const std::string& source){
    if(source.empty()||source.size()>16384)throw std::invalid_argument("Plan input must contain at most 16384 bytes");
    using Json=nlohmann::json;std::vector<std::set<std::string>> objects;
    const auto parsed=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>64)throw std::invalid_argument("Plan input nesting exceeds limits");
        if(event==Json::parse_event_t::object_start)objects.emplace_back();
        else if(event==Json::parse_event_t::object_end)objects.pop_back();
        else if(event==Json::parse_event_t::key&&!objects.back().insert(value.get<std::string>()).second)throw std::invalid_argument("Duplicate plan input field");return true;
    });if(!parsed.is_object())throw std::invalid_argument("Plan input must be a JSON object");
}
void observed_dynamic_child(const nlohmann::json& record){
    const auto kind=record.value("kind",std::string{});
    if(kind=="delegated_leaf"){if(record.value("batch_id",std::string{}).empty()||record.value("task_id",std::string{}).empty()||record.value("preset_id",std::string{})!="workspace.inspect")throw std::runtime_error("Invalid delegated child metadata");}
    else if(kind=="dynamic_agent"){
        for(const auto* key:{"plan_id","claim_id"}){const auto value=record.value(key,std::string{});if(value.empty()||value.size()>128||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid dynamic child identity");}
        const auto label=record.value("node_label",std::string{}),preset=record.value("preset_id",std::string{});
        if(label.empty()||label.size()>32||label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||(preset!="workspace.inspect"&&preset!="workspace.coding")||!record.contains("definition_revision")||!record["definition_revision"].is_number_integer()||record["definition_revision"]<1||record["definition_revision"]>9007199254740991||!record.contains("claim_revision")||record["claim_revision"]!=record["definition_revision"]||record.value("batch_id",std::string{})!=""||record.value("task_id",std::string{})!=""||record.at("run").value("node_id",std::string{}).empty())throw std::runtime_error("Invalid dynamic child claim");
    }else throw std::runtime_error("Invalid owned child kind");
    if(!record.contains("preset_revision")||!record["preset_revision"].is_number_integer()||record["preset_revision"]<1||record["preset_revision"]>9007199254740991)throw std::runtime_error("Invalid child preset revision");
}
nlohmann::json active_profile_discovery(const nlohmann::json& metadata){
    using Json=nlohmann::json;const auto binding=provider_admission_binding(metadata);const auto id=binding.at("provider_profile_id").get<std::string>();
    if(id.empty())throw std::runtime_error("Select a saved provider profile before discovering account models");
    const auto& profiles=metadata.at("profiles");const auto active=std::find_if(profiles.begin(),profiles.end(),[&](const Json& profile){return profile.is_object()&&profile.value("id",std::string{})==id;});
    if(active==profiles.end()||!active->contains("route_id")||!(*active)["route_id"].is_string())throw std::runtime_error("Invalid backend provider discovery metadata");
    return Json{{"id",provider_profile_identity(id)},{"route_id",provider_profile_identity((*active)["route_id"].get<std::string>())},{"expected_revision",binding.at("expected_provider_revision")}};
}
nlohmann::json provider_key_fields(const std::string& variable,const std::string& revision_text) {
    auto normalized=variable;for(auto& character:normalized)if(character>='a' && character<='z')character=static_cast<char>(character-'a'+'A');
    if(variable.empty() || variable.size()>128 || variable.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos || (variable.front()>='0' && variable.front()<='9') || normalized=="XMIND_AUTH_TOKEN" || normalized.starts_with("XMIND_UI_"))throw std::invalid_argument("Select a provider key environment variable");
    const auto revision=provider_revision(revision_text);
    const auto* secret=std::getenv(variable.c_str());if(!secret || !*secret)throw std::invalid_argument("Provider key environment variable is empty");const auto length=std::strlen(secret);if(length>32768)throw std::invalid_argument("Provider key exceeds limits");
    nlohmann::json fields={{"api_key",std::string(secret,length)},{"expected_revision",revision}};
#if defined(_WIN32)
    _putenv_s(variable.c_str(),""); // Only this client process; preserve parent/user settings.
#endif
    return fields;
}
// Native catalogue discovery has a bounded 30-second provider deadline. Keep
// its client wait large enough, without extending later chat/watch requests.
struct ProviderDiscoveryTimeout {
    agentflow::ConsoleTransport& client;
    explicit ProviderDiscoveryTimeout(agentflow::ConsoleTransport& value):client(value){client.set_read_timeout(35,0);}
    ~ProviderDiscoveryTimeout(){client.set_read_timeout(15,0);}
    ProviderDiscoveryTimeout(const ProviderDiscoveryTimeout&)=delete;
    ProviderDiscoveryTimeout& operator=(const ProviderDiscoveryTimeout&)=delete;
};
auto provider_discovery_request(agentflow::ConsoleTransport& client,const httplib::Headers& headers,const std::string& path,const nlohmann::json& body){
    ProviderDiscoveryTimeout timeout(client);return client.Post(path,headers,body.dump(),"application/json");
}
nlohmann::json provider_rejection(const std::string& source){
    using Json=nlohmann::json;
    const Json unavailable={{"detail","Backend returned invalid provider diagnostics"}};
    if(source.empty()||source.size()>16384)return unavailable;
    try{
        std::vector<std::set<std::string>> fields;
        const auto value=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& parsed){
            if(depth>8)throw std::invalid_argument("Invalid provider diagnostic depth");
            if(event==Json::parse_event_t::object_start)fields.emplace_back();
            else if(event==Json::parse_event_t::object_end)fields.pop_back();
            else if(event==Json::parse_event_t::key&&!fields.back().insert(parsed.get<std::string>()).second)throw std::invalid_argument("Duplicate provider diagnostic");
            return true;
        });
        if(!value.is_object())return unavailable;Json result=Json::object();
        // The native server already sanitizes provider bodies and credentials.
        // Retain only its public diagnostic fields, never request/raw-body data.
        for(const auto* name:{"detail","provider_error_type","provider_error_code","provider_error_param"})if(value.contains(name)){
            if(!value[name].is_string()||value[name].get_ref<const std::string&>().size()>(std::string_view(name)=="detail"?4096:256))return unavailable;
            result[name]=value[name];
        }
        if(value.contains("provider_status")){
            if(!value["provider_status"].is_number_integer()||value["provider_status"]<100||value["provider_status"]>599)return unavailable;
            result["provider_status"]=value["provider_status"];
        }
        return result.empty()?unavailable:result;
    }catch(...){return unavailable;}
}
// Observation only: ending this client never grants, cancels or owns execution.
// Each flushed NDJSON record is an actual persisted backend event. Its seq can
// be supplied on reconnect; process-output hex is never written as terminal code.
int watch_run(agentflow::ConsoleTransport& client,const httplib::Headers& headers,const std::string& run,std::int64_t cursor,bool graph=false,bool interactive=false) {
    using Json=nlohmann::json;const auto path="/v1/runs/"+run;
    auto read=[&](const std::string& route) {
        auto response=client.Get(route,headers);if(!response)throw std::runtime_error("Cannot reach xMind Server during observation");
        if(response->status<200 || response->status>=300)throw std::runtime_error("Server rejected run observation (HTTP "+std::to_string(response->status)+")");
        return Json::parse(response->body);
    };
    std::string session;bool tree=false,plan_observation=false;
    const auto initial=read(path);if(!initial.is_object()||initial.value("id",std::string{})!=run||!initial.contains("session_id")||!initial["session_id"].is_string()||initial["session_id"].get<std::string>().empty())throw std::runtime_error("Invalid observed run identity");session=initial["session_id"].get<std::string>();
    if(graph&&initial.value("graph_root",false)!=true)throw std::runtime_error("Select a graph root for graph observation");
    if(!graph&&initial.value("parent_id",std::string{}).empty()&&!initial.value("graph_root",false)){const auto health=read("/v1/health");tree=health.value("owned_child_observation",false);plan_observation=health.contains("agent_planning")&&health["agent_planning"].is_boolean();}
    auto owners=[&] {
        std::set<std::string> owned{run};
        if(graph){
            const auto children=read("/v1/graph-runs/"+run+"/children");
            if(!children.is_array())throw std::runtime_error("Invalid graph child batch");
            for(const auto& child:children){const auto id=child.value("id",std::string{});if(!child.is_object() || child.value("parent_id",std::string{})!=run || child.value("session_id",std::string{})!=session || id.empty() || id.size()>128 || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos || !owned.insert(id).second)throw std::runtime_error("Invalid graph child ownership");}
        }
        if(tree){const auto children=read(path+"/children");if(!children.is_array()||children.size()>8)throw std::runtime_error("Invalid owned child batch");for(const auto& record:children){if(!record.is_object()||!record.contains("run")||!record["run"].is_object())throw std::runtime_error("Invalid owned child metadata");observed_dynamic_child(record);const auto& child=record["run"];const auto id=child.value("id",std::string{});if(child.value("parent_id",std::string{})!=run||child.value("session_id",std::string{})!=session||child.value("graph_root",false)||id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos||!owned.insert(id).second)throw std::runtime_error("Invalid owned child identity");}}
        return owned;
    };
    auto emit=[&] {
        for(;;){const auto events=read((graph?"/v1/graph-runs/"+run:path)+(tree?"/tree-events?after=":"/events?after=")+std::to_string(cursor));
        if(!events.is_array())throw std::runtime_error("Invalid backend event batch");
        if(tree&&events.size()>256)throw std::runtime_error("Invalid tree event page");
        // Ownership is read after events so newly admitted children are covered.
        const auto owned=owners();
        for(const auto& event:events) {
            if(!event.is_object() || !event.contains("seq") || !event["seq"].is_number_integer() || event["seq"]<=cursor || event["seq"]>std::numeric_limits<std::int64_t>::max() || !event.contains("run_id") || !event["run_id"].is_string() || !owned.contains(event["run_id"].get<std::string>()) || !event.contains("kind") || !event["kind"].is_string() || !event.contains("data"))throw std::runtime_error("Invalid backend event identity or cursor");
            std::cout<<event.dump()<<'\n'<<std::flush;
            if(!std::cout)throw std::runtime_error("Run observation output is unavailable");
            cursor=event["seq"].get<std::int64_t>();
        }
        if(!tree||events.size()<256)break;}
    };
    for(;;) {
        emit();const auto state=read(path);
        if(!state.is_object() || state.value("id",std::string{})!=run || !state.contains("state") || !state["state"].is_string())throw std::runtime_error("Invalid backend run identity or state");
        if(graph && (state.value("graph_root",false)!=true || state.value("session_id",std::string{})!=session))throw std::runtime_error("Graph observation identity changed");
        const auto value=state["state"].get<std::string>();
        if(value=="completed" || value=="failed" || value=="cancelled") {
            // A transition can commit between the event query and status query.
            emit();return value=="completed"?0:value=="cancelled"?2:1;
        }
        if(value!="queued" && value!="running" && value!="paused")throw std::runtime_error("Unknown backend run state");
        if(interactive){
            auto operations=Json::array();const auto owned=owners();
            for(const auto& owner:owned){const auto batch=read("/v1/runs/"+owner+"/operations");if(!batch.is_array())throw std::runtime_error("Invalid backend operation list");for(const auto& operation:batch){if(!operation.is_object()||operation.value("run_id",std::string{})!=owner)throw std::runtime_error("Invalid operation ownership");operations.push_back(operation);}}
            bool interacted=false;
            for(const auto& operation:operations){
                if(!operation.is_object() || !owned.contains(operation.value("run_id",std::string{})))throw std::runtime_error("Invalid operation ownership");
                if(operation.value("state",std::string{})!="awaiting_approval")continue;
                const auto operationId=operation.value("id",std::string{});
                if(operationId.empty() || operationId.size()>128 || operationId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid approval identity");
                // JSON escaping keeps file/process content inert on the terminal.
                // The native backend retains the exact proposal and revalidates
                // authority, expiry and effect preconditions on the decision.
                std::cout<<Json{{"type","operation_review"},{"operation",operation}}.dump()<<'\n'<<std::flush;
                if(!std::cout)throw std::runtime_error("Approval review output is unavailable");
                std::cerr<<"Review the exact operation above. Enter /allow "<<operationId<<", /deny "<<operationId<<", /cancel or /exit to detach.\n";
                std::string decision;
                if(!std::getline(std::cin,decision))throw std::runtime_error("CLI detached before a decision; backend execution remains owned by the server");
                if(!decision.empty() && decision.back()=='\r')decision.pop_back();
                if(decision=="/exit")throw std::runtime_error("CLI detached before a decision; backend execution remains owned by the server");
                std::string route;Json body;
                if(decision=="/cancel"){route=path+"/cancel";body=Json::object();}
                else if(decision=="/allow "+operationId || decision=="/deny "+operationId){route="/v1/operations/"+operationId+"/decision";body={{"decision",decision.starts_with("/allow ")?"allow":"deny"}};}
                else {std::cerr<<"No decision sent. Use an exact displayed operation ID.\n";break;}
                const auto response=client.Post(route,headers,body.dump(),"application/json");
                if(!response)throw std::runtime_error("Cannot reach xMind Server for approval");
                if(response->status<200 || response->status>=300)std::cerr<<"Backend rejected the decision (HTTP "<<response->status<<"). Refreshing recorded state.\n";
                else std::cout<<Json{{"type",decision=="/cancel"?"run_cancel_result":"operation_decision_result"},{"result",Json::parse(response->body)}}.dump()<<'\n'<<std::flush;
                interacted=true;
                break; // Re-read state before reviewing another operation.
            }
            if(interacted)continue;
            if(plan_observation){
                const auto response=client.Get(path+"/plan",headers);if(!response)throw std::runtime_error("Cannot inspect the current native plan");
                if(response->status==409){std::this_thread::sleep_for(std::chrono::milliseconds(250));continue;}
                if(response->status!=200)throw std::runtime_error("Server rejected plan inspection");
                const auto snapshot=Json::parse(response->body);
                if(!snapshot.is_object()||snapshot.at("run").value("id",std::string{})!=run||snapshot.at("run").value("session_id",std::string{})!=session||snapshot.at("run").value("parent_id",std::string{})!=""||snapshot.at("run").value("graph_root",false)||!snapshot.at("questions").is_array())throw std::runtime_error("Plan observation ownership changed");
                if(!snapshot.at("plan").is_null()){
                    const auto& plan=snapshot.at("plan");const auto revision=plan_revision(plan.at("revision").dump()),sequence=plan_revision(plan.at("state_sequence").dump());
                    if(plan.value("root_run_id",std::string{})!=run||!plan.at("ready").is_array()||!plan.at("claimed").is_array()||!plan.at("waiting_human").is_array()||!plan.at("report_ready").is_boolean()||!plan.at("halted").is_boolean()||snapshot["questions"].size()>8)throw std::runtime_error("Invalid current plan readiness");
                    auto questions=Json::array();std::set<std::string> waiting;
                    for(const auto& question:snapshot["questions"]){if(question.value("state",std::string{})!="waiting")continue;const auto id=question.value("id",std::string{});
                        if(id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos||question.value("root_run_id",std::string{})!=run||question.value("plan_id",std::string{})!=plan.value("id",std::string{})||!question.contains("question")||!question["question"].is_string()||!waiting.insert(id).second)throw std::runtime_error("Invalid plan human ownership");questions.push_back(question);
                    }
                    const bool resume=snapshot["run"].value("state",std::string{})=="paused"&&waiting.empty()&&plan["claimed"].empty()&&plan["waiting_human"].empty()&&!plan["halted"].get<bool>()&&(!plan["ready"].empty()||plan["report_ready"].get<bool>());
                    if(!questions.empty()||resume){
                        std::cout<<Json{{"type","plan_human_review"},{"snapshot",snapshot}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Plan review output is unavailable");
                        std::cerr<<"Enter /input REQUEST_ID JSON for a displayed question"<<(resume?", /resume":"")<<", /cancel or /exit to detach.\n";
                        std::string answer;if(!std::getline(std::cin,answer))throw std::runtime_error("CLI detached before plan input; backend execution remains owned by the server");if(!answer.empty()&&answer.back()=='\r')answer.pop_back();if(answer=="/exit")throw std::runtime_error("CLI detached before plan input; backend execution remains owned by the server");
                        std::string route;Json input;
                        if(answer=="/cancel"){route=path+"/cancel";input=Json::object();}
                        else if(answer=="/resume"&&resume){route=path+"/plan/resume";input={{"expected_revision",revision},{"expected_state_sequence",sequence}};}
                        else if(answer.starts_with("/input ")){const auto split=answer.find(' ',7);const auto request=split==std::string::npos?std::string{}:answer.substr(7,split-7),raw=split==std::string::npos?std::string{}:answer.substr(split+1);if(!waiting.contains(request)){std::cerr<<"No input sent. Use an exact displayed question ID.\n";continue;}try{validate_plan_input(raw);}catch(...){std::cerr<<"No input sent. Use a bounded strict JSON object.\n";continue;}route=path+"/plan/human/"+request;input={{"input_json",raw},{"expected_revision",revision},{"expected_state_sequence",sequence}};}
                        else {std::cerr<<"No input sent. Use a displayed question, /resume when offered, /cancel or /exit.\n";continue;}
                        const auto result=client.Post(route,headers,input.dump(),"application/json");if(!result)throw std::runtime_error("Cannot reach xMind Server for plan input");if(result->status<200||result->status>=300)std::cerr<<"Backend rejected plan input (HTTP "<<result->status<<"). Refreshing the recorded head; no automatic retry.\n";else std::cout<<Json{{"type",answer=="/cancel"?"run_cancel_result":answer=="/resume"?"plan_resume_result":"plan_input_result"},{"result",Json::parse(result->body)}}.dump()<<'\n'<<std::flush;
                        continue;
                    }
                }
            }
            if(graph){
                const auto snapshot=read("/v1/graph-runs/"+run);
                if(!snapshot.is_object()||snapshot.at("run").value("id",std::string{})!=run||snapshot.at("run").value("session_id",std::string{})!=session||!snapshot.contains("checkpoint_revision")||!snapshot["checkpoint_revision"].is_number_integer()||snapshot["checkpoint_revision"]<1||snapshot["checkpoint_revision"]>9007199254740991||!snapshot.at("checkpoint").at("nodes").is_array()||!snapshot.at("spec").at("nodes").is_array())throw std::runtime_error("Invalid graph input snapshot");
                auto steps=Json::array();std::set<std::string> waiting;
                for(const auto& node:snapshot["checkpoint"]["nodes"]){
                    if(node.value("state",std::string{})!="waiting_human")continue;
                    const auto id=node.value("id",std::string{});
                    if(id.empty()||id.size()>64||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||!waiting.insert(id).second)throw std::runtime_error("Invalid graph human identity");
                    const auto& definitions=snapshot["spec"]["nodes"];const auto definition=std::find_if(definitions.begin(),definitions.end(),[&](const Json& value){return value.value("id",std::string{})==id&&value.value("type",std::string{})=="human";});
                    if(definition==definitions.end()||!definition->contains("prompt")||!(*definition)["prompt"].is_string())throw std::runtime_error("Invalid graph human definition");
                    steps.push_back({{"node_id",id},{"prompt",(*definition)["prompt"]}});
                }
                if(!steps.empty()){
                    const auto revision=snapshot["checkpoint_revision"].get<std::int64_t>();
                    std::cout<<Json{{"type","graph_human_review"},{"root_run_id",run},{"checkpoint_revision",revision},{"steps",steps}}.dump()<<'\n'<<std::flush;
                    if(!std::cout)throw std::runtime_error("Graph review output is unavailable");
                    std::cerr<<"Enter /input NODE_ID JSON for a displayed step, /cancel or /exit to detach.\n";
                    std::string answer;if(!std::getline(std::cin,answer))throw std::runtime_error("CLI detached before graph input; backend execution remains owned by the server");if(!answer.empty()&&answer.back()=='\r')answer.pop_back();if(answer=="/exit")throw std::runtime_error("CLI detached before graph input; backend execution remains owned by the server");
                    std::string route;Json body;
                    if(answer=="/cancel"){route=path+"/cancel";body=Json::object();}
                    else if(answer.starts_with("/input ")){const auto split=answer.find(' ',7);const auto node=split==std::string::npos?std::string{}:answer.substr(7,split-7);const auto input=split==std::string::npos?std::string{}:answer.substr(split+1);if(!waiting.contains(node)||input.empty()||input.size()>65536){std::cerr<<"No input sent. Use a displayed node and bounded JSON.\n";continue;}route="/v1/graph-runs/"+run+"/human/"+node;body={{"input_json",input},{"expected_checkpoint_revision",revision}};}
                    else {std::cerr<<"No input sent. Use /input, /cancel or /exit.\n";continue;}
                    const auto response=client.Post(route,headers,body.dump(),"application/json");if(!response)throw std::runtime_error("Cannot reach xMind Server for graph input");if(response->status<200||response->status>=300)std::cerr<<"Backend rejected graph input (HTTP "<<response->status<<"). Refreshing the checkpoint; no automatic retry.\n";else std::cout<<Json{{"type",answer=="/cancel"?"run_cancel_result":"graph_input_result"},{"result",Json::parse(response->body)}}.dump()<<'\n'<<std::flush;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}
// Interactive access client, not a second agent engine. Every request, tool
// result, approval and response remains owned by the shared native server.
int chat_session(agentflow::ConsoleTransport& client,const httplib::Headers& headers,std::string session,std::string model) {
    using Json=nlohmann::json;
    auto request=[&](const std::string& path,const Json* body=nullptr){
        auto response=body?client.Post(path,headers,body->dump(),"application/json"):client.Get(path,headers);
        if(!response)throw std::runtime_error("Cannot reach xMind Server during chat");
        if(response->status<200 || response->status>=300)throw std::runtime_error("Server rejected chat request (HTTP "+std::to_string(response->status)+")");
        return Json::parse(response->body);
    };
    nlohmann::json observed_skills;
    const auto health=request("/v1/health");if(!health.is_object() || !health.contains("agent_execution") || !health["agent_execution"].is_boolean())throw std::runtime_error("Invalid backend chat capabilities");
    const bool profile_admission=health.value("provider_profile_admission",false)||health.value("graph_provider_profile_admission",false);
    auto provider_binding=profile_admission?provider_admission_binding(request("/v1/provider/profiles")):Json::object();
    if(!model.empty()){
        const auto catalogue=request("/v1/models");
        if(!catalogue.is_object() || !catalogue.contains("models") || !catalogue["models"].is_array())throw std::runtime_error("Invalid backend model catalogue");
        bool available=false;
        for(const auto& item:catalogue["models"])if(item.is_object() && item.value("id",std::string{})==model)available=true;
        if(!available)throw std::invalid_argument("Initial model is not enabled by this backend");
    }
    // Validate a supplied session without starting work or creating a duplicate.
    Json history;
    if(!session.empty()){history=request("/v1/sessions/"+session+"/history");if(!history.is_array())throw std::runtime_error("Invalid session history");}
    std::cerr<<"xMind chat: enter a request, /help for commands, /exit to leave. Backend runs survive disconnect.\n";
    if(!session.empty()){
        std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'
                 <<Json{{"type","history"},{"session_id",session},{"history",history}}.dump()<<'\n'<<std::flush;
        if(!std::cout)throw std::runtime_error("Chat history output is unavailable");
    }
    int last_result=0,profile_result=0,skill_result=0,mcp_result=0;std::string prompt;
    const auto exit_status=[&]{return last_result!=0?last_result:profile_result!=0?profile_result:skill_result!=0?skill_result:mcp_result;};
    while(std::cerr<<"xMind > "<<std::flush,std::getline(std::cin,prompt)) {
        if(!prompt.empty() && prompt.back()=='\r')prompt.pop_back();
        if(prompt=="/exit")return exit_status();
        if(prompt.find_first_not_of(" \t\r\n")==std::string::npos)continue;
        if(prompt=="/help"){
            std::cerr<<"MCP sign-in: /mcp lists configured OAuth status; /mcp-login SERVER signs in and /mcp-refresh SERVER renews an existing grant using current native revisions. /mcp-status REQUEST, /mcp-open REQUEST, /mcp-cancel REQUEST and /mcp-watch REQUEST inspect, open, cancel or observe that backend-owned attempt. Closing chat only detaches.\n";
            std::cerr<<"/compose [GRAPH_ID] collects a multiline request until /send; /discard cancels it. Use //send or //discard for those literal lines. Other slash lines remain request text. Unfinished EOF and blocks over 1 MiB are discarded without submission.\n";
            std::cerr<<"Context controls: context SESSION [MODEL], compact-context SESSION HEAD_REV REQUEST_ID [MODEL], context-request SESSION REQUEST_ID [MODEL]. Resume a ready closed graph with resume-graph ROOT CHECKPOINT_REV.\n";
            std::cerr<<"/runs lists recorded root runs in the selected conversation; use /watch or /graph-watch to attach one.\n";
            std::cerr<<"Agent /watch includes actual owned inspection or coding children and native plan questions when supported. Direct commands: children RUN, child-history PARENT CHILD, tree-events RUN [CURSOR], delegation, planning, inspect-plan RUN.\n";
            std::cerr<<"/graphs lists registered backend graphs; /graph GRAPH_ID REQUEST starts one at its displayed catalog revision.\n";
            std::cerr<<"/watch RUN_ID attaches an existing single-agent run; /graph-watch ROOT_ID attaches a graph with explicit input/approvals. Neither submits another run.\n";
            std::cerr<<"/profiles lists saved provider metadata without changing this chat's admission binding; /profile ID REVISION explicitly selects a shared profile and clears this chat's model override.\n";
            std::cerr<<"/skills lists current workspace guide metadata without activating a guide.\n/session-skills inspects current attachments; /attach-skill ID and /remove-skill ID change exact catalogue ids (spaces are part of the id); /clear-skills clears the observed selection. Native revisions prevent stale writes.\n/models lists backend-enabled models; /model ID selects one for subsequent turns; /model resets to the server default.\n/provider-models discovers account models through the backend's saved key.\n/sessions lists saved conversations; /session ID resumes one; /new starts an empty conversation on your next request.\n/title NAME renames the selected conversation; /history displays its saved messages; /exit leaves. Prefix a literal slash request with another slash.\n";continue;
        }
        if(prompt=="/mcp"||prompt.starts_with("/mcp ")||prompt.starts_with("/mcp-")){
            try{
                const auto split=prompt.find(' ');const auto verb=prompt.substr(0,split),identity=split==std::string::npos?std::string{}:prompt.substr(split+1);std::string command;
                if(verb=="/mcp"&&identity.empty())command="mcp-auth";
                else if(verb=="/mcp-login")command="mcp-login";
                else if(verb=="/mcp-refresh")command="mcp-refresh";
                else if(verb=="/mcp-status")command="mcp-login-status";
                else if(verb=="/mcp-open")command="mcp-login-open";
                else if(verb=="/mcp-cancel")command="mcp-login-cancel";
                else if(verb=="/mcp-watch")command="mcp-login-watch";
                else throw std::invalid_argument("Use /mcp, /mcp-login SERVER, /mcp-refresh SERVER, /mcp-status REQUEST, /mcp-open REQUEST, /mcp-cancel REQUEST or /mcp-watch REQUEST.");
                if(command!="mcp-auth")mcp_identity(identity,command=="mcp-login"||command=="mcp-refresh");
                if(command=="mcp-login-watch")mcp_result=mcp_watch(client,headers,identity);
                else {const auto value=mcp_command(client,headers,command,identity);std::cout<<Json{{"type",command=="mcp-auth"?"mcp_authorization_servers":"mcp_authorization_attempt"},{"result",value}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("MCP observation output unavailable; backend sign-in continues");mcp_result=0;}
            }catch(const std::exception& error){std::cerr<<error.what()<<'\n';mcp_result=1;}continue;
        }
        if(prompt.starts_with("/watch ")||prompt.starts_with("/graph-watch ")){
            const bool graphAttachment=prompt.starts_with("/graph-watch ");const auto id=prompt.substr(graphAttachment?13:7);
            if(id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos){std::cerr<<"Use /watch with a recorded run ID.\n";continue;}
            const auto response=client.Get("/v1/runs/"+id,headers);
            if(!response)throw std::runtime_error("Cannot reach xMind Server to attach the run");
            if(response->status==404){std::cerr<<"Run was not found.\n";continue;}
            if(response->status!=200)throw std::runtime_error("Server rejected run attachment");
            const auto run=Json::parse(response->body);
            if(!run.is_object()||run.value("id",std::string{})!=id||!run.contains("session_id")||!run["session_id"].is_string())throw std::runtime_error("Invalid attached run identity");
            if(run.value("graph_root",false)!=graphAttachment||!run.value("parent_id",std::string{}).empty()){std::cerr<<"Use /graph-watch for graph roots and /watch for single-agent runs.\n";continue;}
            const auto owner=run["session_id"].get<std::string>();
            if(owner.empty()||owner.size()>128||owner.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid attached conversation identity");
            if(!session.empty()&&session!=owner){std::cerr<<"Select the run's conversation with /session first, or use /new before attaching.\n";continue;}
            const auto saved=request("/v1/sessions/"+owner+"/history");if(!saved.is_array())throw std::runtime_error("Invalid attached conversation history");
            session=owner;
            std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<Json{{"type","history"},{"session_id",session},{"history",saved}}.dump()<<'\n'<<Json{{"type","run_attached"},{"run",run}}.dump()<<'\n'<<std::flush;
            last_result=watch_run(client,headers,id,0,graphAttachment,true);profile_result=0;
            std::cout<<Json{{"type","turn_finished"},{"run_id",id},{"exit_status",last_result}}.dump()<<'\n'<<std::flush;
            continue;
        }
        if(prompt=="/runs"){
            const auto saved=session.empty()?Json::array():request("/v1/sessions/"+session+"/runs");
            if(!saved.is_array())throw std::runtime_error("Invalid backend run catalogue");
            std::cout<<Json{{"type","runs"},{"session_id",session},{"runs",saved}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt=="/sessions"){
            const auto saved=request("/v1/sessions");if(!saved.is_array())throw std::runtime_error("Invalid backend session catalogue");
            std::cout<<Json{{"type","sessions"},{"sessions",saved},{"selected_session",session}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt=="/new"){
            session.clear();std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<Json{{"type","history"},{"session_id",session},{"history",Json::array()}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt.starts_with("/title ")){
            if(session.empty()){std::cerr<<"Select a saved conversation before renaming it.\n";continue;}
            const auto saved=request("/v1/sessions");if(!saved.is_array())throw std::runtime_error("Invalid backend session catalogue");
            const auto selected=std::find_if(saved.begin(),saved.end(),[&](const Json& item){return item.is_object()&&item.value("id",std::string{})==session;});
            if(selected==saved.end()||!selected->contains("title")||!(*selected)["title"].is_string())throw std::runtime_error("Selected conversation is unavailable");
            const Json body={{"title",prompt.substr(7)},{"expected_title",(*selected)["title"]}};
            const auto renamed=client.Post("/v1/sessions/"+session+"/title",headers,body.dump(),"application/json");
            if(!renamed)throw std::runtime_error("Cannot reach xMind Server during rename");
            if(renamed->status==409){std::cerr<<"Conversation title changed. Use /sessions and retry.\n";continue;}
            if(renamed->status==400){std::cerr<<"Use a nonempty, single-line conversation title within 4096 bytes.\n";continue;}
            if(renamed->status!=200)throw std::runtime_error("Server rejected conversation rename");
            std::cout<<Json{{"type","session_renamed"},{"session",Json::parse(renamed->body)}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt.starts_with("/session ")){
            const auto selected=prompt.substr(9);
            if(selected.empty()||selected.size()>128||selected.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos){std::cerr<<"Invalid session ID. Use /sessions to see saved conversations.\n";continue;}
            const auto saved=request("/v1/sessions");if(!saved.is_array())throw std::runtime_error("Invalid backend session catalogue");bool found=false;
            for(const auto& item:saved)if(item.is_object()&&item.value("id",std::string{})==selected)found=true;
            if(!found){std::cerr<<"Session was not found. Use /sessions to see saved conversations.\n";continue;}
            const auto selectedHistory=request("/v1/sessions/"+selected+"/history");if(!selectedHistory.is_array())throw std::runtime_error("Invalid session history");
            session=selected;std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<Json{{"type","history"},{"session_id",session},{"history",selectedHistory}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt=="/profiles"){
            if(!profile_admission){std::cerr<<"This backend does not support provider profile admission.\n";profile_result=1;continue;}
            const auto metadata=request("/v1/provider/profiles");(void)provider_admission_binding(metadata);
            std::cout<<Json{{"type","provider_profiles"},{"metadata",metadata}}.dump()<<'\n'<<std::flush;profile_result=0;continue;
        }
        if(prompt=="/profile"||prompt.starts_with("/profile ")){
            std::string selected;std::int64_t revision=0;
            try{
                const auto fields=prompt.size()>9?prompt.substr(9):std::string{};const auto split=fields.find(' ');
                if(split==std::string::npos)throw std::invalid_argument("Use /profile ID REVISION");
                selected=provider_profile_identity(fields.substr(0,split));revision=provider_revision(fields.substr(split+1));
            }catch(const std::invalid_argument&){std::cerr<<"Use /profile ID REVISION with a valid saved profile ID and safe revision. No selection sent.\n";profile_result=1;continue;}
            if(!profile_admission){std::cerr<<"This backend does not support provider profile admission. No selection sent.\n";profile_result=1;continue;}
            const Json body={{"id",selected},{"expected_revision",revision}};
            const auto response=client.Post("/v1/provider/profiles/select",headers,body.dump(),"application/json");
            if(!response)throw std::runtime_error("Cannot reach xMind Server for profile selection; outcome is unknown. Inspect /profiles before selecting again.");
            if(response->status<200||response->status>=300){
                std::cout<<Json{{"type","provider_profile_rejected"},{"http_status",response->status},{"error",provider_rejection(response->body)}}.dump()<<'\n'<<std::flush;
                std::cerr<<"Backend rejected profile selection (HTTP "<<response->status<<"). Chat binding and model are unchanged; no automatic retry. Use /profiles to inspect the shared revision.\n";profile_result=1;continue;
            }
            const auto metadata=Json::parse(response->body);const auto committed=provider_admission_binding(metadata);
            if(committed.at("provider_profile_id")!=selected||committed.at("expected_provider_revision")!=revision+1)throw std::runtime_error("Invalid committed profile selection; inspect /profiles before continuing");
            provider_binding=committed;model.clear();profile_result=0;
            std::cout<<Json{{"type","provider_profile"},{"metadata",metadata},{"provider_profile_id",committed.at("provider_profile_id")},{"expected_provider_revision",committed.at("expected_provider_revision")},{"selected_model",model}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt=="/provider-models"){
            Json body;std::string route;
            if(profile_admission){
                const auto metadata=request("/v1/provider/profiles");
                if(metadata.value("active",std::string{}).empty()){std::cerr<<"Select a saved provider profile before discovering account models.\n";profile_result=1;continue;}
                body=active_profile_discovery(metadata);route="/v1/provider/profiles/models";
            }else{
                const auto setup=request("/v1/provider/configuration");
                if(!setup.is_object() || setup.value("configured",false)!=true || setup.value("provider",std::string{})!="openai" || !setup.contains("revision") || !setup["revision"].is_number_integer() || setup["revision"]<1 || setup["revision"]>9007199254740991){std::cerr<<"Configure a backend provider key before discovering account models.\n";continue;}
                body={{"expected_revision",setup["revision"]}};route="/v1/provider/models";
            }
            const auto response=provider_discovery_request(client,headers,route,body);
            if(!response)throw std::runtime_error("Cannot reach xMind Server for provider discovery");
            if(response->status<200||response->status>=300){
                std::cout<<Json{{"type","provider_models_rejected"},{"http_status",response->status},{"error",provider_rejection(response->body)}}.dump()<<'\n'<<std::flush;
                std::cerr<<"Backend rejected provider discovery (HTTP "<<response->status<<"). Chat binding and model are unchanged; no automatic retry.\n";profile_result=1;continue;
            }
            const auto catalogue=Json::parse(response->body);
            if(!catalogue.is_object() || !catalogue.contains("models") || !catalogue["models"].is_array())throw std::runtime_error("Invalid provider model catalogue");
            std::cout<<Json{{"type","provider_models"},{"catalogue",catalogue},{"provider_revision",body.at("expected_revision")}}.dump()<<'\n'<<std::flush;profile_result=0;
            std::cerr<<"These are discovered account models. /models shows models currently enabled for execution. Discovery does not change shared provider settings.\n";
            continue;
        }
        if(prompt=="/history"){
            const auto saved=session.empty()?Json::array():request("/v1/sessions/"+session+"/history");
            if(!saved.is_array())throw std::runtime_error("Invalid session history");
            std::cout<<Json{{"type","history"},{"session_id",session},{"history",saved}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt=="/skills"){
            const auto catalogue=request("/v1/workspace/skills");
            if(!catalogue.is_object()||!catalogue.contains("skills")||!catalogue["skills"].is_array())throw std::runtime_error("Invalid native skill catalogue");
            std::cout<<Json{{"type","workspace_skills"},{"catalogue",catalogue}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt=="/session-skills"||prompt=="/clear-skills"||prompt.starts_with("/attach-skill ")||prompt.starts_with("/remove-skill ")){
            const bool inspect=prompt=="/session-skills",clear=prompt=="/clear-skills",attach=prompt.starts_with("/attach-skill ");
            if(!health.value("skill_controls",false)){skill_result=1;std::cerr<<"This backend has no session skill controls. Update the native backend.\n";continue;}
            if(session.empty()){
                if(!attach){std::cerr<<"Select a conversation or attach a skill to start one.\n";continue;}
                const Json body={{"title","Workspace skills"}};const auto created=request("/v1/sessions",&body);if(!created.contains("id")||!created["id"].is_string())throw std::runtime_error("Invalid created skill conversation");session=created["id"].get<std::string>();if(session.empty()||session.size()>128||session.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid skill conversation identity");std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<std::flush;
            }
            const auto path="/v1/sessions/"+session+"/skills";
            try{
                if(inspect||!observed_skills.is_object()||observed_skills.value("session_id",std::string{})!=session)observed_skills=request(path);
                skill_snapshot(observed_skills,session);
                if(inspect){std::cout<<Json{{"type","session_skills"},{"selection",observed_skills}}.dump()<<'\n'<<std::flush;continue;}
                auto ids=observed_skills["ids"];if(clear)ids=Json::array();else{
                    const auto selected=prompt.substr(14);skill_ids(Json::array({selected}));const auto found=std::find(ids.begin(),ids.end(),selected);
                    if(attach){if(found!=ids.end()){if(std::find(observed_skills["manual_ids"].begin(),observed_skills["manual_ids"].end(),selected)!=observed_skills["manual_ids"].end()){std::cerr<<"Skill is already explicitly attached. Use /session-skills to inspect it.\n";continue;}}else ids.push_back(selected);}else{if(found==ids.end()){std::cerr<<"Skill is not selected. Use /session-skills to inspect it.\n";continue;}ids.erase(found);}
                }
                const auto body=skill_change(observed_skills,session,ids);const auto response=client.Post(path,headers,body.dump(),"application/json");if(!response)throw std::runtime_error("Cannot reach xMind Server during skill change");
                if(response->status==400||response->status==403||response->status==409){skill_result=1;observed_skills=Json();std::cerr<<"Skill change rejected (HTTP "<<response->status<<"). Use /session-skills and /skills to refresh before retrying; no write was replayed.\n";continue;}
                if(response->status!=200)throw std::runtime_error("Server rejected session skill change");const auto acknowledged=Json::parse(response->body);skill_acknowledgement(acknowledged,session,body);observed_skills=acknowledged;skill_result=0;std::cout<<Json{{"type","session_skills"},{"selection",observed_skills}}.dump()<<'\n'<<std::flush;
            }catch(const std::invalid_argument&){skill_result=1;std::cerr<<"Invalid skill selection or snapshot; use /session-skills to refresh and choose at most eight distinct catalogue ids.\n";}
            continue;
        }
        if(prompt=="/models" || prompt=="/model" || prompt.starts_with("/model ")){
            const auto catalogue=request("/v1/models");
            if(!catalogue.is_object() || !catalogue.contains("models") || !catalogue["models"].is_array() || !catalogue.contains("default_model") || !catalogue["default_model"].is_string())throw std::runtime_error("Invalid backend model catalogue");
            if(prompt=="/models")std::cout<<Json{{"type","models"},{"catalogue",catalogue},{"selected_model",model}}.dump()<<'\n'<<std::flush;
            else {
                const auto selected=prompt=="/model"?std::string{}:prompt.substr(7);
                bool available=selected.empty();
                for(const auto& item:catalogue["models"])if(item.is_object() && item.value("id",std::string{})==selected)available=true;
                if(!available){std::cerr<<"Model is not enabled by this backend. Use /models to see available IDs.\n";continue;}
                model=selected;
                std::cout<<Json{{"type","model"},{"model_id",model},{"default_model",catalogue["default_model"]}}.dump()<<'\n'<<std::flush;
            }
            continue;
        }
        bool graphSubmission=false,composed=false;std::string graphId;std::int64_t graphRevision=0;
        if(prompt=="/compose"||prompt.starts_with("/compose ")){
            graphId=prompt=="/compose"?std::string{}:prompt.substr(9);
            if(prompt!="/compose"&&(graphId.empty()||graphId.size()>64||graphId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)){
                std::cerr<<"Use /compose or /compose GRAPH_ID.\n";continue;
            }
            std::cerr<<"Compose request: /send submits, /discard cancels.\n";
            std::cout<<Json{{"type","compose_started"},{"graph_id",graphId}}.dump()<<'\n'<<std::flush;
            prompt.clear();std::string line;bool submitted=false,discarded=false,invalid=false;
            while(std::getline(std::cin,line)){
                if(!line.empty()&&line.back()=='\r')line.pop_back();
                if(line=="/send"){submitted=true;break;}
                if(line=="/discard"){discarded=true;break;}
                if(line=="//send"||line=="//discard")line.erase(0,1);
                constexpr std::size_t limit=1024*1024;
                if(invalid)continue;
                if(line.find('\0')!=std::string::npos||line.size()>=limit||prompt.size()>limit-line.size()-1){invalid=true;prompt.clear();continue;}
                prompt.append(line);prompt.push_back('\n');
            }
            const bool empty=prompt.find_first_not_of(" \t\r\n")==std::string::npos;
            if(!submitted||invalid||empty){
                const auto reason=discarded?"user":!submitted?"eof":invalid?"invalid_or_too_large":"empty";
                std::cout<<Json{{"type","compose_discarded"},{"reason",reason}}.dump()<<'\n'<<std::flush;
                std::cerr<<"Composed request discarded; no run was submitted.\n";
                if(!submitted&&!discarded)break;
                continue;
            }
            composed=true;
        }
        if((!composed&&(prompt=="/graphs"||prompt.starts_with("/graph ")))||(composed&&!graphId.empty())){
            const auto catalogue=request("/v1/graphs");
            if(!catalogue.is_object()||!catalogue.contains("graphs")||!catalogue["graphs"].is_array())throw std::runtime_error("Invalid backend graph catalogue");
            if(!composed&&prompt=="/graphs"){std::cout<<Json{{"type","graphs"},{"catalogue",catalogue}}.dump()<<'\n'<<std::flush;continue;}
            const auto split=composed?std::string::npos:prompt.find(' ',7);
            if(!composed)graphId=split==std::string::npos?std::string{}:prompt.substr(7,split-7);
            const auto task=composed?prompt:split==std::string::npos?std::string{}:prompt.substr(split+1);
            if(graphId.empty()||graphId.size()>64||graphId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||task.find_first_not_of(" \t\r\n")==std::string::npos){std::cerr<<"Use /graph GRAPH_ID REQUEST with a registered graph and nonempty request.\n";continue;}
            const auto& entries=catalogue["graphs"];const auto selected=std::find_if(entries.begin(),entries.end(),[&](const Json& item){return item.is_object()&&item.value("id",std::string{})==graphId;});
            if(selected==entries.end()){std::cerr<<"Graph was not found. Use /graphs to see registered graphs.\n";continue;}
            if(!selected->contains("executable")||!(*selected)["executable"].is_boolean()||!selected->contains("revision")||!(*selected)["revision"].is_number_integer()||(*selected)["revision"]<1||(*selected)["revision"]>9007199254740991)throw std::runtime_error("Invalid registered graph capabilities");
            if(!(*selected)["executable"].get<bool>()){std::cerr<<"Graph cannot execute with the current backend configuration. Use /graphs to inspect availability.\n";continue;}
            graphRevision=(*selected)["revision"].get<std::int64_t>();graphSubmission=true;prompt=task;
        }
        if(!composed&&!graphSubmission&&prompt.front()=='/'){
            if(prompt.starts_with("//"))prompt.erase(0,1);
            else {std::cerr<<"Unknown chat command. Use /help, or // to send a literal slash request.\n";continue;}
        }
        if(prompt.size()>1024*1024)throw std::invalid_argument("Prompt exceeds limits");
        if(!graphSubmission){const auto current=request("/v1/health");
        if(!current.is_object()||!current.contains("agent_execution")||!current["agent_execution"].is_boolean())throw std::runtime_error("Invalid backend chat capabilities");
        if(!current["agent_execution"].get<bool>()){std::cerr<<"Configure a backend model before submitting a request. Saved conversations and runs remain available for inspection.\n";continue;}}
        if(session.empty()){
            const auto first=prompt.find_first_not_of(" \t\r\n");auto end=std::min(prompt.size(),first+80);
            // Truncate only at a UTF-8 boundary, preserving the original prompt.
            while(end<prompt.size()&&end>first&&(static_cast<unsigned char>(prompt[end])&0xc0)==0x80)--end;
            auto title=prompt.substr(first,end-first);for(auto& byte:title)if(static_cast<unsigned char>(byte)<32)byte=' ';
            const Json body={{"title",title}};const auto created=request("/v1/sessions",&body);
            if(!created.is_object() || !created.contains("id") || !created["id"].is_string())throw std::runtime_error("Invalid created session");session=created["id"].get<std::string>();
            if(session.empty() || session.size()>128 || session.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid created session identity");
            std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<std::flush;
        }
        Json body={{"session_id",session},{"prompt",prompt}};if(!model.empty())body["model_id"]=model;
        if(graphSubmission){body["graph_id"]=graphId;body["graph_revision"]=graphRevision;}
        if(health.value(graphSubmission?"graph_provider_profile_admission":"provider_profile_admission",false))body.update(provider_binding);
        const auto admitted=client.Post(graphSubmission?"/v1/graph-runs":"/v1/runs",headers,body.dump(),"application/json");
        if(!admitted)throw std::runtime_error("Cannot reach xMind Server during run admission; admission outcome is unknown. Inspect /runs before resubmitting.");
        if(admitted->status==400||admitted->status==404||admitted->status==409||admitted->status==429||admitted->status==503){
            std::cout<<Json{{"type","run_rejected"},{"session_id",session},{"http_status",admitted->status},{"prompt",prompt},{"graph_id",graphSubmission?graphId:std::string{}}}.dump()<<'\n'<<std::flush;
            std::cerr<<"Backend rejected run admission (HTTP "<<admitted->status<<"). Request retained above; no automatic retry. Use /runs to inspect existing work.\n";
            last_result=1;continue;
        }
        if(admitted->status!=202)throw std::runtime_error("Unexpected run admission response; inspect /runs before resubmitting");
        const auto run=Json::parse(admitted->body);
        if(!run.is_object() || run.value("session_id",std::string{})!=session || run.value("graph_root",false)!=graphSubmission || !run.contains("id") || !run["id"].is_string())throw std::runtime_error("Invalid chat run admission");const auto id=run["id"].get<std::string>();
        if(id.empty() || id.size()>128 || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid chat run identity");
        std::cout<<Json{{"type","run"},{"run",run}}.dump()<<'\n'<<std::flush;
        last_result=watch_run(client,headers,id,0,graphSubmission,true);profile_result=0;
        std::cout<<Json{{"type","turn_finished"},{"run_id",id},{"exit_status",last_result}}.dump()<<'\n'<<std::flush;
    }
    if(!std::cin.eof())throw std::runtime_error("Chat input is unavailable");
    return exit_status();
}
}

int cli_main(int argc,char** argv,const std::string& workspace,const std::function<agentflow::LocalProfileConnection()>& profile) {
    try {
        if(argc<3) throw std::invalid_argument("Usage: xmind_cli PORT COMMAND [ARGS] (commands: health, chat [SESSION [MODEL]], sessions, create-session, rename-session SESSION TITLE EXPECTED_TITLE, history, runs, run, cancel, status, events, watch, models, agents, session-agent SESSION, set-agent SESSION AGENT_ID|default EXPECTED_REVISION AGENT_REVISION, skills, session-skills SESSION, set-skills SESSION SNAPSHOT_JSON_FILE [ID...], provider-profiles, profile-models ID ROUTE REVISION [KEY_ENV], save-profile ID ROUTE MODEL REVISION [KEY_ENV] [--activate], select-profile ID REVISION, provider, provider-models [KEY_ENV REVISION], configure-provider MODEL KEY_ENV REVISION, graphs, graph-run SESSION GRAPH REV PROMPT [MODEL], graph ROOT, graph-input ROOT NODE REV JSON_FILE, graph-events ROOT [AFTER], graph-watch ROOT [AFTER], graph-children ROOT, planning, inspect-plan ROOT, plan-input ROOT REQUEST REV SEQUENCE JSON_FILE, resume-plan ROOT REV SEQUENCE, instructions, mcp-servers, process-profiles, operations, operation, inspect-edit, decide, append-message)");
        const std::string port_text=argv[1],command=argv[2];int port=0;
        const auto parsed=std::from_chars(port_text.data(),port_text.data()+port_text.size(),port);
        if(parsed.ec!=std::errc{} || parsed.ptr!=port_text.data()+port_text.size() || port<1 || port>65535) throw std::invalid_argument("Invalid port");
        auto id=[](const std::string& value) {
            if(value.empty() || value.size()>128) throw std::invalid_argument("Invalid ID");
            for(unsigned char c:value) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-')) throw std::invalid_argument("Invalid ID");
            return value;
        };
        using Json=nlohmann::json;std::string path,chat_model,mcp_id;Json body;bool post=false,watch=false,graph_watch=false,chat=false,saved_provider_key=false,profile_operation=false,mcp_operation=false;std::int64_t watch_cursor=0;
        if(command=="health" && argc==3) path="/v1/health";
        else if(command=="chat" && argc>=3 && argc<=5){chat=true;if(argc>=4)path=id(argv[3]);if(argc==5)chat_model=argv[4];}
        else if(command=="sessions" && argc==3) path="/v1/sessions";
        else if(command=="create-session" && argc==4) {path="/v1/sessions";body={{"title",argv[3]}};post=true;}
        else if(command=="rename-session" && argc==6) {path="/v1/sessions/"+id(argv[3])+"/title";body={{"title",argv[4]},{"expected_title",argv[5]}};post=true;}
        else if(command=="history" && argc==4) path="/v1/sessions/"+id(argv[3])+"/history";
        else if(command=="context"&&(argc==4||argc==5))path="/v1/sessions/"+id(argv[3])+"/context"+(argc==5?"?model_id="+httplib::encode_query_component(provider_profile_identity(argv[4])):std::string{});
        else if(command=="context-request"&&(argc==5||argc==6))path="/v1/sessions/"+id(argv[3])+"/context/requests/"+id(argv[4])+(argc==6?"?model_id="+httplib::encode_query_component(provider_profile_identity(argv[5])):std::string{});
        else if(command=="compact-context"&&(argc==6||argc==7)){
            path="/v1/sessions/"+id(argv[3])+"/context/compact";body={{"expected_head_revision",provider_revision(argv[4])},{"id",id(argv[5])}};
            if(argc==7)body["model_id"]=provider_profile_identity(argv[6]);post=true;
        }
        else if(command=="runs" && argc==4) path="/v1/sessions/"+id(argv[3])+"/runs";
        else if(command=="status" && argc==4) path="/v1/runs/"+id(argv[3]);
        else if(command=="operations" && argc==4) path="/v1/runs/"+id(argv[3])+"/operations";
        else if(command=="children"&&argc==4)path="/v1/runs/"+id(argv[3])+"/children";
        else if(command=="child-history"&&argc==5)path="/v1/runs/"+id(argv[3])+"/children/"+id(argv[4])+"/history";
        else if(command=="tree-events"&&(argc==4||argc==5)){const auto after=event_cursor(argc==5?argv[4]:"0");path="/v1/runs/"+id(argv[3])+"/tree-events?after="+std::to_string(after);}
        else if(command=="operation" && argc==4) path="/v1/operations/"+id(argv[3]);
        else if(command=="inspect-edit" && argc==4) path="/v1/operations/"+id(argv[3])+"/inspection";
        else if(command=="decide" && argc==5) {
            const std::string decision=argv[4];
            if(decision!="allow" && decision!="deny") throw std::invalid_argument("Decision must be allow or deny");
            path="/v1/operations/"+id(argv[3])+"/decision";body={{"decision",decision}};post=true;
        }
        else if(command=="models" && argc==3) path="/v1/models";
        else if(command=="agents"&&argc==3)path="/v1/agents";
        else if(command=="session-agent"&&argc==4)path="/v1/sessions/"+id(argv[3])+"/agent";
        else if(command=="set-agent"&&argc==7){const auto session=id(argv[3]);const auto expected=provider_revision(argv[5]),definition_revision=provider_revision(argv[6]);Json agent_id=nullptr;if(std::string(argv[4])!="default"){const std::string value=argv[4];if(value.empty()||value.size()>64||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||definition_revision<1)throw std::invalid_argument("Invalid named agent identity or revision");agent_id=value;}else if(definition_revision!=0)throw std::invalid_argument("Default agent selection requires definition revision zero");path="/v1/sessions/"+session+"/agent";body={{"agent_id",std::move(agent_id)},{"agent_revision",definition_revision},{"expected_revision",expected}};post=true;}
        else if(command=="skills" && argc==3) path="/v1/workspace/skills";
        else if(command=="session-skills"&&argc==4)path="/v1/sessions/"+id(argv[3])+"/skills";
        else if(command=="set-skills"&&argc>=5&&argc<=13){const auto session=id(argv[3]);const auto observed=read_skill_snapshot(argv[4]);auto ids=Json::array();for(int i=5;i<argc;i++)ids.push_back(argv[i]);body=skill_change(observed,session,ids);path="/v1/sessions/"+session+"/skills";post=true;}
        else if(command=="provider-profiles" && argc==3){path="/v1/provider/profiles";profile_operation=true;}
        else if(command=="profile-models" && (argc==6||argc==7)){
            const auto profile=provider_profile_identity(argv[3]),route=provider_profile_identity(argv[4]);const auto revision=provider_revision(argv[5]);
            body=argc==7?provider_key_fields(argv[6],argv[5]):Json{{"expected_revision",revision}};
            body["id"]=profile;body["route_id"]=route;path="/v1/provider/profiles/models";post=true;profile_operation=true;
        }
        else if(command=="save-profile" && argc>=7&&argc<=9){
            const auto profile=provider_profile_identity(argv[3]),route=provider_profile_identity(argv[4]),model=provider_profile_identity(argv[5]);const auto revision=provider_revision(argv[6]);
            std::string variable;bool activate=false,has_key_env=false;
            if(argc==8){if(std::string(argv[7])=="--activate")activate=true;else{variable=argv[7];has_key_env=true;}}
            else if(argc==9){if(std::string(argv[8])!="--activate")throw std::invalid_argument("Use save-profile ID ROUTE MODEL REVISION [KEY_ENV] [--activate]");variable=argv[7];has_key_env=true;activate=true;}
            body=has_key_env?provider_key_fields(variable,argv[6]):Json{{"expected_revision",revision}};
            body["id"]=profile;body["route_id"]=route;body["model"]=model;if(activate)body["activate"]=true;
            path="/v1/provider/profiles";post=true;profile_operation=true;
        }
        else if(command=="select-profile" && argc==5){
            const auto profile=provider_profile_identity(argv[3]);const auto revision=provider_revision(argv[4]);
            path="/v1/provider/profiles/select";body={{"id",profile},{"expected_revision",revision}};post=true;profile_operation=true;
        }
        else if(command=="provider" && argc==3) path="/v1/provider/configuration";
        else if(command=="provider-models" && (argc==3 || argc==5)){
            path="/v1/provider/models";post=true;
            if(argc==3)saved_provider_key=true;
            else body=provider_key_fields(argv[3],argv[4]);
        }
        else if(command=="configure-provider" && argc==6){
            path="/v1/provider/configuration";body=provider_key_fields(argv[4],argv[5]);body["model"]=argv[3];post=true;
        }
        else if(command=="mcp-servers" && argc==3) path="/v1/mcp/servers";
        else if(command=="mcp-auth"&&argc==3)mcp_operation=true;
        else if((command=="mcp-login"||command=="mcp-refresh"||command=="mcp-login-status"||command=="mcp-login-cancel"||command=="mcp-login-open"||command=="mcp-login-watch")&&argc==4){mcp_operation=true;mcp_id=mcp_identity(argv[3],command=="mcp-login"||command=="mcp-refresh");}
        else if(command=="process-profiles" && argc==3) path="/v1/process/profiles";
        else if(command=="instructions" && argc==3) path="/v1/agent/instructions";
        else if(command=="delegation"&&argc==3)path="/v1/agent/delegation";
        else if(command=="planning"&&argc==3)path="/v1/agent/planning";
        else if(command=="inspect-plan"&&argc==4)path="/v1/runs/"+id(argv[3])+"/plan";
        else if(command=="resume-plan"&&argc==6){path="/v1/runs/"+id(argv[3])+"/plan/resume";body={{"expected_revision",plan_revision(argv[4])},{"expected_state_sequence",plan_revision(argv[5])}};post=true;}
        else if(command=="plan-input"&&argc==8){
            const auto root=id(argv[3]),request=id(argv[4]);const auto revision=plan_revision(argv[5]),sequence=plan_revision(argv[6]);
            std::ifstream file(std::filesystem::u8path(argv[7]),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read plan input file");std::string source;char byte;while(file.get(byte)){if(source.size()>=16384)throw std::invalid_argument("Plan input file exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read plan input file");validate_plan_input(source);
            path="/v1/runs/"+root+"/plan/human/"+request;body={{"input_json",source},{"expected_revision",revision},{"expected_state_sequence",sequence}};post=true;
        }
        else if(command=="graphs" && argc==3)path="/v1/graphs";
        else if(command=="graph" && argc==4)path="/v1/graph-runs/"+id(argv[3]);
        else if(command=="resume-graph"&&argc==5){path="/v1/graph-runs/"+id(argv[3])+"/resume";body={{"expected_checkpoint_revision",plan_revision(argv[4])}};post=true;}
        else if(command=="graph-children" && argc==4)path="/v1/graph-runs/"+id(argv[3])+"/children";
        else if(command=="graph-child-history" && argc==5)path="/v1/graph-runs/"+id(argv[3])+"/children/"+id(argv[4])+"/history";
        else if(command=="graph-events" && (argc==4 || argc==5)){const auto after=event_cursor(argc==5?argv[4]:"0");path="/v1/graph-runs/"+id(argv[3])+"/events?after="+std::to_string(after);}
        else if(command=="graph-run" && (argc==7 || argc==8)){
            const auto revision=event_cursor(argv[5]);if(revision<1 || revision>9007199254740991)throw std::invalid_argument("Invalid graph revision");
            path="/v1/graph-runs";body={{"session_id",id(argv[3])},{"graph_id",argv[4]},{"graph_revision",revision},{"prompt",argv[6]}};if(argc==8)body["model_id"]=argv[7];post=true;
        }
        else if(command=="graph-input" && argc==7){
            const std::string node=argv[4];if(node.empty() || node.size()>64 || node.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid graph node identity");
            const auto revision=event_cursor(argv[5]);if(revision<1 || revision>9007199254740991)throw std::invalid_argument("Invalid graph checkpoint revision");
            std::ifstream file(std::filesystem::u8path(argv[6]),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read graph input file");std::string source;char byte;
            while(file.get(byte)){if(source.size()>=65536)throw std::invalid_argument("Graph input file exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read graph input file");
            std::vector<std::set<std::string>> objects;
            auto input=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
                if(depth>16)throw std::invalid_argument("Graph input JSON nesting exceeds limits");
                if(event==Json::parse_event_t::object_start)objects.emplace_back();
                else if(event==Json::parse_event_t::object_end)objects.pop_back();
                else if(event==Json::parse_event_t::key && !objects.back().insert(value.get<std::string>()).second)throw std::invalid_argument("Graph input JSON contains duplicate keys");
                return true;
            });if(!input.is_object())throw std::invalid_argument("Graph input file must contain a JSON object");
            path="/v1/graph-runs/"+id(argv[3])+"/human/"+node;body={{"input",std::move(input)},{"expected_checkpoint_revision",revision}};post=true;
        }
        else if(command=="run" && (argc==5 || argc==6)) {path="/v1/runs";body={{"session_id",id(argv[3])},{"prompt",argv[4]}};if(argc==6)body["model_id"]=argv[5];post=true;}
        else if(command=="cancel" && argc==4) {path="/v1/runs/"+id(argv[3])+"/cancel";body=Json::object();post=true;}
        else if(command=="watch" && (argc==4 || argc==5)) {path=id(argv[3]);watch=true;watch_cursor=event_cursor(argc==5?argv[4]:"0");}
        else if(command=="graph-watch" && (argc==4 || argc==5)) {path=id(argv[3]);watch=true;graph_watch=true;watch_cursor=event_cursor(argc==5?argv[4]:"0");}
        else if(command=="events" && (argc==4 || argc==5)) {
            const std::string cursor=argc==5?argv[4]:"0";event_cursor(cursor);
            path="/v1/runs/"+id(argv[3])+"/events?after="+cursor;
        }
        else if(command=="append-message" && argc==5) {path="/v1/sessions/"+id(argv[3])+"/messages";body={{"role","user"},{"data",{{"content",argv[4]}}}};post=true;}
        else throw std::invalid_argument("Unknown command or incorrect arguments");
        std::optional<agentflow::LocalProfileConnection> managed;std::string auth,selected=workspace;
        if(profile){managed.emplace(profile());port=managed->port;selected=managed->workspace;auth.assign(reinterpret_cast<const char*>(managed->auth.view().data()),managed->auth.view().size());}
        else{const auto* token=std::getenv("XMIND_AUTH_TOKEN");if(!token)throw std::invalid_argument("Set XMIND_AUTH_TOKEN for the local client");auth=token;}
        agentflow::ConsoleTransport client(port,selected);
        client.set_connection_timeout(5,0);client.set_read_timeout(15,0);client.set_write_timeout(5,0);client.set_follow_location(false);
        const httplib::Headers headers{{"Authorization",std::string("Bearer ")+auth}};
        if(chat)return chat_session(client,headers,path,chat_model);
        if(mcp_operation){if(command=="mcp-login-watch")return mcp_watch(client,headers,mcp_id);std::cout<<mcp_command(client,headers,command,mcp_id).dump(2)<<'\n';if(!std::cout)throw std::runtime_error("MCP observation output unavailable; backend sign-in continues");return 0;}
        if(saved_provider_key){
            const auto current=client.Get("/v1/health",headers);if(!current||current->status!=200)throw std::runtime_error("Cannot inspect backend provider capabilities");
            const auto capability=Json::parse(current->body);
            if(capability.value("provider_profile_admission",false)||capability.value("graph_provider_profile_admission",false)){
                const auto metadata=client.Get("/v1/provider/profiles",headers);if(!metadata||metadata->status!=200)throw std::runtime_error("Cannot inspect backend provider profiles for discovery");
                body=active_profile_discovery(Json::parse(metadata->body));path="/v1/provider/profiles/models";profile_operation=true;
            }else{
                const auto metadata=client.Get("/v1/provider/configuration",headers);
                if(!metadata)throw std::runtime_error("Cannot reach xMind Server for provider discovery");
                if(metadata->status<200 || metadata->status>=300)throw std::runtime_error("Server rejected provider metadata (HTTP "+std::to_string(metadata->status)+")");
                const auto setup=Json::parse(metadata->body);
                if(!setup.is_object() || !setup.contains("configured") || setup["configured"]!=true || setup.value("provider",std::string{})!="openai" || !setup.contains("revision") || !setup["revision"].is_number_integer() || setup["revision"]<1 || setup["revision"]>9007199254740991)throw std::runtime_error("Configure a provider key before discovering saved-account models");
                body={{"expected_revision",setup["revision"]}};
            }
        }
        if(watch)return watch_run(client,headers,path,watch_cursor,graph_watch);
        if(post&&(path=="/v1/runs"||path=="/v1/graph-runs"||command=="compact-context")){
            const auto current=client.Get("/v1/health",headers);if(!current||current->status!=200)throw std::runtime_error("Cannot inspect backend admission capabilities");
            const auto capability=Json::parse(current->body);if(command=="compact-context"&&!capability.value("context_controls",false))throw std::runtime_error("This backend has no registered context controls");if(capability.value(path=="/v1/graph-runs"?"graph_provider_profile_admission":"provider_profile_admission",false)){
                const auto metadata=client.Get("/v1/provider/profiles",headers);if(!metadata||metadata->status!=200)throw std::runtime_error("Cannot inspect backend provider profile");body.update(provider_admission_binding(Json::parse(metadata->body)));
            }
        }
        auto response=post?((path=="/v1/provider/profiles/models"||path=="/v1/provider/models")?provider_discovery_request(client,headers,path,body):client.Post(path,headers,body.dump(),"application/json")):client.Get(path,headers);
        if(!response) throw std::runtime_error("Cannot reach xMind Server");
        if(response->status<200 || response->status>=300) {
            if(profile_operation)std::cerr<<provider_rejection(response->body).dump()<<'\n'<<"Backend rejected provider profile request (HTTP "<<response->status<<"). No automatic retry or selection fallback.\n";
            else {std::cerr<<Json::parse(response->body).dump()<<'\n';if(command=="set-skills")std::cerr<<"Backend rejected session skill change (HTTP "<<response->status<<"). No automatic retry.\n";}
            return 1;
        }
        const auto result=Json::parse(response->body);
        if(command=="session-skills")skill_snapshot(result,id(argv[3]));
        if(command=="set-skills")skill_acknowledgement(result,id(argv[3]),body);
        std::cout<<result.dump(2)<<'\n';return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
#if !defined(XMIND_UNIFIED_EXECUTABLE)
#if defined(_WIN32)
int wmain(int argc,wchar_t** argv){
    try{std::vector<std::string> arguments;arguments.reserve(argc);for(int i=0;i<argc;i++)arguments.push_back(utf8_argument(argv[i]));std::vector<char*> pointers;for(auto& value:arguments)pointers.push_back(value.data());return cli_main(argc,pointers.data());}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
#else
int main(int argc,char** argv){return cli_main(argc,argv);}
#endif
#endif
