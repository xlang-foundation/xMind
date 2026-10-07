#include "agentflow/process_configuration.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <map>
#include <set>
namespace agentflow {
namespace {
using Json=nlohmann::json;constexpr std::int64_t revision_limit=9007199254740991;
void fields(const Json& value,std::initializer_list<const char*> allowed) {
    if(!value.is_object())throw std::invalid_argument("Process configuration must contain objects");
    for(auto it=value.begin();it!=value.end();++it){bool known=false;for(const auto* key:allowed)if(it.key()==key)known=true;if(!known)throw std::invalid_argument("Unknown process configuration field");}
}
std::string text(const Json& value,const char* key,std::size_t limit) {
    if(!value.contains(key) || !value[key].is_string())throw std::invalid_argument("Missing process configuration text");const auto result=value[key].get<std::string>();
    if(result.empty() || result.size()>limit || result.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid process configuration text");return result;
}
void id(const std::string& value){if(value.empty() || value.size()>64 || value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid process profile identity");}
Json parse(const std::string& source){if(source.size()>256*1024)throw std::invalid_argument("Process configuration exceeds limits");try{return Json::parse(mcp_compact_object(source));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid process configuration JSON");}}
ProcessProfile profile(const Json& value,bool stored) {
    fields(value,{"id","executable","prefix_arguments","max_timeout_ms","revision","executable_id"});
    if(!stored && (value.contains("revision") || value.contains("executable_id")))throw std::invalid_argument("Process revision/executable binding are backend-owned");
    ProcessProfile result;result.id=text(value,"id",64);id(result.id);result.executable=text(value,"executable",32768);
    if(!std::filesystem::u8path(result.executable).is_absolute())throw std::invalid_argument("Process executable must be absolute");
    if(!value.contains("prefix_arguments") || !value["prefix_arguments"].is_array() || value["prefix_arguments"].size()>32)throw std::invalid_argument("Invalid process prefix arguments");
    for(const auto& arg:value["prefix_arguments"]){if(!arg.is_string())throw std::invalid_argument("Process prefix arguments must be strings");auto s=arg.get<std::string>();if(s.size()>4096 || s.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid process prefix argument");result.prefix_arguments.push_back(std::move(s));}
    if(!value.contains("max_timeout_ms") || !value["max_timeout_ms"].is_number_integer() || value["max_timeout_ms"]<1 || value["max_timeout_ms"]>600000)throw std::invalid_argument("Invalid process timeout budget");result.max_timeout=std::chrono::milliseconds(value["max_timeout_ms"].get<std::int64_t>());
    result.revision=0;if(stored){if(!value.contains("revision") || !value["revision"].is_number_integer() || value["revision"]<1 || value["revision"]>revision_limit)throw std::invalid_argument("Invalid stored process revision");result.revision=value["revision"].get<std::int64_t>();result.executable_id=text(value,"executable_id",512);if(!result.executable_id.starts_with("windows-local-executable-v1:"))throw std::invalid_argument("Invalid stored executable binding");}
    return result;
}
std::vector<ProcessProfile> list(const Json& value,bool stored) {
    if(!value.contains("profiles") || !value["profiles"].is_array() || value["profiles"].size()>16)throw std::invalid_argument("Invalid process profile count");
    std::set<std::string> seen;std::vector<ProcessProfile> result;for(const auto& item:value["profiles"]){auto p=profile(item,stored);if(!seen.insert(p.id).second)throw std::invalid_argument("Duplicate process profile identity");result.push_back(std::move(p));}return result;
}
Json encode(const ProcessProfile& p,bool revision=true){Json value{{"id",p.id},{"executable",p.executable},{"prefix_arguments",p.prefix_arguments},{"max_timeout_ms",p.max_timeout.count()},{"executable_id",p.executable_id}};if(revision)value["revision"]=p.revision;return value;}
Json stored(PersistenceService& store) {
    try {auto value=parse(store.information("native-process","profiles").get());fields(value,{"version","profiles","retired_ids"});if(!value.contains("version") || value["version"]!=1 || !value.contains("retired_ids") || !value["retired_ids"].is_array() || value["retired_ids"].size()>4096)throw std::invalid_argument("Invalid stored process configuration version");return value;}
    catch(const NotFound&){return {{"version",1},{"profiles",Json::array()},{"retired_ids",Json::array()}};}
}
std::set<std::string> retired(const Json& saved,const std::vector<ProcessProfile>& values) {
    std::set<std::string> active,ids;for(const auto& p:values)active.insert(p.id);
    for(const auto& value:saved["retired_ids"]){if(!value.is_string())throw std::invalid_argument("Invalid retired process profile");const auto s=value.get<std::string>();id(s);if(!ids.insert(s).second || active.contains(s))throw std::invalid_argument("Invalid retired process profile");}return ids;
}
}
std::vector<ProcessProfile> ProcessConfigurationStore::load(){const auto saved=stored(store_);const auto values=list(saved,true);(void)retired(saved,values);return values;}
std::vector<ProcessProfile> ProcessConfigurationStore::apply(const std::string& source) {
    const auto desired=parse(source);fields(desired,{"profiles"});auto values=list(desired,false);const auto saved=stored(store_);const auto previous=list(saved,true);auto retired_ids=retired(saved,previous);
    std::map<std::string,ProcessProfile> old;for(const auto& p:previous)old.emplace(p.id,p);std::set<std::string> active;Json profiles=Json::array();
    for(auto& p:values){if(retired_ids.contains(p.id))throw Conflict("Process profile identity is retired");active.insert(p.id);p.executable_id=ForegroundProcess::executable_identity(p.executable);p.revision=1;const auto found=old.find(p.id);
        if(found!=old.end()){p.revision=found->second.revision;if(encode(p,false)!=encode(found->second,false)){if(p.revision>=revision_limit)throw Conflict("Process profile revision exhausted");++p.revision;}}profiles.push_back(encode(p));}
    for(const auto& [name,p]:old){(void)p;if(!active.contains(name))retired_ids.insert(name);}if(retired_ids.size()>4096)throw Conflict("Retired process profile limit reached");
    const auto result=Json{{"version",1},{"profiles",profiles},{"retired_ids",retired_ids}}.dump();if(result.size()>256*1024)throw Conflict("Stored process configuration exceeds limits");store_.put_information("native-process","profiles",result).get();return values;
}
}
