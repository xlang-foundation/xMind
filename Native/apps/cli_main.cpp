#include "agentflow/console_transport.hpp"
#include "agentflow/program_entries.hpp"
#include "agentflow/terminal_key_input.hpp"
#include "agentflow/cli_plan_review.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <exception>
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
#include <condition_variable>
#include <deque>
#include <mutex>
#include <memory>
#include <optional>
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include "agentflow/mcp_http_transport.hpp"
#else
#include <unistd.h>
#endif

namespace {
bool human_console_output(){
    if(const auto* mode=std::getenv("XMIND_CLI_OUTPUT")){
        const std::string_view selected(mode);
        if(selected=="human")return true;
        if(selected=="json")return false;
    }
#if defined(_WIN32)
    DWORD mode{};return GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),&mode)!=0;
#else
    return ::isatty(STDOUT_FILENO)!=0;
#endif
}
std::string terminal_safe_text(std::string_view value){
    std::string result;result.reserve(value.size());
    for(const unsigned char byte:value)if(byte=='\n'||byte=='\t'||byte>=0x20&&byte!=0x7f)result.push_back(static_cast<char>(byte));
    return result;
}
void print_human_history(const std::string& session,const nlohmann::json& history){
    using Json=nlohmann::json;std::cout<<"Session "<<session<<"\n";
    if(!history.is_array())return;
    for(const auto& message:history){if(!message.is_object()||!message.contains("role")||!message["role"].is_string()||!message.contains("data")||!message["data"].is_object())continue;const auto role=message["role"].get<std::string>();if(role!="user"&&role!="assistant")continue;const auto& data=message["data"];if(!data.contains("content")||!data["content"].is_string()||data["content"].get_ref<const std::string&>().empty())continue;std::cout<<"\n"<<(role=="user"?"You":"xMind")<<":\n"<<terminal_safe_text(data["content"].get_ref<const std::string&>())<<"\n";if(role=="assistant"&&data.contains("usage")&&data["usage"].is_object()){const auto& usage=data["usage"];const auto input=usage.value("input_tokens",usage.value("prompt_tokens",Json(nullptr))),output=usage.value("output_tokens",usage.value("completion_tokens",Json(nullptr)));if(input.is_number_integer()&&output.is_number_integer())std::cout<<"Input "<<input<<" · Output "<<output;if(usage.contains("total_tokens")&&usage["total_tokens"].is_number_integer())std::cout<<" · Total "<<usage["total_tokens"];if(input.is_number_integer()&&output.is_number_integer())std::cout<<" tokens\n";}}
    std::cout<<'\n'<<std::flush;
}
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
std::string connection_profile_identity(const std::string& value){
    if(value.empty()||value.size()>64||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)
        throw std::invalid_argument("Invalid connection profile identity");
    return value;
}
std::string workspace_relative_attachment_path(std::string value){
    if(value.empty()||value.size()>4096||value.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid workspace-relative attachment path");
    std::replace(value.begin(),value.end(),'\\','/');
    if(value.starts_with('/')||(value.size()>1&&value[1]==':'))throw std::invalid_argument("Prompt attachments must use workspace-relative paths");
    std::size_t start=0;while(start<=value.size()){const auto end=value.find('/',start);const auto part=value.substr(start,end==std::string::npos?std::string::npos:end-start);if(part.empty()||part=="."||part=="..")throw std::invalid_argument("Prompt attachment path contains an invalid component");if(end==std::string::npos)break;start=end+1;}
    return value;
}
std::string workspace_folder_identity(const std::string& value){
    if(value.empty()||value.size()>256||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=std::string::npos)throw std::invalid_argument("Invalid workspace folder ID");return value;
}
nlohmann::json validate_connection_catalog(const nlohmann::json& value){
    using Json=nlohmann::json;
    constexpr std::int64_t maximum=9007199254740991;
    if(!value.is_object()||value.size()!=3||!value.contains("revision")||!value["revision"].is_number_integer()||value["revision"]<1||value["revision"]>maximum||
       !value.contains("active_id")||!value["active_id"].is_string()||!value.contains("profiles")||!value["profiles"].is_array()||value["profiles"].empty()||value["profiles"].size()>32)
        throw std::runtime_error("Invalid backend connection profile catalog");
    const auto active=connection_profile_identity(value["active_id"].get<std::string>());
    std::set<std::string> ids;bool local=false,active_found=false;
    for(const auto& profile:value["profiles"]){
        if(!profile.is_object()||profile.size()!=8||!profile.contains("id")||!profile["id"].is_string()||!profile.contains("name")||!profile["name"].is_string()||
           !profile.contains("kind")||!profile["kind"].is_string()||!profile.contains("origin")||!profile["origin"].is_string()||
           !profile.contains("revision")||!profile["revision"].is_number_integer()||!profile.contains("status")||!profile["status"].is_string()||
           !profile.contains("command_routing")||!profile["command_routing"].is_boolean()||!profile.contains("authenticated")||!profile["authenticated"].is_boolean())
            throw std::runtime_error("Invalid backend connection profile entry");
        const auto id=connection_profile_identity(profile["id"].get<std::string>()),kind=profile["kind"].get<std::string>();
        const auto& name=profile["name"].get_ref<const std::string&>();const auto& origin=profile["origin"].get_ref<const std::string&>();
        if(name.empty()||name.size()>128||name.find_first_of("\r\n")!=std::string::npos||name.find('\0')!=std::string::npos||!ids.insert(id).second||
           profile["revision"]<1||profile["revision"]>value["revision"]||profile["revision"]>maximum)
            throw std::runtime_error("Invalid backend connection profile identity or revision");
        const bool is_local=kind=="local";
        if((!is_local&&kind!="nexus")||(is_local&&(id!="local"||!origin.empty()))||(!is_local&&(id=="local"||!origin.starts_with("https://")||origin.find_first_of("@?#\\ \t\r\n")!=std::string::npos))||
           profile["status"]!=(is_local?"connected":"not_connected")||profile["command_routing"]!=is_local||profile["authenticated"]!=is_local)
            throw std::runtime_error("Backend connection profile reports an invalid connection state");
        local|=is_local;active_found|=id==active;
    }
    if(!local||!active_found)throw std::runtime_error("Backend connection profile catalog has no active Local profile");
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
std::int64_t message_sequence(const std::string& message_id){
    if(!message_id.starts_with("msg_")||message_id.size()<5||message_id.find_first_not_of("0123456789",4)!=std::string::npos)
        throw std::invalid_argument("Message ID must match ^msg_[0-9]+$");
    const auto sequence=event_cursor(message_id.substr(4));if(sequence<1||sequence>9007199254740991LL)throw std::invalid_argument("Invalid message sequence");return sequence;
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
std::string a2a_identity(const std::string& value){if(value.empty()||value.size()>64||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid A2A remote-agent identity");return value;}
std::string a2a_operation_identity(const std::string& value){if(value.empty()||value.size()>128||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::invalid_argument("Invalid A2A operation identity");return value;}
nlohmann::json a2a_catalogue(const nlohmann::json& value){
    using Json=nlohmann::json;if(!value.is_object()||value.size()!=2||!value.contains("revision")||!value["revision"].is_number_integer()||value["revision"]<0||value["revision"]>9007199254740991LL||!value.contains("agents")||!value["agents"].is_array()||value["agents"].size()>256)throw std::runtime_error("Invalid A2A remote-agent catalogue");std::set<std::string> ids;
    for(const auto& agent:value["agents"]){if(!agent.is_object()||agent.size()!=5||!agent.contains("id")||!agent["id"].is_string()||!agent.contains("revision")||!agent["revision"].is_number_integer()||!agent.contains("card_url")||!agent["card_url"].is_string()||!agent.contains("enabled")||!agent["enabled"].is_boolean()||!agent.contains("has_bearer")||!agent["has_bearer"].is_boolean())throw std::runtime_error("Invalid A2A remote-agent metadata");const auto id=a2a_identity(agent["id"].get<std::string>()),url=agent["card_url"].get<std::string>();if(!ids.insert(id).second||agent["revision"]<1||agent["revision"]>9007199254740991LL||url.empty()||url.size()>8192||url.find_first_of("\r\n")!=std::string::npos||url.find('\0')!=std::string::npos)throw std::runtime_error("Invalid A2A remote-agent metadata");}return value;
}
nlohmann::json a2a_command(agentflow::ConsoleTransport& client,const httplib::Headers& headers,const std::string& command,int argc,char** argv){
    using Json=nlohmann::json;const std::string base="/v1/a2a/agents";if(command=="a2a-agents"){const auto response=client.Get(base,headers);if(!response)throw std::runtime_error("Cannot reach xMind Server while listing A2A agents");if(response->status!=200)throw std::runtime_error("A2A agent listing was rejected (HTTP "+std::to_string(response->status)+")");return a2a_catalogue(Json::parse(response->body));}
    std::string path;Json body;bool remove=false,inspect=false,cancel=false,follow_up=false,resubscribe=false;std::string operation_id;
    if(command=="a2a-register"&&(argc==7||argc==8)){
        const auto id=a2a_identity(argv[3]),url=std::string(argv[4]);if(url.empty()||url.size()>8192||url.find_first_of("\r\n")!=std::string::npos||url.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid A2A AgentCard URL");const std::string enabled=argv[5];if(enabled!="true"&&enabled!="false")throw std::invalid_argument("Enabled must be true or false");const auto revision=provider_revision(argv[6]);body={{"id",id},{"card_url",url},{"enabled",enabled=="true"},{"expected_agent_revision",revision}};
        if(argc==8){const std::string variable=argv[7];auto normalized=variable;for(auto& c:normalized)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');if(variable.empty()||variable.size()>128||variable.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos||(variable.front()>='0'&&variable.front()<='9')||normalized=="XMIND_AUTH_TOKEN"||normalized.starts_with("XMIND_UI_"))throw std::invalid_argument("Select a private A2A bearer environment variable");const auto* secret=std::getenv(variable.c_str());if(!secret||!*secret)throw std::invalid_argument("A2A bearer environment variable is empty");const auto length=std::strlen(secret);if(length>4096||std::any_of(secret,secret+length,[](unsigned char c){return c<0x21||c>0x7e;}))throw std::invalid_argument("A2A bearer credential exceeds limits");body["bearer_token"]=std::string(secret,length);
#if defined(_WIN32)
            _putenv_s(variable.c_str(),"");
#endif
        }
        path=base+"/register";
    }else if(command=="a2a-enable"&&argc==6){const auto id=a2a_identity(argv[3]);const std::string enabled=argv[4];if(enabled!="true"&&enabled!="false")throw std::invalid_argument("Enabled must be true or false");body={{"enabled",enabled=="true"},{"expected_agent_revision",provider_revision(argv[5])}};path=base+"/"+id+"/enabled";
    }else if(command=="a2a-remove"&&argc==5){const auto id=a2a_identity(argv[3]);body={{"expected_agent_revision",provider_revision(argv[4])}};path=base+"/"+id+"/remove";remove=true;
    }else if(command=="a2a-inspect"&&(argc==4||argc==5)){operation_id=a2a_operation_identity(argv[3]);path="/v1/operations/"+operation_id+"/a2a/inspect";body=Json::object();if(argc==5){if(std::string(argv[4])!="--include-artifacts")throw std::invalid_argument("Use --include-artifacts to fetch remote task artifacts");body["include_artifacts"]=true;}inspect=true;
    }else if(command=="a2a-cancel"&&argc==4){operation_id=a2a_operation_identity(argv[3]);path="/v1/operations/"+operation_id+"/a2a/cancel";body=Json::object();cancel=true;
    }else if(command=="a2a-follow-up"&&argc==6){operation_id=a2a_operation_identity(argv[3]);const auto sequence=provider_revision(argv[4]);if(sequence<1||sequence>64)throw std::invalid_argument("A2A follow-up sequence must be between 1 and 64");const std::string message=argv[5];if(message.empty()||message.size()>32768||message.find('\0')!=std::string::npos)throw std::invalid_argument("A2A follow-up message is empty or exceeds limits");body={{"sequence",sequence},{"message",message}};path="/v1/operations/"+operation_id+"/a2a/follow-up";follow_up=true;
    }else if(command=="a2a-resubscribe"&&argc==4){operation_id=a2a_operation_identity(argv[3]);path="/v1/operations/"+operation_id+"/a2a/resubscribe";body=Json::object();resubscribe=true;
    }else throw std::invalid_argument("Usage: a2a-agents | a2a-register ID CARD_URL true|false EXPECTED_REVISION [BEARER_ENV] | a2a-enable ID true|false EXPECTED_REVISION | a2a-remove ID EXPECTED_REVISION | a2a-inspect OPERATION_ID [--include-artifacts] | a2a-cancel OPERATION_ID | a2a-follow-up OPERATION_ID SEQUENCE MESSAGE | a2a-resubscribe OPERATION_ID");
    auto encoded=body.dump();auto response=client.Post(path,headers,encoded,"application/json");if(body.contains("bearer_token")){auto& secret=body.at("bearer_token").get_ref<Json::string_t&>();std::fill(secret.begin(),secret.end(),'\\0');}std::fill(encoded.begin(),encoded.end(),'\\0');if(!response)throw std::runtime_error("Cannot reach xMind Server during A2A operation");if(resubscribe){if(response->status!=200)throw std::runtime_error("A2A task resubscription failed or is uncertain (HTTP "+std::to_string(response->status)+"); inspect the remote task before retrying");const auto result=Json::parse(response->body);if(!result.is_object()||result.size()!=7||!result.contains("operation_id")||result["operation_id"]!=operation_id||!result.contains("agent_id")||!result["agent_id"].is_string()||!result.contains("task_id")||!result["task_id"].is_string()||!result.contains("subscription_id")||!result["subscription_id"].is_string()||!result.contains("subscription_state")||result["subscription_state"]!="closed"||!result.contains("last_event_id")||!result["last_event_id"].is_string()||!result.contains("events")||!result["events"].is_array()||result["events"].size()>64)throw std::runtime_error("Invalid A2A task resubscription response");a2a_identity(result["agent_id"].get<std::string>());const auto& task_id=result["task_id"].get_ref<const std::string&>();const auto& subscription_id=result["subscription_id"].get_ref<const std::string&>();const auto& cursor=result["last_event_id"].get_ref<const std::string&>();if(task_id.empty()||task_id.size()>256||task_id.find_first_of("\r\n")!=std::string::npos||task_id.find('\\0')!=std::string::npos||subscription_id.empty()||subscription_id.size()>128||subscription_id.find_first_of("\r\n")!=std::string::npos||subscription_id.find('\\0')!=std::string::npos||cursor.size()>1024||std::any_of(cursor.begin(),cursor.end(),[](unsigned char c){return c<0x20||c==0x7f;}))throw std::runtime_error("Invalid A2A task resubscription identity or cursor");for(const auto& event:result["events"])if(!event.is_object())throw std::runtime_error("Invalid A2A task resubscription event");return result;}if(inspect){if(response->status!=200)throw std::runtime_error("A2A task inspection rejected (HTTP "+std::to_string(response->status)+"); uncertain dispatches are never replayed");const auto result=Json::parse(response->body);if(!result.is_object()||(result.size()!=8&&result.size()!=9)||!result.contains("operation_id")||result["operation_id"]!=operation_id||!result.contains("agent_id")||!result["agent_id"].is_string()||!result.contains("task_id")||!result["task_id"].is_string()||!result.contains("task")||!result["task"].is_object()||!result.contains("inspected_unix_ms")||!result["inspected_unix_ms"].is_number_integer()||result["inspected_unix_ms"]<=0||result["inspected_unix_ms"]>9007199254740991LL||!result.contains("cancel_state")||(result["cancel_state"]!="not_attempted"&&result["cancel_state"]!="outcome_unknown"&&result["cancel_state"]!="acknowledged")||!result.contains("follow_up_sequence")||!result["follow_up_sequence"].is_number_integer()||result["follow_up_sequence"]<0||result["follow_up_sequence"]>64||!result.contains("follow_up_state")||!result["follow_up_state"].is_string()||(result.size()==9&&(!result.contains("follow_up_retry_message")||!result["follow_up_retry_message"].is_string())))throw std::runtime_error("Invalid A2A task inspection response");a2a_identity(result["agent_id"].get<std::string>());return result;}if(cancel){if(response->status!=200)throw std::runtime_error("A2A cancellation rejected or uncertain (HTTP "+std::to_string(response->status)+"); inspect the remote task and do not retry automatically");const auto result=Json::parse(response->body);if(!result.is_object()||result.size()!=6||!result.contains("operation_id")||result["operation_id"]!=operation_id||!result.contains("agent_id")||!result["agent_id"].is_string()||!result.contains("task_id")||!result["task_id"].is_string()||!result.contains("task")||!result["task"].is_object()||!result.contains("cancel_state")||result["cancel_state"]!="acknowledged"||!result.contains("requested_unix_ms")||!result["requested_unix_ms"].is_number_integer()||result["requested_unix_ms"]<=0||result["requested_unix_ms"]>9007199254740991LL||!result["task"].contains("id")||result["task"]["id"]!=result["task_id"])throw std::runtime_error("Invalid A2A task cancellation acknowledgement");a2a_identity(result["agent_id"].get<std::string>());return result;}if(follow_up){if(response->status!=200)throw std::runtime_error("A2A follow-up rejected or uncertain (HTTP "+std::to_string(response->status)+"); inspect the task and never replay an uncertain message");const auto result=Json::parse(response->body);if(!result.is_object()||result.size()!=7||!result.contains("operation_id")||result["operation_id"]!=operation_id||!result.contains("agent_id")||!result["agent_id"].is_string()||!result.contains("task_id")||!result["task_id"].is_string()||!result.contains("sequence")||!result["sequence"].is_number_integer()||result["sequence"]!=body["sequence"]||!result.contains("task")||!result["task"].is_object()||!result["task"].contains("id")||result["task"]["id"]!=result["task_id"]||!result.contains("follow_up_state")||result["follow_up_state"]!="acknowledged"||!result.contains("requested_unix_ms")||!result["requested_unix_ms"].is_number_integer()||result["requested_unix_ms"]<=0||result["requested_unix_ms"]>9007199254740991LL)throw std::runtime_error("Invalid A2A follow-up acknowledgement");a2a_identity(result["agent_id"].get<std::string>());return result;}if(remove&&response->status==204)return Json{{"removed",true},{"id",path.substr(base.size()+1,path.find("/remove")-(base.size()+1))}};if(response->status<200||response->status>=300)throw std::runtime_error("A2A registry update rejected (HTTP "+std::to_string(response->status)+"); refresh a2a-agents before retrying");const auto result=Json::parse(response->body);if(!result.is_object()||result.size()!=1||!result.contains("agent"))throw std::runtime_error("Invalid A2A registry acknowledgement");const auto wrapped=Json{{"revision",std::max<std::int64_t>(0,result["agent"].value("revision",0LL))},{"agents",Json::array({result["agent"]})}};a2a_catalogue(wrapped);return result;
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
void agent_catalogue(const nlohmann::json& value){
    if(!value.is_object()||value.size()!=1||!value.contains("agents")||!value["agents"].is_array()||value["agents"].size()>64)throw std::runtime_error("Invalid native agent catalogue");
    std::set<std::string> ids;for(const auto& item:value["agents"]){
        const bool legacy=item.is_object()&&item.size()==3&&item.contains("id")&&item.contains("revision")&&item.contains("model_id");
        const bool named=item.is_object()&&item.size()==5&&item.contains("id")&&item.contains("revision")&&item.contains("model_id")&&item.contains("name")&&item.contains("description");
        const bool extended=item.is_object()&&item.contains("id")&&item.contains("revision")&&item.contains("model_id")&&item.contains("name")&&item.contains("description")&&item.contains("mode")&&item.contains("hidden")&&item.contains("default")&&item.contains("permissions");
        if((!legacy&&!named&&!extended)||!item["id"].is_string()||!item["revision"].is_number_integer()||!item["model_id"].is_string()||
           (named&&(!item["name"].is_string()||!item["description"].is_string())))throw std::runtime_error("Invalid native agent metadata");
        const auto id=item["id"].get<std::string>(),model=item["model_id"].get<std::string>();if(id.empty()||id.size()>64||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||!ids.insert(id).second||item["revision"]<1||item["revision"]>9007199254740991LL||model.size()>256||model.starts_with("sk-")||model.find('\0')!=std::string::npos)throw std::runtime_error("Invalid native agent metadata");
        if(named){const auto& name=item["name"].get_ref<const std::string&>();const auto& description=item["description"].get_ref<const std::string&>();if(name.empty()||name.size()>128||name.find('\0')!=std::string::npos||description.size()>2048||description.find('\0')!=std::string::npos)throw std::runtime_error("Invalid native agent display metadata");}
        if(extended){const auto& name=item["name"].get_ref<const std::string&>();const auto& description=item["description"].get_ref<const std::string&>();const auto& mode=item["mode"].get_ref<const std::string&>();
            if(name.empty()||name.size()>128||name.find('\0')!=std::string::npos||description.size()>2048||description.find('\0')!=std::string::npos||(mode!="all"&&mode!="primary"&&mode!="subagent")||!item["hidden"].is_boolean()||!item["default"].is_boolean()||!item["permissions"].is_array()||item["permissions"].size()>128||(item["default"].get<bool>()&&(item["hidden"].get<bool>()||mode=="subagent")))throw std::runtime_error("Invalid extended native agent metadata");
            for(const auto& rule:item["permissions"])if(!rule.is_object()||rule.size()!=3||!rule.contains("action")||!rule["action"].is_string()||!rule.contains("resource")||!rule["resource"].is_string()||!rule.contains("effect")||!rule["effect"].is_string()||(rule["effect"]!="deny"&&rule["effect"]!="allow"&&rule["effect"]!="ask"))throw std::runtime_error("Invalid native agent permission metadata");
        }
    }
}
void session_agent_snapshot(const nlohmann::json& value,const std::string& session){
    if(!value.is_object()||value.size()!=4||!value.contains("session_id")||value["session_id"]!=session||!value.contains("revision")||!value["revision"].is_number_integer()||value["revision"]<0||value["revision"]>9007199254740991LL||!value.contains("editable")||!value["editable"].is_boolean()||!value.contains("selected"))throw std::runtime_error("Invalid conversation agent selection");
    if(value["selected"].is_null())return;const auto& selected=value["selected"];
    const bool legacy=selected.is_object()&&selected.size()==3&&selected.contains("id")&&selected.contains("revision")&&selected.contains("model_id");
    const bool named=selected.is_object()&&selected.size()==5&&selected.contains("id")&&selected.contains("revision")&&selected.contains("model_id")&&selected.contains("name")&&selected.contains("description");
    const bool extended=selected.is_object()&&selected.contains("id")&&selected.contains("revision")&&selected.contains("model_id")&&selected.contains("name")&&selected.contains("description")&&selected.contains("mode")&&selected.contains("hidden")&&selected.contains("default")&&selected.contains("permissions");
    if((!legacy&&!named&&!extended)||!selected["id"].is_string()||!selected["revision"].is_number_integer()||!selected["model_id"].is_string()||
       (named&&(!selected["name"].is_string()||!selected["description"].is_string())))throw std::runtime_error("Invalid selected agent metadata");
    const auto id=selected["id"].get<std::string>(),model=selected["model_id"].get<std::string>();if(id.empty()||id.size()>64||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||selected["revision"]<1||selected["revision"]>9007199254740991LL||model.size()>256||model.starts_with("sk-")||model.find('\0')!=std::string::npos)throw std::runtime_error("Invalid selected agent metadata");
    if(named){const auto& name=selected["name"].get_ref<const std::string&>();const auto& description=selected["description"].get_ref<const std::string&>();if(name.empty()||name.size()>128||name.find('\0')!=std::string::npos||description.size()>2048||description.find('\0')!=std::string::npos)throw std::runtime_error("Invalid selected agent display metadata");}
    if(extended){const auto& name=selected["name"].get_ref<const std::string&>();const auto& description=selected["description"].get_ref<const std::string&>();const auto& mode=selected["mode"].get_ref<const std::string&>();
        if(name.empty()||name.size()>128||name.find('\0')!=std::string::npos||description.size()>2048||description.find('\0')!=std::string::npos||(mode!="all"&&mode!="primary"&&mode!="subagent")||!selected["hidden"].is_boolean()||!selected["default"].is_boolean()||!selected["permissions"].is_array()||selected["permissions"].size()>128||(selected["default"].get<bool>()&&(selected["hidden"].get<bool>()||mode=="subagent")))throw std::runtime_error("Invalid extended selected agent metadata");
        for(const auto& rule:selected["permissions"])if(!rule.is_object()||rule.size()!=3||!rule.contains("action")||!rule["action"].is_string()||!rule.contains("resource")||!rule["resource"].is_string()||!rule.contains("effect")||!rule["effect"].is_string()||(rule["effect"]!="deny"&&rule["effect"]!="allow"&&rule["effect"]!="ask"))throw std::runtime_error("Invalid selected agent permission metadata");
    }
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
struct PreviewLines { std::vector<std::string> lines; bool truncated{}; };
PreviewLines preview_lines(const std::string& content){
    constexpr std::size_t max_lines=40,max_bytes=8192,max_line_bytes=1024;
    PreviewLines result;std::size_t offset=0,bytes=0;
    while(offset<content.size()){
        if(result.lines.size()>=max_lines||bytes>=max_bytes){result.truncated=true;break;}
        const auto end=content.find('\n',offset);const auto length=(end==std::string::npos?content.size():end)-offset;
        const auto take=std::min(length,max_line_bytes);const auto line=content.substr(offset,take);
        const auto encoded=nlohmann::json(line).dump(-1,' ',false,nlohmann::json::error_handler_t::replace);
        if(bytes+encoded.size()>max_bytes){result.truncated=true;break;}
        result.lines.push_back(line);bytes+=encoded.size();
        if(take<length){result.truncated=true;break;}
        if(end==std::string::npos)break;offset=end+1;
    }
    return result;
}
void print_unified_preview(const std::string& path,const std::string& before,const std::string& after,bool before_exists,bool after_exists){
    constexpr std::size_t max_cells=41*41;const auto old_lines=preview_lines(before),new_lines=preview_lines(after);
    const auto n=old_lines.lines.size(),m=new_lines.lines.size();if((n+1)*(m+1)>max_cells)throw std::runtime_error("File preview exceeds the bounded diff size");
    std::vector<std::size_t> lcs((n+1)*(m+1));const auto at=[&](std::size_t i,std::size_t j)->std::size_t&{return lcs[i*(m+1)+j];};
    for(std::size_t i=n;i-->0;)for(std::size_t j=m;j-->0;)at(i,j)=old_lines.lines[i]==new_lines.lines[j]?1+at(i+1,j+1):std::max(at(i+1,j),at(i,j+1));
    const auto encoded_path=nlohmann::json(path).dump(-1,' ',false,nlohmann::json::error_handler_t::replace);
    std::cerr<<"  --- "<<(before_exists?"a/":"")<<encoded_path<<(before_exists?"":" (new file)")<<'\n';
    std::cerr<<"  +++ "<<(after_exists?"b/":"")<<encoded_path<<(after_exists?"":" (file deleted)")<<'\n';
    if(n||m)std::cerr<<"  @@ -"<<(n?"1":"0")<<","<<n<<" +"<<(m?"1":"0")<<","<<m<<" @@\n";
    std::size_t i=0,j=0;const auto print=[&](char marker,const std::string& line){std::cerr<<"  "<<marker<<" "<<nlohmann::json(line).dump(-1,' ',false,nlohmann::json::error_handler_t::replace)<<'\n';};
    while(i<n||j<m){
        if(i<n&&j<m&&old_lines.lines[i]==new_lines.lines[j]){print(' ',old_lines.lines[i]);++i;++j;}
        else if(i<n&&(j==m||at(i+1,j)>=at(i,j+1))){print('-',old_lines.lines[i]);++i;}
        else{print('+',new_lines.lines[j]);++j;}
    }
    if(!n&&!m)std::cerr<<"  (empty file)\n";
    if(old_lines.truncated||new_lines.truncated)std::cerr<<"  … diff preview truncated; the operation record above retains the exact approved bytes\n";
}
void print_operation_preview(const nlohmann::json& operation)noexcept{
    using Json=nlohmann::json;
    try{
        if(!operation.is_object()||!operation.contains("tool")||!operation["tool"].is_string()||!operation.contains("arguments_json")||!operation["arguments_json"].is_string())return;
        const auto tool=operation["tool"].get<std::string>();if(tool!="create_file"&&tool!="replace_file"&&tool!="patch_file")return;
        const auto plan=Json::parse(operation["arguments_json"].get<std::string>());if(!plan.is_object()||!plan.contains("path")||!plan["path"].is_string())return;
        const auto action=tool=="create_file"?std::string("add"):tool=="replace_file"?std::string("update"):plan.value("action",std::string("change"));
        std::cerr<<"\nFile change preview · "<<Json(plan["path"].get<std::string>()).dump()<<" · "<<action;
        if(plan.contains("patch_index")&&plan.contains("patch_file_count")&&plan["patch_index"].is_number_unsigned()&&plan["patch_file_count"].is_number_unsigned())std::cerr<<" · file "<<(plan["patch_index"].get<std::size_t>()+1)<<" of "<<plan["patch_file_count"].get<std::size_t>();
        if(action=="move"&&plan.contains("destination_path")&&plan["destination_path"].is_string())std::cerr<<" → "<<Json(plan["destination_path"].get<std::string>()).dump();
        std::cerr<<"\n";
        const bool before_exists=plan.value("before_exists",tool=="replace_file"||action!="add"),after_exists=plan.value("after_exists",action!="delete");
        const auto before=plan.value("before_content",std::string{}),after=plan.value("after_content",std::string{});
        print_unified_preview(plan["path"].get<std::string>(),before,after,before_exists,after_exists);
    }catch(...){std::cerr<<"\nFile change preview unavailable; inspect the exact operation record before deciding.\n";}
}
void print_process_preview(const nlohmann::json& operation)noexcept{
    using Json=nlohmann::json;
    try{
        if(!operation.is_object()||operation.value("tool",std::string{})!="run_process"||!operation.contains("arguments_json")||!operation["arguments_json"].is_string())return;
        const auto plan=Json::parse(operation["arguments_json"].get<std::string>());if(!plan.is_object())return;
        const auto executable=plan.value("executable",std::string{});const auto arguments=plan.value("arguments",Json::array());
        if(executable.empty()||!arguments.is_array())return;
        std::cerr<<"\nProcess approval preview · profile "<<Json(plan.value("profile_id",std::string{})).dump()<<"\n  Working directory: "<<Json(plan.value("workdir",std::string{})).dump()<<"\n  Command:";
        const auto quote=[](const std::string& value){return Json(value).dump();};std::cerr<<" "<<quote(executable);for(const auto& argument:arguments){if(!argument.is_string())return;std::cerr<<" "<<quote(argument.get<std::string>());}if(plan.contains("timeout_ms")&&plan["timeout_ms"].is_number_integer())std::cerr<<"\n  Timeout: "<<plan["timeout_ms"]<<" ms";std::cerr<<"\n";
        if(plan.contains("environment")&&plan["environment"].is_array()&&!plan["environment"].empty()){std::cerr<<"  Environment variables:";for(const auto& item:plan["environment"])if(item.is_object()&&item.contains("name")&&item["name"].is_string())std::cerr<<" "<<Json(item["name"].get<std::string>()).dump();std::cerr<<"\n";}
    }catch(...){std::cerr<<"\nProcess approval preview unavailable; review the recorded operation before deciding.\n";}
}
class ChatInput {
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<std::string> ready,deferred;
        std::size_t queued_bytes{},queued_lines{};
        bool eof{},stopped{};
        std::exception_ptr error;
    };
public:
    ChatInput():state_(std::make_shared<State>()),reader_([state=state_]{
        try {
            const auto enqueue=[&](std::string line){
                if(!line.empty()&&line.back()=='\r')line.pop_back();
                if(line.size()>1024*1024)throw std::invalid_argument("Chat line exceeds limits");
                std::unique_lock lock(state->mutex);
                state->changed.wait(lock,[&]{return state->stopped||(state->queued_lines<256&&state->queued_bytes+line.size()<=1024*1024);});
                if(state->stopped)return;
                state->queued_bytes+=line.size();++state->queued_lines;state->ready.push_back(std::move(line));
                state->changed.notify_all();
            };
#if defined(_WIN32)
            const auto handle=GetStdHandle(STD_INPUT_HANDLE);DWORD mode{};
            if(handle==INVALID_HANDLE_VALUE||handle==nullptr)throw std::runtime_error("Chat input is unavailable");
            if(GetConsoleMode(handle,&mode)){
                const auto enqueue_wide=[&](const std::wstring& value){const auto length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(length<0||length>1024*1024||(length==0&&!value.empty()))throw std::invalid_argument("Invalid or oversized UTF-16 chat input");std::string line(static_cast<std::size_t>(length),'\0');if(length&&WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),line.data(),length,nullptr,nullptr)!=length)throw std::invalid_argument("Invalid UTF-16 chat input");enqueue(std::move(line));};
                std::wstring pending;std::vector<wchar_t> buffer(32768);bool previous_cr=false;
                for(;;){DWORD count{};if(!ReadConsoleW(handle,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr))break;if(count==0)break;
                    for(DWORD index=0;index<count;++index){const auto character=buffer[index];if(character==L'\r'){enqueue_wide(pending);pending.clear();previous_cr=true;}else if(character==L'\n'){if(!previous_cr)enqueue_wide(pending);pending.clear();previous_cr=false;}else{previous_cr=false;pending.push_back(character);if(pending.size()>1024*1024)throw std::invalid_argument("Chat line exceeds limits");}}
                }
                if(!pending.empty())enqueue_wide(pending);
            }else{
                std::string pending;std::array<char,4096> buffer{};
                for(;;){DWORD count{};if(!ReadFile(handle,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)||count==0)break;std::vector<std::string> lines;
                    for(DWORD index=0;index<count;++index){if(buffer[index]=='\n'){lines.push_back(std::move(pending));pending.clear();}else{pending.push_back(buffer[index]);if(pending.size()>1024*1024)throw std::invalid_argument("Chat line exceeds limits");}}
                    DWORD available{};const bool pipe_closed=!PeekNamedPipe(handle,nullptr,0,nullptr,&available,nullptr)&&GetLastError()==ERROR_BROKEN_PIPE;
                    if(pipe_closed){std::lock_guard lock(state->mutex);state->eof=true;}
                    for(auto& line:lines)enqueue(std::move(line));
                    if(pipe_closed)break;
                }
                if(!pending.empty())enqueue(std::move(pending));
            }
#else
            std::string line;while(std::getline(std::cin,line))enqueue(std::move(line));
#endif
            std::lock_guard lock(state->mutex);state->eof=true;state->changed.notify_all();
        } catch(...) {
            std::lock_guard lock(state->mutex);state->error=std::current_exception();state->eof=true;state->changed.notify_all();
        }
    }){}
    ChatInput(const ChatInput&)=delete;ChatInput& operator=(const ChatInput&)=delete;
    ~ChatInput(){
        {std::lock_guard lock(state_->mutex);state_->stopped=true;state_->changed.notify_all();}
        // std::getline may be waiting for the next terminal line. The reader
        // owns shared state, so it can safely finish after this chat returns.
        if(reader_.joinable())reader_.detach();
    }
    bool read_line(std::string& line){
        auto state=state_;std::unique_lock lock(state->mutex);
        state->changed.wait(lock,[&]{return state->stopped||state->error||!state->deferred.empty()||!state->ready.empty()||state->eof;});
        if(state->error)std::rethrow_exception(state->error);
        if(!state->deferred.empty()){line=std::move(state->deferred.front());state->deferred.pop_front();}
        else if(!state->ready.empty()){line=std::move(state->ready.front());state->ready.pop_front();}
        else return false;
        state->queued_bytes-=line.size();--state->queued_lines;state->changed.notify_all();return true;
    }
    bool source_closed()const{
        std::lock_guard lock(state_->mutex);return state_->eof;
    }
    std::optional<std::string> control_line(){
        auto state=state_;std::lock_guard lock(state->mutex);
        if(state->error)std::rethrow_exception(state->error);
        // Closed input is a complete scripted conversation; leave its queued
        // commands for normal processing after the current turn.
        if(state->eof)return std::nullopt;
        if(state->ready.empty())return std::nullopt;
        auto line=std::move(state->ready.front());state->ready.pop_front();
        if(line=="/cancel"||line=="/exit"){
            state->queued_bytes-=line.size();--state->queued_lines;state->changed.notify_all();return line;
        }
        if(!line.empty()&&line.front()=='/'&&!line.starts_with("//")){
            state->deferred.push_back(std::move(line));state->changed.notify_all();return std::nullopt;
        }
        state->queued_bytes-=line.size();--state->queued_lines;state->changed.notify_all();
        if(line.starts_with("//"))line.erase(0,1);
        return line;
    }
private:
    std::shared_ptr<State> state_;
    std::thread reader_;
};
std::string queued_input_id(){
    static constexpr char digits[]="0123456789abcdef";std::random_device random;std::string result="inbox_";result.reserve(38);
    for(int i=0;i<16;++i){const auto byte=static_cast<unsigned char>(random());result.push_back(digits[byte>>4]);result.push_back(digits[byte&15]);}
    return result;
}
constexpr int run_detached=3;
int watch_run(agentflow::ConsoleTransport& client,const httplib::Headers& headers,const std::string& run,std::int64_t cursor,bool graph=false,bool interactive=false,ChatInput* input=nullptr) {
    using Json=nlohmann::json;const bool human=human_console_output();const auto path="/v1/runs/"+run;
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
            if(!human){std::cout<<event.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Run observation output is unavailable");}
            else {
                const auto& kind=event["kind"].get_ref<const std::string&>();const auto& data=event["data"];
                if(kind=="model.text"||kind=="model.refusal"){
                    if(data.is_object()&&data.contains("text")&&data["text"].is_string()){std::cout<<terminal_safe_text(data["text"].get_ref<const std::string&>())<<std::flush;if(!std::cout)throw std::runtime_error("Run response output is unavailable");}
                }else if(kind=="tool.started"){
                    if(data.is_object()&&data.contains("name")&&data["name"].is_string())std::cerr<<"\nUsing "<<terminal_safe_text(data["name"].get_ref<const std::string&>())<<"…\n";
                }else if(kind=="tool.completed"||kind=="tool.failed"){
                    if(data.is_object()&&data.contains("name")&&data["name"].is_string())std::cerr<<(kind=="tool.completed"?"Finished ":"Failed ")<<terminal_safe_text(data["name"].get_ref<const std::string&>())<<".\n";
                }else if(kind=="model.usage"&&data.is_object()){
                    bool first=true;const auto counter=[&](const Json& source,const char* field)->std::optional<std::int64_t>{if(source.contains(field)&&source[field].is_number_integer()&&source[field]>=0)return source[field].get<std::int64_t>();return std::nullopt;};
                    const auto input=counter(data,"input_tokens").has_value()?counter(data,"input_tokens"):counter(data,"prompt_tokens");const auto output=counter(data,"output_tokens").has_value()?counter(data,"output_tokens"):counter(data,"completion_tokens");
                    const auto show=[&](const char* label,const std::optional<std::int64_t>& value){if(value){std::cerr<<(first?"\n":" · ")<<label<<" "<<*value;first=false;}};show("Input",input);show("Output",output);show("Total",counter(data,"total_tokens"));
                    auto cached=counter(data,"cached_tokens");if(!cached&&data.contains("prompt_tokens_details")&&data["prompt_tokens_details"].is_object())cached=counter(data["prompt_tokens_details"],"cached_tokens");if(!cached&&data.contains("input_tokens_details")&&data["input_tokens_details"].is_object())cached=counter(data["input_tokens_details"],"cached_tokens");show("Cached",cached);if(!first)std::cerr<<" tokens\n";
                }else if(kind=="run.failed"){
                    std::cerr<<"\nRun failed";if(data.is_object()&&data.contains("reason")&&data["reason"].is_string())std::cerr<<": "<<terminal_safe_text(data["reason"].get_ref<const std::string&>());std::cerr<<"\n";
                }
            }
            cursor=event["seq"].get<std::int64_t>();
        }
        if(!tree||events.size()<256)break;}
    };
    bool cancellation_requested=false;
    for(;;) {
        emit();const auto state=read(path);
        if(!state.is_object() || state.value("id",std::string{})!=run || !state.contains("state") || !state["state"].is_string())throw std::runtime_error("Invalid backend run identity or state");
        if(graph && (state.value("graph_root",false)!=true || state.value("session_id",std::string{})!=session))throw std::runtime_error("Graph observation identity changed");
        const auto value=state["state"].get<std::string>();
        if(value=="completed" || value=="failed" || value=="cancelled") {
            // A transition can commit between the event query and status query.
            emit();if(human)std::cerr<<(value=="completed"?"\nRun completed.\n":value=="cancelled"?"\nRun cancelled.\n":"\nRun failed.\n");return value=="completed"?0:value=="cancelled"?2:1;
        }
        if(value!="queued" && value!="running" && value!="paused")throw std::runtime_error("Unknown backend run state");
        if(interactive){
            if(input){
                const auto control=input->control_line();
                if(control&&*control=="/exit"){
                    std::cerr<<"Detached from run "<<run<<"; backend execution continues.\n";
                    return run_detached;
                }
                if(control&&*control=="/cancel"){
                    if(!cancellation_requested){
                        const auto response=client.Post(path+"/cancel",headers,Json::object().dump(),"application/json");
                        if(!response)throw std::runtime_error("Cannot reach xMind Server to cancel the active run");
                        if(response->status<200||response->status>=300)std::cerr<<"Backend rejected cancellation (HTTP "<<response->status<<"). Refreshing run state.\n";
                        else {std::cout<<Json{{"type","run_cancel_result"},{"result",Json::parse(response->body)}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Cancellation result output is unavailable");cancellation_requested=true;}
                    }
                    continue; // Re-read durable state; never ask for new approval after cancellation.
                }
                if(control&&!control->empty()&&control->find_first_not_of(" \t\r\n")!=std::string::npos){
                    const auto inbox_id=queued_input_id();const Json queued={{"id",inbox_id},{"content",*control}};
                    const auto response=client.Post("/v1/sessions/"+session+"/inbox",headers,queued.dump(),"application/json");
                    if(!response)throw std::runtime_error("Cannot reach xMind Server while saving queued chat input; inspect the session inbox before resubmitting");
                    if(response->status==202){
                        const auto item=Json::parse(response->body);if(!item.is_object()||item.value("id",std::string{})!=inbox_id||item.value("session_id",std::string{})!=session||item.value("content",std::string{})!=*control||!item.contains("state")||!item["state"].is_string()||(item["state"]!="queued"&&item["state"]!="claimed")||!item.contains("created_ms")||!item["created_ms"].is_number_integer())throw std::runtime_error("Invalid durable queued-input acknowledgement");
                        std::cout<<Json{{"type","prompt_queued"},{"item",item}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Queued-input output is unavailable");
                    }else{
                        std::cout<<Json{{"type","prompt_queue_rejected"},{"session_id",session},{"http_status",response->status},{"content",*control}}.dump()<<'\n'<<std::flush;
                        std::cerr<<"Backend did not save the queued prompt (HTTP "<<response->status<<"); the prompt is retained above.\n";
                    }
                    continue;
                }
            }
            auto operations=Json::array();const auto owned=owners();
            for(const auto& owner:owned){const auto batch=read("/v1/runs/"+owner+"/operations");if(!batch.is_array())throw std::runtime_error("Invalid backend operation list");for(const auto& operation:batch){if(!operation.is_object()||operation.value("run_id",std::string{})!=owner)throw std::runtime_error("Invalid operation ownership");operations.push_back(operation);}}
            bool interacted=false;
            for(const auto& operation:operations){
                if(!operation.is_object() || !owned.contains(operation.value("run_id",std::string{})))throw std::runtime_error("Invalid operation ownership");
                if(operation.value("state",std::string{})!="awaiting_approval")continue;
                const auto operationId=operation.value("id",std::string{});
                if(operationId.empty() || operationId.size()>128 || operationId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid approval identity");
                // Structured output retains the exact proposal; human output
                // renders bounded previews. The backend still revalidates the
                // recorded operation before applying the decision.
                if(!human){std::cout<<Json{{"type","operation_review"},{"operation",operation}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Approval review output is unavailable");}
                print_operation_preview(operation);print_process_preview(operation);
                std::cerr<<(human?"Review the operation above. ":"Review the exact operation and any preview above. ")<<"Enter /allow "<<operationId<<", /deny "<<operationId<<", /cancel or /exit to detach.\n";
                std::string decision;
                if(!input||!input->read_line(decision))throw std::runtime_error("CLI detached before a decision; backend execution remains owned by the server");
                if(decision=="/exit"){std::cerr<<"Detached from run "<<run<<"; backend execution continues.\n";return run_detached;}
                std::string route;Json body;
                if(decision=="/cancel"){route=path+"/cancel";body=Json::object();}
                else if(decision=="/allow "+operationId || decision=="/deny "+operationId){route="/v1/operations/"+operationId+"/decision";body={{"decision",decision.starts_with("/allow ")?"allow":"deny"}};}
                else {std::cerr<<"No decision sent. Use an exact displayed operation ID.\n";break;}
                const auto response=client.Post(route,headers,body.dump(),"application/json");
                if(!response)throw std::runtime_error("Cannot reach xMind Server for approval");
                if(response->status<200 || response->status>=300)std::cerr<<"Backend rejected the decision (HTTP "<<response->status<<"). Refreshing recorded state.\n";
                else if(human)std::cerr<<(decision.starts_with("/allow ")?"Approval recorded.\n":"Operation denied.\n");
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
                if(!snapshot.at("plan").is_null()){
                    const auto review=agentflow::cli::inspect_plan_human_review(snapshot,run,session);
                    const auto& plan=snapshot.at("plan");const auto revision=plan_revision(plan.at("revision").dump()),sequence=plan_revision(plan.at("state_sequence").dump());
                    if(!review.questions.empty()||review.resume_available){
                        std::cout<<Json{{"type","plan_human_review"},{"snapshot",snapshot},{"questions",review.questions}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Plan review output is unavailable");
                        std::cerr<<"Enter /input REQUEST_ID JSON for a displayed question"<<(review.resume_available?", /resume":"")<<", /cancel or /exit to detach.\n";
                        std::string answer;if(!input||!input->read_line(answer))throw std::runtime_error("CLI detached before plan input; backend execution remains owned by the server");if(answer=="/exit"){std::cerr<<"Detached from run "<<run<<"; backend execution continues.\n";return run_detached;}
                        std::string route;Json input;
                        if(answer=="/cancel"){route=path+"/cancel";input=Json::object();}
                        else if(answer=="/resume"&&review.resume_available){route=path+"/plan/resume";input={{"expected_revision",revision},{"expected_state_sequence",sequence}};}
                        else if(answer.starts_with("/input ")){const auto split=answer.find(' ',7);const auto request=split==std::string::npos?std::string{}:answer.substr(7,split-7),raw=split==std::string::npos?std::string{}:answer.substr(split+1);const auto target=review.waiting.find(request);if(target==review.waiting.end()){std::cerr<<"No input sent. Use an exact displayed question ID.\n";continue;}try{validate_plan_input(raw);}catch(...){std::cerr<<"No input sent. Use a bounded strict JSON object.\n";continue;}route=path+"/plan/human/"+request;input={{"input_json",raw},{"expected_revision",target->second.revision},{"expected_state_sequence",target->second.state_sequence}};}
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
                    std::string answer;if(!input||!input->read_line(answer))throw std::runtime_error("CLI detached before graph input; backend execution remains owned by the server");if(answer=="/exit"){std::cerr<<"Detached from run "<<run<<"; backend execution continues.\n";return run_detached;}
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
    using Json=nlohmann::json;const bool human=human_console_output();
    auto request=[&](const std::string& path,const Json* body=nullptr){
        auto response=body?client.Post(path,headers,body->dump(),"application/json"):client.Get(path,headers);
        if(!response)throw std::runtime_error("Cannot reach xMind Server during chat");
        if(response->status<200 || response->status>=300)throw std::runtime_error("Server rejected chat request (HTTP "+std::to_string(response->status)+")");
        return Json::parse(response->body);
    };
    nlohmann::json observed_skills;
    const auto health=request("/v1/health");if(!health.is_object() || !health.contains("agent_execution") || !health["agent_execution"].is_boolean())throw std::runtime_error("Invalid backend chat capabilities");
    const bool agent_selection=health.value("agent_selection",false);
    const bool provider_profile_admission=health.value("provider_profile_admission",false);
    const bool session_inbox_dispatch=health.value("session_inbox_dispatch",false);
    const bool profile_admission=provider_profile_admission||health.value("graph_provider_profile_admission",false);
    const bool session_model_selection=health.value("session_model_selection",false);
    auto provider_binding=profile_admission?provider_admission_binding(request("/v1/provider/profiles")):Json::object();
    auto active_provider_route=[&](const Json& metadata){
        const auto binding=provider_admission_binding(metadata);if(binding.at("provider_profile_id").get<std::string>().empty())return std::string{};
        const auto active_id=binding.at("provider_profile_id").get<std::string>();for(const auto& profile:metadata.at("profiles"))if(profile.is_object()&&profile.value("id",std::string{})==active_id){
            if(!profile.contains("route_id")||!profile.at("route_id").is_string())throw std::runtime_error("Invalid active provider route metadata");
            return provider_profile_identity(profile.at("route_id").get<std::string>());
        }
        throw std::runtime_error("Active provider route is missing");
    };
    auto validate_session_model=[&](const Json& value,const std::string& expected_session){
        if(!value.is_object()||value.size()!=4||value.value("session_id",std::string{})!=expected_session||!value.contains("revision")||!value.at("revision").is_number_integer()||value.at("revision")<0||value.at("revision")>9007199254740991LL||!value.contains("editable")||!value.at("editable").is_boolean()||!value.contains("selected"))throw std::runtime_error("Invalid backend session model state");
        if(!value.at("selected").is_null()){
            const auto& selected=value.at("selected");if(!selected.is_object()||selected.size()!=3||!selected.contains("model_id")||!selected.at("model_id").is_string()||!selected.contains("provider_profile_id")||!selected.at("provider_profile_id").is_string()||!selected.contains("provider_route_id")||!selected.at("provider_route_id").is_string())throw std::runtime_error("Invalid backend session model selection");
            provider_profile_identity(selected.at("model_id").get<std::string>());provider_profile_identity(selected.at("provider_profile_id").get<std::string>());provider_profile_identity(selected.at("provider_route_id").get<std::string>());
        }
        return value;
    };
    auto read_session_model=[&](const std::string& selected_session){
        if(!session_model_selection||selected_session.empty())return Json();
        return validate_session_model(request("/v1/sessions/"+selected_session+"/model"),selected_session);
    };
    auto model_for_active_profile=[&](const Json& state){
        if(state.is_null()||!state.contains("selected")||state.at("selected").is_null()||provider_binding.value("provider_profile_id",std::string{}).empty())return std::string{};
        const auto& selected=state.at("selected");const auto metadata=request("/v1/provider/profiles");
        if(selected.at("provider_profile_id").get<std::string>()==provider_binding.at("provider_profile_id").get<std::string>()&&selected.at("provider_route_id").get<std::string>()==active_provider_route(metadata))return selected.at("model_id").get<std::string>();
        return std::string{};
    };
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
    auto session_model_state=read_session_model(session);if(model.empty())model=model_for_active_profile(session_model_state);
    ChatInput input;
    std::cerr<<"xMind chat: enter a request, /help for commands, /exit to leave. Backend runs survive disconnect.\n";
    if(!session.empty()){
        if(human)print_human_history(session,history);else std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<Json{{"type","history"},{"session_id",session},{"history",history}}.dump()<<'\n'<<std::flush;
        if(!std::cout)throw std::runtime_error("Chat history output is unavailable");
    }
    int last_result=0,profile_result=0,skill_result=0,agent_result=0,mcp_result=0;std::string prompt;
    const auto exit_status=[&]{return last_result!=0?last_result:profile_result!=0?profile_result:skill_result!=0?skill_result:agent_result!=0?agent_result:mcp_result;};
    auto drain_inbox=[&](){
        if(session.empty())return false;
        for(;;){
            const auto snapshot=request("/v1/sessions/"+session+"/inbox");
            if(!snapshot.is_object()||snapshot.size()!=1||!snapshot.contains("items")||!snapshot["items"].is_array()||snapshot["items"].size()>128)throw std::runtime_error("Invalid session prompt inbox");
            const Json* next=nullptr;
            for(const auto& item:snapshot["items"]){
                if(!item.is_object()||!item.contains("id")||!item["id"].is_string()||item.value("session_id",std::string{})!=session||!item.contains("content")||!item["content"].is_string()||item["content"].get_ref<const std::string&>().empty()||!item.contains("state")||!item["state"].is_string()||(item["state"]!="queued"&&item["state"]!="claimed")||!item.contains("created_ms")||!item["created_ms"].is_number_integer())throw std::runtime_error("Invalid session prompt inbox item");
                if(item["state"]=="queued"&&!next)next=&item;
            }
            if(!next)return false;
            const auto inbox_id=next->at("id").get<std::string>(),queued_prompt=next->at("content").get<std::string>();
            if(session_inbox_dispatch){
                for(;;){
                    if(const auto control=input.control_line()){
                        if(*control=="/exit"){std::cerr<<"Detached while xMind Server dispatches the queued prompt; backend work remains owned by the server.\n";return true;}
                        if(*control=="/cancel"){
                            const auto cancelled=client.Delete("/v1/sessions/"+session+"/inbox/"+inbox_id,headers);
                            if(!cancelled)throw std::runtime_error("Cannot reach xMind Server to cancel the queued prompt");
                            std::cout<<Json{{"type",cancelled->status==204?"prompt_cancelled":"prompt_cancel_rejected"},{"session_id",session},{"inbox_id",inbox_id},{"http_status",cancelled->status}}.dump()<<'\n'<<std::flush;
                            if(!std::cout)throw std::runtime_error("Queued cancellation output is unavailable");
                            if(cancelled->status!=204)std::cerr<<"The queued prompt was already claimed or could not be cancelled (HTTP "<<cancelled->status<<").\n";
                            if(cancelled->status==409)continue; // Re-read the row and attach if native dispatch won the race.
                            break;
                        }
                        if(!control->empty()&&control->find_first_not_of(" \t\r\n")!=std::string::npos){
                            const auto additional_id=queued_input_id();const Json queued={{"id",additional_id},{"content",*control}};
                            const auto response=client.Post("/v1/sessions/"+session+"/inbox",headers,queued.dump(),"application/json");
                            if(!response)throw std::runtime_error("Cannot reach xMind Server while saving queued chat input; inspect the session inbox before resubmitting");
                            if(response->status==202){
                                const auto item=Json::parse(response->body);if(!item.is_object()||item.value("id",std::string{})!=additional_id||item.value("session_id",std::string{})!=session||item.value("content",std::string{})!=*control||!item.contains("state")||!item["state"].is_string()||(item["state"]!="queued"&&item["state"]!="claimed")||!item.contains("created_ms")||!item["created_ms"].is_number_integer())throw std::runtime_error("Invalid durable queued-input acknowledgement");
                                std::cout<<Json{{"type","prompt_queued"},{"item",item}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Queued-input output is unavailable");
                            }else{
                                std::cout<<Json{{"type","prompt_queue_rejected"},{"session_id",session},{"http_status",response->status},{"content",*control}}.dump()<<'\n'<<std::flush;
                                std::cerr<<"Backend did not save the queued prompt (HTTP "<<response->status<<"); the prompt is retained above.\n";
                            }
                        }
                    }
                    const auto current=request("/v1/sessions/"+session+"/inbox");
                    if(!current.is_object()||current.size()!=1||!current.contains("items")||!current["items"].is_array()||current["items"].size()>128)throw std::runtime_error("Invalid session prompt inbox");
                    const Json* observed=nullptr;
                    for(const auto& item:current["items"])if(item.is_object()&&item.value("id",std::string{})==inbox_id){observed=&item;break;}
                    if(!observed)break; // Another client cancelled this queued item.
                    if(observed->value("session_id",std::string{})!=session||observed->value("content",std::string{})!=queued_prompt||!observed->contains("state")||!observed->at("state").is_string()||((*observed)["state"]!="queued"&&(*observed)["state"]!="claimed"))throw std::runtime_error("Queued prompt changed while awaiting native dispatch");
                    if((*observed)["state"]=="claimed"){
                        if(!observed->contains("run_id")||!(*observed)["run_id"].is_string())throw std::runtime_error("Claimed prompt has no durable run identity");
                        const auto id=(*observed)["run_id"].get<std::string>();if(id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid queued run identity");
                        const auto run=request("/v1/runs/"+id);
                        if(!run.is_object()||run.value("id",std::string{})!=id||run.value("session_id",std::string{})!=session||run.value("graph_root",false)||!run.value("parent_id",std::string{}).empty())throw std::runtime_error("Claimed prompt run does not belong to this root session");
                        std::cout<<Json{{"type","queued_run"},{"inbox_id",inbox_id},{"run",run}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Queued-run output is unavailable");
                        const auto watched=watch_run(client,headers,id,0,false,true,&input);if(watched==run_detached)return true;last_result=watched;profile_result=0;
                        std::cout<<Json{{"type","turn_finished"},{"run_id",id},{"exit_status",last_result},{"queued",true}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Queued turn output is unavailable");
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
                continue;
            }
            Json body={{"session_id",session},{"prompt",queued_prompt},{"inbox_id",inbox_id}};if(!model.empty())body["model_id"]=model;if(provider_profile_admission&&!provider_binding.value("provider_profile_id",std::string{}).empty())body.update(provider_binding);
            const auto response=client.Post("/v1/runs",headers,body.dump(),"application/json");
            if(!response||response->status!=202){
                const auto code=response?response->status:0;
                std::cout<<Json{{"type","prompt_dispatch_deferred"},{"session_id",session},{"inbox_id",inbox_id},{"http_status",code}}.dump()<<'\n'<<std::flush;
                std::cerr<<"Queued prompt remains saved; backend admission was not confirmed. Refresh /inbox before retrying.\n";return false;
            }
            const auto run=Json::parse(response->body);
            if(!run.is_object()||run.value("session_id",std::string{})!=session||run.value("graph_root",false)||!run.contains("id")||!run["id"].is_string())throw std::runtime_error("Invalid queued run admission response");
            const auto id=run["id"].get<std::string>();if(id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid queued run identity");
            std::cout<<Json{{"type","queued_run"},{"inbox_id",inbox_id},{"run",run}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Queued run output is unavailable");
            const auto watched=watch_run(client,headers,id,0,false,true,&input);if(watched==run_detached)return true;last_result=watched;profile_result=0;
            std::cout<<Json{{"type","turn_finished"},{"run_id",id},{"exit_status",last_result},{"queued",true}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Queued turn output is unavailable");
        }
    };
    if(drain_inbox())return exit_status();
    while(std::cerr<<"xMind > "<<std::flush,input.read_line(prompt)) {
        if(prompt=="/exit")return exit_status();
        if(prompt.find_first_not_of(" \t\r\n")==std::string::npos)continue;
        if(prompt=="/help"){
            std::cerr<<"MCP sign-in: /mcp lists configured OAuth status; /mcp-login SERVER signs in and /mcp-refresh SERVER renews an existing grant using current native revisions. /mcp-status REQUEST, /mcp-open REQUEST, /mcp-cancel REQUEST and /mcp-watch REQUEST inspect, open, cancel or observe that backend-owned attempt. Closing chat only detaches.\n";
            std::cerr<<"/compose [GRAPH_ID] collects a multiline request until /send; /discard cancels it. Use //send or //discard for those literal lines. Other slash lines remain request text. Unfinished EOF and blocks over 1 MiB are discarded without submission.\n";
            std::cerr<<"Context controls: context SESSION [MODEL], compact-context SESSION HEAD_REV REQUEST_ID [MODEL], context-request SESSION REQUEST_ID [MODEL]. Resume a ready closed graph with resume-graph ROOT CHECKPOINT_REV.\n";
            std::cerr<<"/runs lists recorded root runs in the selected conversation; use /watch or /graph-watch to attach one.\n";
            std::cerr<<"During an active turn, /cancel requests backend cancellation and /exit detaches while the run continues. Prompt text is saved in the session inbox and starts after the active turn; slash commands wait until it finishes.\n";
            std::cerr<<"Agent /watch includes actual owned inspection or coding children and native plan questions when supported. Direct commands: children RUN, child-history PARENT CHILD, tree-events RUN [CURSOR], delegation, planning, inspect-plan RUN.\n";
            std::cerr<<"/graphs lists registered backend graphs; /graph GRAPH_ID REQUEST starts one at its displayed catalog revision.\n";
            std::cerr<<"/commands lists workspace Markdown commands, invoked as /command-id arguments.\n";
            std::cerr<<"/watch RUN_ID attaches an existing single-agent run; /graph-watch ROOT_ID attaches a graph with explicit input/approvals. Neither submits another run.\n";
            std::cerr<<"/profiles lists saved provider metadata without changing this chat's admission binding; /profile ID REVISION explicitly selects a shared profile and clears this chat's model override.\n";
            std::cerr<<"/agents lists named agent definitions; /agent shows this conversation's selection; /agent ID selects one and /agent default clears it. Selection is revision-checked and locked while a run is active.\n";
            std::cerr<<"/skills lists current workspace guide metadata without activating a guide.\n/session-skills inspects current attachments; /attach-skill ID and /remove-skill ID change exact catalogue ids (spaces are part of the id); /clear-skills clears the observed selection. Native revisions prevent stale writes.\n/models lists backend-enabled models; /provider-models discovers account models using the saved provider key; /model ID saves a model for this conversation; /model clears the override and returns to the active provider default.\n/sessions lists saved conversations; /session ID resumes one; /new starts an empty conversation on your next request.\n/title NAME renames the selected conversation; /history displays its saved messages; /exit leaves. Prefix a literal slash request with another slash.\n";continue;
        }
        if(prompt=="/agents"||prompt=="/agent"||prompt.starts_with("/agent ")){
            if(!agent_selection){agent_result=1;std::cerr<<"This backend does not support named agent selection. Update the native backend.\n";continue;}
            try{
                const auto catalogue=request("/v1/agents");agent_catalogue(catalogue);
                if(prompt=="/agents"){
                    std::cout<<Json{{"type","agents"},{"catalogue",catalogue}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Agent catalogue output unavailable");agent_result=0;continue;
                }
                if(prompt=="/agent"&&session.empty()){agent_result=1;std::cerr<<"No conversation is selected. Use /sessions and /session ID, or /agent ID to start an agent conversation.\n";continue;}
                const auto selected_id=prompt=="/agent"?std::string{}:prompt.substr(7);Json agent_id=nullptr,expected_agent=nullptr;std::int64_t definition_revision=0;
                if(prompt!="/agent"&&selected_id.empty())throw std::invalid_argument("Empty named agent identity");
                if(selected_id!="default"){
                    if(!selected_id.empty()){
                        if(selected_id.size()>64||selected_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid named agent identity");
                        const auto found=std::find_if(catalogue["agents"].begin(),catalogue["agents"].end(),[&](const Json& value){return value["id"]==selected_id;});if(found==catalogue["agents"].end()){agent_result=1;std::cerr<<"Agent was not found. Use /agents to see current IDs.\n";continue;}expected_agent=*found;agent_id=selected_id;definition_revision=expected_agent["revision"].get<std::int64_t>();
                    }
                }
                if(session.empty()){
                    if(agent_id.is_null()){agent_result=1;std::cerr<<"No conversation is selected to clear. Use /agent ID to start an agent conversation.\n";continue;}
                    const Json creation={{"title","New conversation"}};const auto created=request("/v1/sessions",&creation);if(!created.is_object()||!created.contains("id")||!created["id"].is_string())throw std::runtime_error("Invalid new conversation response");session=created["id"].get<std::string>();if(session.empty()||session.size()>128||session.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid new conversation identity");
                    std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<Json{{"type","history"},{"session_id",session},{"history",Json::array()}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("New agent conversation output unavailable");
                }
                const auto path="/v1/sessions/"+session+"/agent";auto observed=request(path);session_agent_snapshot(observed,session);
                if(prompt=="/agent"){
                    std::cout<<Json{{"type","agent_selection"},{"catalogue",catalogue},{"selection",observed}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Agent selection output unavailable");agent_result=0;continue;
                }
                if(!observed["editable"].get<bool>()){agent_result=1;std::cerr<<"This conversation's agent is locked while a run is active. Wait for it to finish, then use /agent again.\n";continue;}
                const Json body={{"agent_id",agent_id},{"agent_revision",definition_revision},{"expected_revision",observed["revision"]}};const auto response=client.Post(path,headers,body.dump(),"application/json");if(!response)throw std::runtime_error("Cannot reach xMind Server during agent selection");
                if(response->status==400||response->status==403||response->status==409){agent_result=1;std::cerr<<"Agent selection rejected (HTTP "<<response->status<<"). Use /agent to refresh before retrying; no write was replayed.\n";continue;}
                if(response->status!=200)throw std::runtime_error("Server rejected agent selection");const auto acknowledged=Json::parse(response->body);session_agent_snapshot(acknowledged,session);
                const bool selection_matches=agent_id.is_null()?acknowledged["selected"].is_null():acknowledged["selected"].is_object()&&acknowledged["selected"]["id"]==agent_id&&acknowledged["selected"]["revision"]==definition_revision&&acknowledged["selected"]["model_id"]==expected_agent["model_id"];
                if(!acknowledged["editable"].get<bool>()||acknowledged["revision"]!=observed["revision"].get<std::int64_t>()+1||!selection_matches)throw std::runtime_error("Backend agent acknowledgement changed selection or revision");
                std::cout<<Json{{"type","agent_selection"},{"catalogue",catalogue},{"selection",acknowledged}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Agent selection output unavailable");agent_result=0;
            }catch(const std::invalid_argument&){agent_result=1;std::cerr<<"Use /agents, /agent or /agent ID|default.\n";}catch(const std::exception& error){agent_result=1;std::cerr<<error.what()<<'\n';}
            continue;
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
            const auto watched=watch_run(client,headers,id,0,graphAttachment,true,&input);if(watched==run_detached)return exit_status();last_result=watched;profile_result=0;
            std::cout<<Json{{"type","turn_finished"},{"run_id",id},{"exit_status",last_result}}.dump()<<'\n'<<std::flush;
            if(drain_inbox())return exit_status();
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
            session.clear();model.clear();session_model_state=Json();std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<Json{{"type","history"},{"session_id",session},{"history",Json::array()}}.dump()<<'\n'<<std::flush;continue;
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
            session=selected;session_model_state=read_session_model(session);model=model_for_active_profile(session_model_state);std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<Json{{"type","history"},{"session_id",session},{"history",selectedHistory}}.dump()<<'\n'<<std::flush;continue;
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
                if(!session_model_selection){
                    const auto selected=prompt=="/model"?std::string{}:prompt.substr(7);bool available=selected.empty();for(const auto& item:catalogue["models"])if(item.is_object()&&item.value("id",std::string{})==selected)available=true;
                    if(!available){std::cerr<<"Model is not enabled by this backend. Use /models to see available IDs.\n";continue;}model=selected;
                    std::cout<<Json{{"type","model"},{"model_id",model},{"default_model",catalogue["default_model"]}}.dump()<<'\n'<<std::flush;continue;
                }
                if(!profile_admission||provider_binding.value("provider_profile_id",std::string{}).empty()){std::cerr<<"Select an active provider profile before choosing a conversation model. Use /profiles and /profile ID REVISION.\n";profile_result=1;continue;}
                if(session.empty()){
                    const Json create_body={{"title","New conversation"}};const auto created=request("/v1/sessions",&create_body);
                    if(!created.is_object()||!created.contains("id")||!created.at("id").is_string())throw std::runtime_error("Invalid created model-selection conversation");session=created.at("id").get<std::string>();
                    if(session.empty()||session.size()>128||session.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid created conversation identity");
                    std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<std::flush;session_model_state=read_session_model(session);
                }
                const bool reset_to_default=prompt=="/model";std::optional<std::string> selected;if(!reset_to_default)selected=prompt.substr(7);
                const auto metadata=request("/v1/provider/profiles");(void)provider_admission_binding(metadata);
                std::string route_id,default_model;for(const auto& profile:metadata.at("profiles"))if(profile.is_object()&&profile.value("id",std::string{})==provider_binding.at("provider_profile_id").get<std::string>()){
                    if(!profile.contains("route_id")||!profile.at("route_id").is_string()||!profile.contains("model")||!profile.at("model").is_string())throw std::runtime_error("Invalid active provider model metadata");route_id=provider_profile_identity(profile.at("route_id").get<std::string>());const auto configured_model=profile.at("model").get<std::string>();if(!configured_model.empty())default_model=provider_profile_identity(configured_model);break;
                }
                if(route_id.empty()||route_id!=active_provider_route(metadata))throw std::runtime_error("Active provider route is invalid");if(selected&&selected->empty())throw std::invalid_argument("Model ID cannot be empty");if(selected)provider_profile_identity(*selected);else if(default_model.empty()){std::cerr<<"The active provider profile has no default model. Choose a model with /model MODEL_ID or configure its default in Settings.\n";profile_result=1;continue;}
                if(!session_model_state.is_object())session_model_state=read_session_model(session);
                if(!session_model_state.at("editable").get<bool>()){std::cerr<<"Conversation model is locked while a run is active. Wait for it to finish and retry.\n";profile_result=1;continue;}
                const auto revision=session_model_state.at("revision").get<std::int64_t>();if(revision>=9007199254740991LL)throw std::runtime_error("Conversation model revision is exhausted");
                const Json body={{"model_id",selected?Json(*selected):Json(nullptr)},{"provider_profile_id",provider_binding.at("provider_profile_id")},{"expected_provider_revision",provider_binding.at("expected_provider_revision")},{"expected_revision",revision}};
                const auto response=client.Post("/v1/sessions/"+session+"/model",headers,body.dump(),"application/json");if(!response)throw std::runtime_error("Cannot reach xMind Server during model selection; outcome is unknown. Refresh /session ID before selecting again.");
                if(response->status==409){profile_result=1;session_model_state=read_session_model(session);model=model_for_active_profile(session_model_state);std::cerr<<"Conversation model changed or is locked (HTTP 409). Refresh the session and provider profile before retrying; no model write was replayed.\n";continue;}
                if(response->status==400){profile_result=1;std::cerr<<"Backend rejected this model for the active provider route. Use /provider-models to inspect account models.\n";continue;}
                if(response->status!=200)throw std::runtime_error("Server rejected conversation model selection (HTTP "+std::to_string(response->status)+")");
                session_model_state=validate_session_model(Json::parse(response->body),session);if(session_model_state.at("revision")!=revision+1)throw std::runtime_error("Invalid acknowledged conversation model selection");
                if(selected){if(!session_model_state.at("selected").is_object()||session_model_state.at("selected").at("model_id").get<std::string>()!=*selected||session_model_state.at("selected").at("provider_profile_id").get<std::string>()!=provider_binding.at("provider_profile_id").get<std::string>()||session_model_state.at("selected").at("provider_route_id").get<std::string>()!=route_id)throw std::runtime_error("Invalid acknowledged conversation model selection");}
                else if(!session_model_state.at("selected").is_null())throw std::runtime_error("Invalid acknowledged conversation model reset");
                model=selected.value_or(std::string{});profile_result=0;std::cout<<Json{{"type","model"},{"session_id",session},{"model_id",selected?Json(*selected):Json(nullptr)},{"default_model",default_model},{"revision",revision+1},{"saved",true}}.dump()<<'\n'<<std::flush;
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
            while(input.read_line(line)){
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
        std::optional<std::string> workspace_command;std::string workspace_command_arguments;Json workspace_command_catalogue;
        if(!composed&&!graphSubmission&&prompt.front()=='/'){
            if(prompt.starts_with("//"))prompt.erase(0,1);
            else {
                try{workspace_command_catalogue=request("/v1/workspace/commands");}
                catch(const std::exception& error){if(prompt=="/commands"){std::cerr<<error.what()<<'\n';continue;}std::cerr<<"Unknown chat command. Use /help, /commands, or // to send a literal slash request.\n";continue;}
                if(!workspace_command_catalogue.is_object()||!workspace_command_catalogue.contains("commands")||!workspace_command_catalogue["commands"].is_array()||workspace_command_catalogue["commands"].size()>128||!workspace_command_catalogue.contains("workspace_id")||!workspace_command_catalogue["workspace_id"].is_string()||workspace_command_catalogue["workspace_id"].get<std::string>().empty()||!workspace_command_catalogue.contains("authority_id")||!workspace_command_catalogue["authority_id"].is_string()||workspace_command_catalogue["authority_id"].get<std::string>().size()!=32||workspace_command_catalogue["authority_id"].get<std::string>().find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid workspace command authority catalogue");
                if(prompt=="/commands"){
                    std::cout<<Json{{"type","workspace_commands"},{"catalogue",workspace_command_catalogue}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Workspace command catalogue output is unavailable");continue;
                }
                const auto end=prompt.find_first_of(" \t\r\n");const auto selected=prompt.substr(1,end==std::string::npos?std::string::npos:end-1);
                const auto found=std::find_if(workspace_command_catalogue["commands"].begin(),workspace_command_catalogue["commands"].end(),[&](const Json& entry){return entry.is_object()&&entry.value("id",std::string{})==selected;});
                if(found==workspace_command_catalogue["commands"].end()){std::cerr<<"Unknown chat command. Use /help, /commands, or // to send a literal slash request.\n";continue;}
                workspace_command=selected;if(end!=std::string::npos)workspace_command_arguments=prompt.substr(end+1);
            }
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
            if(human)std::cerr<<"Session "<<session<<"\n";else std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<std::flush;
        }
        Json body={{"session_id",session},{"prompt",prompt}};if(!model.empty())body["model_id"]=model;
        if(graphSubmission){body["graph_id"]=graphId;body["graph_revision"]=graphRevision;}
        if(health.value(graphSubmission?"graph_provider_profile_admission":"provider_profile_admission",false)&&!provider_binding.value("provider_profile_id",std::string{}).empty())body.update(provider_binding);
        if(workspace_command){body.erase("prompt");body.erase("session_id");body["command_id"]=*workspace_command;body["arguments"]=workspace_command_arguments;body["expected_workspace_id"]=workspace_command_catalogue["workspace_id"];body["expected_workspace_authority_id"]=workspace_command_catalogue["authority_id"];}
        const auto admitted=client.Post(workspace_command?"/v1/sessions/"+session+"/commands":graphSubmission?"/v1/graph-runs":"/v1/runs",headers,body.dump(),"application/json");
        if(!admitted)throw std::runtime_error("Cannot reach xMind Server during run admission; admission outcome is unknown. Inspect /runs before resubmitting.");
        if(admitted->status==400||admitted->status==404||admitted->status==409||admitted->status==429||admitted->status==503){
            std::cout<<Json{{"type","run_rejected"},{"session_id",session},{"http_status",admitted->status},{"prompt",prompt},{"graph_id",graphSubmission?graphId:std::string{}},{"command_id",workspace_command?Json(*workspace_command):Json(nullptr)}}.dump()<<'\n'<<std::flush;
            const auto diagnostic=provider_rejection(admitted->body);const auto detail=diagnostic.value("detail",std::string{});
            std::cerr<<"Backend rejected run admission (HTTP "<<admitted->status<<")"<<(detail.empty()?std::string{}:": "+detail)<<". Request retained above; no automatic retry. Use /runs to inspect existing work.\n";
            last_result=1;continue;
        }
        if(admitted->status!=202)throw std::runtime_error("Unexpected run admission response; inspect /runs before resubmitting");
        const auto run=Json::parse(admitted->body);
        if(!run.is_object() || run.value("session_id",std::string{})!=session || run.value("graph_root",false)!=graphSubmission || !run.contains("id") || !run["id"].is_string())throw std::runtime_error("Invalid chat run admission");const auto id=run["id"].get<std::string>();
        if(id.empty() || id.size()>128 || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid chat run identity");
        if(human)std::cerr<<"Run started · "<<id<<"\n";else std::cout<<Json{{"type","run"},{"run",run}}.dump()<<'\n'<<std::flush;
        const auto watched=watch_run(client,headers,id,0,graphSubmission,true,&input);if(watched==run_detached)return exit_status();last_result=watched;profile_result=0;
        if(!human)std::cout<<Json{{"type","turn_finished"},{"run_id",id},{"exit_status",last_result}}.dump()<<'\n'<<std::flush;
        if(drain_inbox())return exit_status();
    }
    if(!input.source_closed())throw std::runtime_error("Chat input is unavailable");
    return exit_status();
}

std::string terminal_hex_bytes(const std::string& value){
    if(value.size()>131072||value.size()%2)throw std::runtime_error("Invalid terminal output bytes");
    const auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1;};
    std::string result;result.reserve(value.size()/2);
    for(std::size_t i=0;i<value.size();i+=2){const auto high=digit(value[i]),low=digit(value[i+1]);if(high<0||low<0)throw std::runtime_error("Invalid terminal output encoding");result.push_back(static_cast<char>((high<<4)|low));}
    return result;
}
std::int64_t terminal_sequence(const nlohmann::json& value){if(!value.is_number_integer()||value<0||value>9007199254740991LL)throw std::runtime_error("Invalid terminal output cursor");return value.get<std::int64_t>();}
void terminal_exact_fields(const nlohmann::json& value,std::initializer_list<const char*> expected){
    if(!value.is_object()||value.size()!=expected.size())throw std::runtime_error("Invalid terminal response");
    for(const auto* field:expected)if(!value.contains(field))throw std::runtime_error("Invalid terminal response");
}
class TerminalConsoleMode {
public:
    explicit TerminalConsoleMode(bool force_console=false){
#if defined(_WIN32)
        input_=GetStdHandle(STD_INPUT_HANDLE);output_=GetStdHandle(STD_OUTPUT_HANDLE);
        bool standard_input_console=input_!=INVALID_HANDLE_VALUE&&GetConsoleMode(input_,&input_mode_);
        if(force_console&&!standard_input_console){
            const auto console_input=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
            if(console_input!=INVALID_HANDLE_VALUE&&GetConsoleMode(console_input,&input_mode_)){input_=console_input;owns_input_=true;standard_input_console=true;}
            else if(console_input!=INVALID_HANDLE_VALUE)CloseHandle(console_input);
        }
        if(force_console&&!standard_input_console)throw std::runtime_error("No interactive Windows console is available");
        if(output_==INVALID_HANDLE_VALUE||!GetConsoleMode(output_,&output_mode_)){
            const auto console_output=CreateFileW(L"CONOUT$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
            if(console_output!=INVALID_HANDLE_VALUE){output_=console_output;owns_output_=true;}
        }
        if(standard_input_console){
            console_=true;has_input_mode_=true;
            const DWORD mode=(input_mode_&~(ENABLE_PROCESSED_INPUT|ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT))|ENABLE_WINDOW_INPUT|ENABLE_EXTENDED_FLAGS;
            SetConsoleMode(input_,mode);
        }else{
            resize_input_=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
            if(resize_input_!=INVALID_HANDLE_VALUE&&GetConsoleMode(resize_input_,&resize_input_mode_)){
                has_resize_input_mode_=true;const DWORD mode=resize_input_mode_|ENABLE_WINDOW_INPUT|ENABLE_EXTENDED_FLAGS;
                SetConsoleMode(resize_input_,mode);
            }else if(resize_input_!=INVALID_HANDLE_VALUE){CloseHandle(resize_input_);resize_input_=INVALID_HANDLE_VALUE;}
        }
        if(output_!=INVALID_HANDLE_VALUE&&GetConsoleMode(output_,&output_mode_)){
            has_output_mode_=true;SetConsoleMode(output_,output_mode_|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
        input_cp_=GetConsoleCP();output_cp_=GetConsoleOutputCP();SetConsoleCP(CP_UTF8);SetConsoleOutputCP(CP_UTF8);
#endif
#if !defined(_WIN32)
        if(force_console)throw std::runtime_error("The --console option is supported only on Windows");
#endif
    }
    ~TerminalConsoleMode(){
#if defined(_WIN32)
        if(has_input_mode_)SetConsoleMode(input_,input_mode_);
        if(has_output_mode_)SetConsoleMode(output_,output_mode_);
        if(has_resize_input_mode_)SetConsoleMode(resize_input_,resize_input_mode_);
        if(input_cp_)SetConsoleCP(input_cp_);if(output_cp_)SetConsoleOutputCP(output_cp_);
        if(owns_input_)CloseHandle(input_);if(owns_output_)CloseHandle(output_);if(resize_input_!=INVALID_HANDLE_VALUE)CloseHandle(resize_input_);
#endif
    }
#if defined(_WIN32)
    bool console()const{return console_;}
    HANDLE input()const{return input_;}
    HANDLE resize_input()const{return resize_input_;}
    std::pair<unsigned short,unsigned short> dimensions()const{
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if(output_==INVALID_HANDLE_VALUE||!GetConsoleScreenBufferInfo(output_,&info))return {24,80};
        const auto rows=static_cast<unsigned short>(std::max<SHORT>(1,info.srWindow.Bottom-info.srWindow.Top+1));
        const auto columns=static_cast<unsigned short>(std::max<SHORT>(1,info.srWindow.Right-info.srWindow.Left+1));
        return agentflow::terminal_console_dimensions(rows,columns);
    }
#endif
private:
#if defined(_WIN32)
    HANDLE input_{INVALID_HANDLE_VALUE},output_{INVALID_HANDLE_VALUE},resize_input_{INVALID_HANDLE_VALUE};DWORD input_mode_{},output_mode_{},resize_input_mode_{};UINT input_cp_{},output_cp_{};bool console_{},has_input_mode_{},has_output_mode_{},owns_input_{},owns_output_{},has_resize_input_mode_{};
#endif
};

int terminal_attach(agentflow::ConsoleTransport& client,int port,const std::string& workspace,const httplib::Headers& headers,const std::string& session,const std::string& title,bool force_console){
    using Json=nlohmann::json;
    TerminalConsoleMode console(force_console);
    unsigned short initial_rows=24,initial_columns=80;
#if defined(_WIN32)
    if(console.console()){const auto size=console.dimensions();initial_rows=size.first;initial_columns=size.second;}
#endif
    const auto created=client.Post("/v1/sessions/"+session+"/terminals",headers,Json{{"title",title},{"rows",initial_rows},{"columns",initial_columns}}.dump(),"application/json");
    if(!created||created->status!=201)throw std::runtime_error("Cannot create the workspace terminal");
    const auto metadata=Json::parse(created->body);terminal_exact_fields(metadata,{"id","session_id","title","rows","columns","running","exit_code","output_sequence"});
    if(!metadata["id"].is_string()||metadata["id"].get_ref<const std::string&>().size()!=32||metadata["id"].get_ref<const std::string&>().find_first_not_of("0123456789abcdef")!=std::string::npos||metadata["session_id"]!=session||metadata["title"]!=title||metadata["rows"]!=initial_rows||metadata["columns"]!=initial_columns||metadata["running"]!=true||!metadata["exit_code"].is_null())throw std::runtime_error("Invalid created workspace terminal");
    const auto initial_sequence=terminal_sequence(metadata["output_sequence"]);(void)initial_sequence;
    const auto terminal=metadata["id"].get<std::string>();std::atomic_bool finished=false,stop=false;std::exception_ptr observation_error;std::string observation_message;
#if defined(_WIN32)
    std::mutex input_mutex;std::condition_variable input_ready;std::deque<std::string> pending_input;std::size_t pending_input_bytes=0;bool stop_input=false;std::exception_ptr input_error;
    const auto enqueue_input=[&](const std::wstring& wide){
        if(wide.empty())return;const auto data=utf8_argument(wide);std::lock_guard lock(input_mutex);
        if(pending_input_bytes+data.size()>1024*1024)throw std::runtime_error("Terminal input queue is full");
        pending_input_bytes+=data.size();pending_input.push_back(data);input_ready.notify_one();
    };
    const auto resize_terminal=[&]{
        const auto size=console.dimensions();agentflow::ConsoleTransport resize_client(port,workspace);resize_client.set_connection_timeout(2,0);resize_client.set_read_timeout(5,0);resize_client.set_write_timeout(2,0);resize_client.set_follow_location(false);
        const auto response=resize_client.Post("/v1/terminals/"+terminal+"/resize",headers,Json{{"rows",size.first},{"columns",size.second}}.dump(),"application/json");
        if(!response||response->status!=200)throw std::runtime_error("Terminal resize was not accepted");
    };
    std::thread input_worker([&]{
        try{
            agentflow::ConsoleTransport input_client(port,workspace);input_client.set_connection_timeout(2,0);input_client.set_read_timeout(5,0);input_client.set_write_timeout(2,0);input_client.set_follow_location(false);
            while(true){std::string batch;
                {std::unique_lock lock(input_mutex);input_ready.wait_for(lock,std::chrono::milliseconds(5),[&]{return stop_input||finished.load(std::memory_order_acquire);});
                    if(finished.load(std::memory_order_acquire)){pending_input.clear();pending_input_bytes=0;break;}
                    while(!pending_input.empty()&&batch.size()+pending_input.front().size()<=64*1024){batch+=pending_input.front();pending_input_bytes-=pending_input.front().size();pending_input.pop_front();}
                    if(batch.empty()&&stop_input)break;
                }
                if(batch.empty())continue;
                const auto response=input_client.Post("/v1/terminals/"+terminal+"/input",headers,Json{{"data",batch}}.dump(),"application/json");
                if(!response||response->status!=200)throw std::runtime_error("Terminal input was not accepted");
            }
        }catch(...){input_error=std::current_exception();finished.store(true,std::memory_order_release);input_ready.notify_all();}
    });
#endif
    std::thread output([&]{
        try{
            agentflow::ConsoleTransport observer(port,workspace);observer.set_connection_timeout(2,0);observer.set_read_timeout(5,0);observer.set_write_timeout(2,0);observer.set_follow_location(false);
            auto cursor=terminal_sequence(metadata["output_sequence"]);
            while(!stop.load(std::memory_order_acquire)){
                const auto response=observer.Get("/v1/terminals/"+terminal+"/output?after="+std::to_string(cursor),headers);
                if(!response||response->status!=200)throw std::runtime_error("Terminal output observation failed; close the terminal and inspect the backend");
                const auto chunk=Json::parse(response->body);terminal_exact_fields(chunk,{"encoding","data","next_sequence","truncated","running","exit_code"});
                if(chunk["encoding"]!="hex"||!chunk["truncated"].is_boolean()||!chunk["running"].is_boolean())throw std::runtime_error("Invalid terminal output metadata");
                const auto next=terminal_sequence(chunk["next_sequence"]);if(next<cursor)throw std::runtime_error("Terminal output cursor moved backwards");
                if(chunk["truncated"].get<bool>()){constexpr char warning[]="\r\n[xMind: older terminal output was truncated]\r\n";std::cout.write(warning,sizeof(warning)-1);}
                const auto bytes=terminal_hex_bytes(chunk["data"].get<std::string>());if(!bytes.empty())std::cout.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));std::cout.flush();if(!std::cout)throw std::runtime_error("Terminal display output is unavailable");cursor=next;
                if(!chunk["running"].get<bool>()){
                    if(!chunk["exit_code"].is_number_integer())throw std::runtime_error("Invalid terminal exit status");
                    std::cout<<"\r\n[xMind: terminal exited with code "<<chunk["exit_code"].get<std::int64_t>()<<"]\r\n"<<std::flush;finished.store(true,std::memory_order_release);break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }catch(...){observation_error=std::current_exception();try{std::rethrow_exception(observation_error);}catch(const std::exception& error){observation_message=error.what();}catch(...){observation_message="Unknown terminal observation failure";}finished.store(true,std::memory_order_release);}
    });
#if defined(_WIN32)
    std::atomic_bool stop_resize=false;std::exception_ptr resize_error;
    std::thread resize_events;
    if(!console.console()&&console.resize_input()!=INVALID_HANDLE_VALUE){
        resize_events=std::thread([&]{try{INPUT_RECORD event{};DWORD count=0;while(!stop_resize.load(std::memory_order_acquire)){
            if(WaitForSingleObject(console.resize_input(),100)!=WAIT_OBJECT_0)continue;
            if(!ReadConsoleInputW(console.resize_input(),&event,1,&count)||count!=1)throw std::runtime_error("Terminal resize observation is unavailable");
            if(event.EventType==WINDOW_BUFFER_SIZE_EVENT)resize_terminal();
        }}catch(...){resize_error=std::current_exception();}});
    }
#endif
    std::cerr<<"Connected to the workspace shell. Enter commands; `exit` closes it.\n"<<std::flush;
    bool requested_exit=false;
    try{
#if defined(_WIN32)
        if(console.console()){
            INPUT_RECORD record{};DWORD read=0;wchar_t pending_high_surrogate{};
            while(!finished.load(std::memory_order_acquire)){
                if(WaitForSingleObject(console.input(),100)!=WAIT_OBJECT_0)continue;
                if(!ReadConsoleInputW(console.input(),&record,1,&read)||read!=1)throw std::runtime_error("Terminal console input is unavailable");
                if(record.EventType==WINDOW_BUFFER_SIZE_EVENT){resize_terminal();continue;}
                if(record.EventType!=KEY_EVENT||!record.Event.KeyEvent.bKeyDown)continue;
                const auto& key=record.Event.KeyEvent;const auto modifiers=key.dwControlKeyState;
                const auto character=key.uChar.UnicodeChar;
                std::wstring characters;
                if(character>=0xd800&&character<=0xdbff){pending_high_surrogate=character;continue;}
                if(character>=0xdc00&&character<=0xdfff){if(!pending_high_surrogate)continue;characters.push_back(pending_high_surrogate);characters.push_back(character);pending_high_surrogate=0;}
                else{pending_high_surrogate=0;if(character)characters.push_back(character);}
                const auto value=agentflow::terminal_key_input(key.wVirtualKeyCode,characters,
                    (modifiers&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0,
                    (modifiers&(SHIFT_PRESSED))!=0,
                    (modifiers&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))!=0);
                const auto repeats=std::min<DWORD>(key.wRepeatCount,128);
                for(DWORD repeat=0;repeat<repeats;++repeat)enqueue_input(value);
            }
        }else
#endif
        {
            std::string line;
            while(!finished.load(std::memory_order_acquire)&&std::getline(std::cin,line)){
                if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.size()>64*1024||line.find('\0')!=std::string::npos)throw std::invalid_argument("Terminal command exceeds input limits");
                const std::string data=line+"\r\n";const auto response=client.Post("/v1/terminals/"+terminal+"/input",headers,Json{{"data",data}}.dump(),"application/json");if(!response||response->status!=200)throw std::runtime_error("Terminal input was not accepted");if(line=="exit"||line=="exit /b"){requested_exit=true;break;}
            }
        }
    }catch(...){stop.store(true,std::memory_order_release);
#if defined(_WIN32)
        {std::lock_guard lock(input_mutex);stop_input=true;}input_ready.notify_all();if(input_worker.joinable())input_worker.join();
        stop_resize.store(true,std::memory_order_release);if(resize_events.joinable())resize_events.join();
#endif
        if(output.joinable())output.join();client.Post("/v1/terminals/"+terminal+"/close",headers,"{}","application/json");throw;}
    if(requested_exit){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!finished.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(25));}
    stop.store(true,std::memory_order_release);
#if defined(_WIN32)
    {std::lock_guard lock(input_mutex);stop_input=true;}input_ready.notify_all();if(input_worker.joinable())input_worker.join();
    stop_resize.store(true,std::memory_order_release);if(resize_events.joinable())resize_events.join();
#endif
    if(output.joinable())output.join();
    const auto closed=client.Post("/v1/terminals/"+terminal+"/close",headers,"{}","application/json");if(!closed||closed->status!=200)throw std::runtime_error("Could not retire the workspace terminal");
#if defined(_WIN32)
    if(resize_error)try{std::rethrow_exception(resize_error);}catch(const std::exception& error){throw std::runtime_error(error.what());}
    if(input_error)try{std::rethrow_exception(input_error);}catch(const std::exception& error){throw std::runtime_error(error.what());}
#endif
    if(observation_error)throw std::runtime_error(observation_message);
    return 0;
}
}

int cli_main(int argc,char** argv,const std::string& workspace,const std::function<agentflow::LocalProfileConnection()>& profile) {
    try {
        if(argc<3) throw std::invalid_argument("Usage: xmind_cli PORT COMMAND [ARGS] (commands: health, info, connections, connection-save ID NAME HTTPS_ORIGIN EXPECTED_REVISION, connection-remove ID EXPECTED_REVISION, connection-select ID EXPECTED_REVISION, chat [SESSION [MODEL]], sessions, create-session, rename-session SESSION TITLE EXPECTED_TITLE, fork-session SESSION [BEFORE_MESSAGE_ID], export-session SESSION FILE, import-session FILE, delete-session SESSION, restore SESSION OPERATION_ID, session-interrupt SESSION [false], history SESSION, history-page SESSION LIMIT [BEFORE_MESSAGE_ID], message SESSION MESSAGE_ID, terminals, terminal-create SESSION [TITLE], terminal-attach SESSION [TITLE] [--console], terminal-output ID [AFTER], terminal-input ID UTF8, terminal-resize ID ROWS COLUMNS, terminal-close ID, runs, run SESSION PROMPT [MODEL] [--file PATH] [--folder-id ID], cancel, status, git-status [FOLDER_ID], git-info [FOLDER_ID], git-branches [FOLDER_ID], git-base [FOLDER_ID], git-diff MODE [BASE [CONTEXT]] [--folder-id ID], events, watch, models, agents, session-agent SESSION, set-agent SESSION AGENT_ID|default EXPECTED_REVISION AGENT_REVISION, skills, session-skills, set-skills SESSION SNAPSHOT_JSON_FILE [ID...], provider-profiles, profile-models ID ROUTE REVISION [KEY_ENV], save-profile ID ROUTE MODEL REVISION [KEY_ENV] [--activate], select-profile ID REVISION, provider, provider-models [KEY_ENV REVISION], configure-provider MODEL KEY_ENV REVISION, graphs, graph-run SESSION GRAPH REV PROMPT [MODEL] [--file PATH] [--folder-id ID], graph ROOT, graph-input ROOT NODE REV JSON_FILE, graph-events ROOT [AFTER], graph-watch ROOT [AFTER], graph-children ROOT, planning, plan-input ROOT REQUEST REV SEQUENCE JSON_FILE, resume-plan ROOT REV SEQUENCE, instructions, mcp-servers, mcp-resources, a2a-agents, a2a-register ID CARD_URL true|false EXPECTED_REVISION [BEARER_ENV], a2a-enable ID true|false EXPECTED_REVISION, a2a-remove ID EXPECTED_REVISION, a2a-inspect OPERATION_ID [--include-artifacts], a2a-cancel OPERATION_ID, a2a-follow-up OPERATION_ID SEQUENCE MESSAGE, a2a-resubscribe OPERATION_ID, process-profiles, process-output OPERATION_ID stdout|stderr [AFTER [LIMIT]], operations, operation, inspect-edit, decide, append-message)");
        const std::string port_text=argv[1],command=argv[2];int port=0;
        const auto parsed=std::from_chars(port_text.data(),port_text.data()+port_text.size(),port);
        if(parsed.ec!=std::errc{} || parsed.ptr!=port_text.data()+port_text.size() || port<1 || port>65535) throw std::invalid_argument("Invalid port");
        auto id=[](const std::string& value) {
            if(value.empty() || value.size()>128) throw std::invalid_argument("Invalid ID");
            for(unsigned char c:value) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-')) throw std::invalid_argument("Invalid ID");
            return value;
        };
        auto terminal_id=[](const std::string& value){if(value.size()!=32||value.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("Invalid terminal ID");return value;};
        auto terminal_dimension=[](const std::string& value){const auto parsed=event_cursor(value);if(parsed<1||parsed>500)throw std::invalid_argument("Terminal dimension is outside its supported range");return parsed;};
        using Json=nlohmann::json;std::string path,chat_model,mcp_id,terminal_session,terminal_title="xMind CLI",connection_id,connection_name,connection_origin,transfer_file,deleted_session_id;Json body;bool post=false,watch=false,graph_watch=false,chat=false,terminal_attach_command=false,terminal_force_console=false,saved_provider_key=false,profile_operation=false,mcp_operation=false,a2a_operation=false,vcs_status=false,vcs_info=false,vcs_branches=false,vcs_base=false,vcs_diff=false,connection_operation=false,connection_save=false,connection_remove=false,session_export=false,session_delete=false;std::int64_t watch_cursor=0,connection_expected_revision=0;
        if(command=="health" && argc==3) path="/v1/health";
        else if(command=="info" && argc==3) path="/v1/info";
        else if(command=="connections"&&argc==3){path="/v1/connections";connection_operation=true;}
        else if(command=="connection-select"&&argc==5){connection_id=connection_profile_identity(argv[3]);connection_expected_revision=provider_revision(argv[4]);path="/v1/connections/select";body={{"id",connection_id},{"expected_revision",connection_expected_revision}};post=true;connection_operation=true;}
        else if(command=="connection-save"&&argc==7){connection_id=connection_profile_identity(argv[3]);connection_name=argv[4];connection_origin=argv[5];connection_expected_revision=provider_revision(argv[6]);if(connection_id=="local"||connection_name.empty()||connection_name.size()>128||connection_name.find_first_of("\r\n")!=std::string::npos||connection_name.find('\0')!=std::string::npos||connection_origin.empty()||connection_origin.size()>8192)throw std::invalid_argument("Invalid Nexus connection profile fields");connection_save=true;connection_operation=true;}
        else if(command=="connection-remove"&&argc==5){connection_id=connection_profile_identity(argv[3]);connection_expected_revision=provider_revision(argv[4]);if(connection_id=="local")throw std::invalid_argument("The Local connection profile cannot be removed");connection_remove=true;connection_operation=true;}
        else if(command=="chat" && argc>=3 && argc<=5){chat=true;if(argc>=4)path=id(argv[3]);if(argc==5)chat_model=argv[4];}
        else if(command=="sessions" && argc==3) path="/v1/sessions";
        else if(command=="delete-session"&&argc==4){deleted_session_id=id(argv[3]);path="/v1/sessions/"+deleted_session_id;session_delete=true;}
        else if(command=="create-session" && argc==4) {path="/v1/sessions";body={{"title",argv[3]}};post=true;}
        else if(command=="rename-session" && argc==6) {path="/v1/sessions/"+id(argv[3])+"/title";body={{"title",argv[4]},{"expected_title",argv[5]}};post=true;}
        else if(command=="fork-session"&&(argc==4||argc==5)){path="/v1/sessions/"+id(argv[3])+"/fork";body={{"before",argc==5?Json(argv[4]):Json(nullptr)}};post=true;}
        else if(command=="restore"&&argc==5){path="/v1/operations/"+id(argv[4])+"/restore";body={{"session_id",id(argv[3])}};post=true;}
        else if(command=="export-session"&&argc==5){path="/v1/sessions/"+id(argv[3])+"/export";transfer_file=argv[4];session_export=true;}
        else if(command=="import-session"&&argc==4){transfer_file=argv[3];std::ifstream file(std::filesystem::u8path(transfer_file),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read xMind session archive");std::string source;char byte;while(file.get(byte)){if(source.size()>=4*1024*1024)throw std::invalid_argument("xMind session archive exceeds 4 MiB");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read xMind session archive");const auto archive=Json::parse(source);body={{"archive",archive}};path="/v1/sessions/import";post=true;}
        else if(command=="history" && argc==4) path="/v1/sessions/"+id(argv[3])+"/history";
        else if(command=="history-page"&&(argc==5||argc==6)){const auto session=id(argv[3]);const auto limit=event_cursor(argv[4]);if(limit<1||limit>1000)throw std::invalid_argument("Message history limit must be between 1 and 1000");path="/v1/sessions/"+session+"/messages?limit="+std::to_string(limit);if(argc==6)path+="&before=msg_"+std::to_string(message_sequence(argv[5]));}
        else if(command=="message" && argc==5){const auto sequence=message_sequence(argv[4]);path="/v1/sessions/"+id(argv[3])+"/messages/msg_"+std::to_string(sequence);}
        else if(command=="terminals"&&argc==4)path="/v1/sessions/"+id(argv[3])+"/terminals";
        else if(command=="terminal-attach"&&argc>=4&&argc<=6){terminal_attach_command=true;terminal_session=id(argv[3]);bool have_title=false;for(int index=4;index<argc;++index){if(std::string_view(argv[index])=="--console"){if(terminal_force_console)throw std::invalid_argument("Duplicate terminal console option");terminal_force_console=true;}else if(!have_title){terminal_title=argv[index];have_title=true;}else throw std::invalid_argument("Unexpected terminal attachment option");}if(terminal_title.empty()||terminal_title.size()>128||terminal_title.find_first_of("\r\n\0")!=std::string::npos)throw std::invalid_argument("Invalid terminal title");}
        else if(command=="terminal-create"&&(argc==4||argc==5)){path="/v1/sessions/"+id(argv[3])+"/terminals";if(argc==5)body={{"title",argv[4]}};else body=Json::object();post=true;}
        else if(command=="terminal-output"&&(argc==4||argc==5)){path="/v1/terminals/"+terminal_id(argv[3])+"/output?after="+std::to_string(event_cursor(argc==5?argv[4]:"0"));}
        else if(command=="terminal-input"&&argc==5){const std::string data=argv[4];if(data.empty()||data.size()>64*1024||data.find('\0')!=std::string::npos)throw std::invalid_argument("Terminal input is empty or exceeds limits");path="/v1/terminals/"+terminal_id(argv[3])+"/input";body={{"data",data}};post=true;}
        else if(command=="terminal-resize"&&argc==6){const auto rows=terminal_dimension(argv[4]),columns=terminal_dimension(argv[5]);if(rows<5||rows>300||columns<20||columns>500)throw std::invalid_argument("Terminal size must be between 20x5 and 500x300");path="/v1/terminals/"+terminal_id(argv[3])+"/resize";body={{"rows",rows},{"columns",columns}};post=true;}
        else if(command=="terminal-close"&&argc==4){path="/v1/terminals/"+terminal_id(argv[3])+"/close";body=Json::object();post=true;}
        else if(command=="context"&&(argc==4||argc==5))path="/v1/sessions/"+id(argv[3])+"/context"+(argc==5?"?model_id="+httplib::encode_query_component(provider_profile_identity(argv[4])):std::string{});
        else if(command=="context-request"&&(argc==5||argc==6))path="/v1/sessions/"+id(argv[3])+"/context/requests/"+id(argv[4])+(argc==6?"?model_id="+httplib::encode_query_component(provider_profile_identity(argv[5])):std::string{});
        else if(command=="compact-context"&&(argc==6||argc==7)){
            path="/v1/sessions/"+id(argv[3])+"/context/compact";body={{"expected_head_revision",provider_revision(argv[4])},{"id",id(argv[5])}};
            if(argc==7)body["model_id"]=provider_profile_identity(argv[6]);post=true;
        }
        else if(command=="session-interrupt"&&(argc==4||argc==5)){if(argc==5&&std::string_view(argv[4])!="false")throw std::invalid_argument("Session interruption currently supports only resume=false");path="/v1/sessions/"+id(argv[3])+"/interrupt?resume=false";body=Json::object();post=true;}
        else if(command=="runs" && argc==4) path="/v1/sessions/"+id(argv[3])+"/runs";
        else if(command=="status" && argc==4) path="/v1/runs/"+id(argv[3]);
        else if(command=="git-status"&&(argc==3||argc==4)){path="/v1/vcs/status";if(argc==4)path+="?folder_id="+httplib::encode_query_component(argv[3]);vcs_status=true;}
        else if(command=="git-info"&&(argc==3||argc==4)){path="/v1/vcs/info";if(argc==4)path+="?folder_id="+httplib::encode_query_component(argv[3]);vcs_info=true;}
        else if(command=="git-branches"&&(argc==3||argc==4)){path="/v1/vcs/branches";if(argc==4)path+="?folder_id="+httplib::encode_query_component(argv[3]);vcs_branches=true;}
        else if(command=="git-base"&&(argc==3||argc==4)){path="/v1/vcs/base";if(argc==4)path+="?folder_id="+httplib::encode_query_component(argv[3]);vcs_base=true;}
        else if(command=="git-diff"&&argc>=4&&argc<=8){const std::string mode=argv[3];std::optional<std::string> base;std::optional<std::string> context;std::optional<std::string> folder;for(int index=4;index<argc;++index){const std::string_view value=argv[index];if(value=="--folder-id"){if(folder||index+1>=argc||!*argv[index+1])throw std::invalid_argument("git-diff requires one value after --folder-id");folder=argv[++index];}else if(value.starts_with("--"))throw std::invalid_argument("Unknown git-diff option");else if(!base)base=std::string(value);else if(!context)context=std::string(value);else throw std::invalid_argument("Unexpected git-diff argument");}path="/v1/vcs/diff?mode="+httplib::encode_query_component(mode);if(base)path+="&base="+httplib::encode_query_component(*base);if(context)path+="&context="+httplib::encode_query_component(*context);if(folder)path+="&folder_id="+httplib::encode_query_component(*folder);vcs_diff=true;}
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
        else if(command=="agent-definition"&&argc==4){const std::string value=argv[3];if(value.empty()||value.size()>64||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid named agent identity");path="/v1/agents/"+value;}
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
        else if(command=="mcp-resources" && argc==3) path="/v1/mcp/resources";
        else if(command=="mcp-auth"&&argc==3)mcp_operation=true;
        else if((command=="mcp-login"||command=="mcp-refresh"||command=="mcp-login-status"||command=="mcp-login-cancel"||command=="mcp-login-open"||command=="mcp-login-watch")&&argc==4){mcp_operation=true;mcp_id=mcp_identity(argv[3],command=="mcp-login"||command=="mcp-refresh");}
        else if(command=="a2a-agents"&&argc==3)a2a_operation=true;
        else if(command=="a2a-register"&&(argc==7||argc==8))a2a_operation=true;
        else if(command=="a2a-enable"&&argc==6)a2a_operation=true;
        else if(command=="a2a-remove"&&argc==5)a2a_operation=true;
        else if(command=="a2a-inspect"&&(argc==4||argc==5))a2a_operation=true;
        else if(command=="a2a-cancel"&&argc==4)a2a_operation=true;
        else if(command=="a2a-follow-up"&&argc==6)a2a_operation=true;
        else if(command=="a2a-resubscribe"&&argc==4)a2a_operation=true;
        else if(command=="process-profiles" && argc==3) path="/v1/process/profiles";
        else if(command=="process-output" && argc>=5 && argc<=7){const std::string channel=argv[4];if(channel!="stdout"&&channel!="stderr")throw std::invalid_argument("Process output channel must be stdout or stderr");const auto after=event_cursor(argc>=6?argv[5]:"0");const auto limit=argc==7?event_cursor(argv[6]):65536;if(limit<1||limit>262144)throw std::invalid_argument("Process output page size must be between 1 and 262144");path="/v1/operations/"+id(argv[3])+"/output?channel="+channel+"&after="+std::to_string(after)+"&limit="+std::to_string(limit);}
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
        else if(command=="graph-run" && argc>=7){
            const auto revision=event_cursor(argv[5]);if(revision<1 || revision>9007199254740991)throw std::invalid_argument("Invalid graph revision");
            path="/v1/graph-runs";body={{"session_id",id(argv[3])},{"graph_id",argv[4]},{"graph_revision",revision},{"prompt",argv[6]}};post=true;Json files=Json::array();std::string folder_id;bool model_set=false;
            for(int index=7;index<argc;++index){const std::string_view option=argv[index];
                if(option=="--file"||option=="--folder-id"||option=="--model"){
                    if(index+1>=argc||!*argv[index+1])throw std::invalid_argument("Missing value for graph-run option");const std::string value=argv[++index];
                    if(option=="--file"){if(files.size()>=32)throw std::invalid_argument("A graph run can include up to 32 prompt files");Json file={{"path",workspace_relative_attachment_path(value)}};if(!folder_id.empty())file["folder_id"]=folder_id;files.push_back(std::move(file));}
                    else if(option=="--folder-id")folder_id=workspace_folder_identity(value);
                    else{if(model_set)throw std::invalid_argument("Duplicate graph model selection");body["model_id"]=provider_profile_identity(value);model_set=true;}
                }else if(index==7&&!model_set&&files.empty()&&!option.starts_with("--")){body["model_id"]=provider_profile_identity(std::string(option));model_set=true;}
                else throw std::invalid_argument("Unknown graph-run option; use --file PATH, --folder-id ID, or --model MODEL");
            }
            if(!files.empty())body["files"]=std::move(files);
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
        else if(command=="run" && argc>=5) {
            path="/v1/runs";body={{"session_id",id(argv[3])},{"prompt",argv[4]}};post=true;Json files=Json::array();std::string folder_id;bool model_set=false;
            for(int index=5;index<argc;++index){const std::string_view option=argv[index];
                if(option=="--file"||option=="--folder-id"||option=="--model"){
                    if(index+1>=argc||!*argv[index+1])throw std::invalid_argument("Missing value for run option");const std::string value=argv[++index];
                    if(option=="--file"){if(files.size()>=32)throw std::invalid_argument("A run can include up to 32 prompt files");Json file={{"path",workspace_relative_attachment_path(value)}};if(!folder_id.empty())file["folder_id"]=folder_id;files.push_back(std::move(file));}
                    else if(option=="--folder-id")folder_id=workspace_folder_identity(value);
                    else {if(model_set)throw std::invalid_argument("Duplicate model selection");body["model_id"]=provider_profile_identity(value);model_set=true;}
                }else if(index==5&&!model_set&&files.empty()&&!option.starts_with("--")){body["model_id"]=provider_profile_identity(std::string(option));model_set=true;}
                else throw std::invalid_argument("Unknown run option; use --file PATH, --folder-id ID, or --model MODEL");
            }
            if(!files.empty())body["files"]=std::move(files);
        }
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
        if(connection_save||connection_remove){
            const auto current=client.Get("/v1/connections",headers);
            if(!current||current->status!=200)throw std::runtime_error("Cannot inspect the backend connection profile catalog");
            const auto catalog=validate_connection_catalog(Json::parse(current->body));
            auto profiles=Json::array();
            for(const auto& existing:catalog["profiles"])profiles.push_back({{"id",existing["id"]},{"name",existing["name"]},{"kind",existing["kind"]},{"origin",existing["origin"]}});
            const auto found=std::find_if(profiles.begin(),profiles.end(),[&](const auto& item){return item.value("id",std::string{})==connection_id;});
            if(connection_save){
                const Json profile{{"id",connection_id},{"name",connection_name},{"kind","nexus"},{"origin",connection_origin}};
                if(found==profiles.end())profiles.push_back(profile);
                else *found=profile;
                if(profiles.size()>32)throw std::invalid_argument("Connection catalog is full");
            }else{
                if(found==profiles.end())throw std::invalid_argument("Nexus connection profile was not found");
                profiles.erase(found);
            }
            path="/v1/connections";body={{"profiles",std::move(profiles)},{"expected_revision",connection_expected_revision}};post=true;
        }
        if(terminal_attach_command)return terminal_attach(client,port,selected,headers,terminal_session,terminal_title,terminal_force_console);
        if(chat)return chat_session(client,headers,path,chat_model);
        if(mcp_operation){if(command=="mcp-login-watch")return mcp_watch(client,headers,mcp_id);std::cout<<mcp_command(client,headers,command,mcp_id).dump(2)<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("MCP observation output unavailable; backend sign-in continues");return 0;}
        if(a2a_operation){std::cout<<a2a_command(client,headers,command,argc,argv).dump(2)<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("A2A registry output unavailable");return 0;}
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
                const auto metadata=client.Get("/v1/provider/profiles",headers);if(!metadata||metadata->status!=200)throw std::runtime_error("Cannot inspect backend provider profile");const auto binding=provider_admission_binding(Json::parse(metadata->body));if(!binding.at("provider_profile_id").get<std::string>().empty())body.update(binding);
            }
            if((path=="/v1/runs"||path=="/v1/graph-runs")&&body.contains("files")){
                const auto workspace_response=client.Get("/v1/workspace",headers),membership_response=client.Get("/v1/workspace/membership",headers);
                if(!workspace_response||workspace_response->status!=200||!membership_response||membership_response->status!=200)throw std::runtime_error("Cannot inspect the current workspace for prompt file attachments");
                const auto workspace_metadata=Json::parse(workspace_response->body),membership=Json::parse(membership_response->body);
                if(!workspace_metadata.is_object()||workspace_metadata.value("configured",false)!=true||!workspace_metadata.contains("workspace_id")||!workspace_metadata["workspace_id"].is_string()||!workspace_metadata.contains("authority_id")||!workspace_metadata["authority_id"].is_string()||workspace_metadata["authority_id"].get_ref<const std::string&>().size()!=32||workspace_metadata["authority_id"].get_ref<const std::string&>().find_first_not_of("0123456789abcdef")!=std::string::npos||
                   !membership.is_object()||!membership.contains("workspace_id")||!membership["workspace_id"].is_string()||membership["workspace_id"].get_ref<const std::string&>()!=workspace_metadata["workspace_id"].get_ref<const std::string&>()||!membership.contains("revision")||!membership["revision"].is_number_integer()||membership["revision"]<1||membership["revision"]>9007199254740991||!membership.contains("primary_folder_id")||!membership["primary_folder_id"].is_string()||!membership.contains("folders")||!membership["folders"].is_array()||membership["folders"].empty()||membership["folders"].size()>64)throw std::runtime_error("Backend returned invalid workspace attachment metadata");
                for(auto& file:body["files"]){if(!file.contains("folder_id"))file["folder_id"]=membership["primary_folder_id"];const auto folder=workspace_folder_identity(file["folder_id"].get<std::string>());const auto found=std::find_if(membership["folders"].begin(),membership["folders"].end(),[&](const Json& item){return item.is_object()&&item.value("folder_id",std::string{})==folder;});if(found==membership["folders"].end())throw std::invalid_argument("Prompt attachment folder is not registered in the current workspace");file["folder_id"]=folder;}
                body["expected_workspace_id"]=workspace_metadata["workspace_id"];body["expected_workspace_authority_id"]=workspace_metadata["authority_id"];body["expected_workspace_revision"]=membership["revision"];
            }
        }
        auto response=session_delete?client.Delete(path,headers):post?((path=="/v1/provider/profiles/models"||path=="/v1/provider/models")?provider_discovery_request(client,headers,path,body):client.Post(path,headers,body.dump(),"application/json")):client.Get(path,headers);
        if(!response) throw std::runtime_error("Cannot reach xMind Server");
        if(response->status<200 || response->status>=300) {
            if(profile_operation)std::cerr<<provider_rejection(response->body).dump()<<'\n'<<"Backend rejected provider profile request (HTTP "<<response->status<<"). No automatic retry or selection fallback.\n";
            else {std::cerr<<Json::parse(response->body).dump()<<'\n';if(command=="set-skills")std::cerr<<"Backend rejected session skill change (HTTP "<<response->status<<"). No automatic retry.\n";if(connection_operation)std::cerr<<"Connection profile request was not applied; no fallback or implicit profile change occurred.\n";}
            return 1;
        }
        if(session_delete&&(response->status!=204||!response->body.empty()))throw std::runtime_error("Invalid native session deletion response");
        auto result=session_delete?Json{{"deleted",true},{"session_id",deleted_session_id}}:Json::parse(response->body);
        if(connection_operation)result=validate_connection_catalog(result);
        if(session_export){
            const auto output=std::filesystem::u8path(transfer_file);if(std::filesystem::exists(output))throw std::invalid_argument("Export target already exists; choose a new file path");
            std::ofstream file(output,std::ios::binary|std::ios::out|std::ios::trunc);if(!file)throw std::runtime_error("Cannot create session archive file");const auto serialized=result.dump(2);file.write(serialized.data(),static_cast<std::streamsize>(serialized.size()));file.put('\n');file.close();if(!file)throw std::runtime_error("Cannot write complete session archive");
            std::cout<<Json{{"exported",true},{"file",transfer_file}}.dump(2)<<'\n';return 0;
        }
        if(vcs_status){
            if(!result.is_array()||result.size()>4096)throw std::runtime_error("Invalid native Git status response");
            for(const auto& item:result){
                const auto count=[](const Json& value){return value.is_number_unsigned()&&value.get<std::uint64_t>()<=9007199254740991ULL;};
                if(!item.is_object()||item.size()!=4||!item.contains("file")||!item["file"].is_string()||item["file"].get_ref<const std::string&>().empty()||item["file"].get_ref<const std::string&>().size()>32768||item["file"].get_ref<const std::string&>().find('\0')!=std::string::npos||!item.contains("additions")||!count(item["additions"])||!item.contains("deletions")||!count(item["deletions"])||!item.contains("status")||!item["status"].is_string()||(item["status"]!="added"&&item["status"]!="deleted"&&item["status"]!="modified"))throw std::runtime_error("Invalid native Git status entry");
            }
        }
        if(vcs_info){
            const auto valid_branch=[](const Json& value){return value.is_string()&&!value.get_ref<const std::string&>().empty()&&value.get_ref<const std::string&>().size()<=1024&&value.get_ref<const std::string&>().find_first_of("\r\n") == std::string::npos&&value.get_ref<const std::string&>().find('\0')==std::string::npos;};
            const auto valid_branch_object=[&](const Json& value){if(!value.is_object()||value.size()>2)return false;for(auto item=value.begin();item!=value.end();++item)if((item.key()!="current"&&item.key()!="default")||!valid_branch(item.value()))return false;return true;};
            const bool valid_provider=!result.is_object()||!result.contains("provider")||(result["provider"].is_string()&&result["provider"]=="git");
            if(!result.is_object()||result.size()<(result.contains("provider")?2U:1U)||result.size()>2||!valid_provider||!result.contains("branch")||!valid_branch_object(result["branch"]))throw std::runtime_error("Invalid native Git metadata response");
        }
        if(vcs_branches){
            if(!result.is_array()||result.size()>4096)throw std::runtime_error("Invalid native Git branch response");
            for(const auto& branch:result)if(!branch.is_string()||branch.get_ref<const std::string&>().empty()||branch.get_ref<const std::string&>().size()>1024||branch.get_ref<const std::string&>().find_first_of("\r\n")!=std::string::npos||branch.get_ref<const std::string&>().find('\0')!=std::string::npos)throw std::runtime_error("Invalid native Git branch entry");
        }
        if(vcs_base){
            if(!result.is_null()&&(!result.is_object()||result.size()!=3||!result.contains("name")||!result["name"].is_string()||result["name"].get_ref<const std::string&>().empty()||!result.contains("ref")||!result["ref"].is_string()||result["ref"].get_ref<const std::string&>().size()<40||!result.contains("source")||!result["source"].is_string()||(result["source"]!="default"&&result["source"]!="reflog")))throw std::runtime_error("Invalid native Git review base response");
        }
        if(vcs_diff){
            if(!result.is_array()||result.size()>4096)throw std::runtime_error("Invalid native Git diff response");
            const auto count=[](const Json& value){return value.is_number_unsigned()&&value.get<std::uint64_t>()<=9007199254740991ULL;};
            for(const auto& item:result)if(!item.is_object()||item.size()!=5||!item.contains("file")||!item["file"].is_string()||item["file"].get_ref<const std::string&>().empty()||item["file"].get_ref<const std::string&>().size()>32768||!item.contains("patch")||!item["patch"].is_string()||item["patch"].get_ref<const std::string&>().size()>10*1024*1024||!item.contains("additions")||!count(item["additions"])||!item.contains("deletions")||!count(item["deletions"])||!item.contains("status")||!item["status"].is_string()||(item["status"]!="added"&&item["status"]!="deleted"&&item["status"]!="modified"))throw std::runtime_error("Invalid native Git diff entry");
        }
        if(command=="session-interrupt"&&(!result.is_object()||result.size()!=1||!result.contains("interrupted")||!result["interrupted"].is_boolean()))throw std::runtime_error("Invalid native session interruption response");
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
