#include "agentflow/connection_profiles.hpp"
#include "agentflow/authoring_document.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <set>
#include <string_view>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::int64_t revision_limit=9007199254740991;
void fields(const Json& value,std::initializer_list<const char*> allowed){
    if(!value.is_object())throw std::invalid_argument("Connection profile values must be objects");
    for(auto it=value.begin();it!=value.end();++it)if(std::none_of(allowed.begin(),allowed.end(),[&](const char* key){return it.key()==key;}))throw std::invalid_argument("Unknown or backend-owned connection profile field");
}
std::string text(const Json& value,const char* key,std::size_t limit,bool allow_empty=false){
    if(!value.contains(key)||!value.at(key).is_string())throw std::invalid_argument("Missing connection profile text");
    auto result=value.at(key).get<std::string>();if((!allow_empty&&result.empty())||result.size()>limit||result.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid connection profile text");return result;
}
void identity(const std::string& value){if(value.empty()||value.size()>64||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid connection profile identity");}
std::string kind_name(ConnectionKind kind){return kind==ConnectionKind::local?"local":"nexus";}
ConnectionKind parse_kind(const std::string& value){if(value=="local")return ConnectionKind::local;if(value=="nexus")return ConnectionKind::nexus;throw std::invalid_argument("Unsupported connection profile kind");}
void origin(const std::string& value){
    if(value.empty()||value.size()>8192||!value.starts_with("https://")||value.find_first_of("?#@%\\ \t\r\n")!=std::string::npos)throw std::invalid_argument("Nexus profiles require a canonical HTTPS origin without credentials, query or fragment");
    constexpr std::size_t authority_begin=8;
    const auto authority_end=value.find('/',authority_begin);const auto authority=value.substr(authority_begin,authority_end==std::string::npos?std::string::npos:authority_end-authority_begin);
    if(authority.empty())throw std::invalid_argument("Invalid Nexus origin host");
    std::string host,port;
    if(authority.front()=='['){const auto close=authority.find(']');if(close==std::string::npos||close<3||authority.substr(1,close-1).find(':')==std::string::npos)throw std::invalid_argument("Invalid bracketed Nexus IPv6 host");host=authority.substr(1,close-1);for(unsigned char c:host)if(!(std::isxdigit(c)||c==':'||c=='.'))throw std::invalid_argument("Invalid bracketed Nexus IPv6 host");if(close+1<authority.size()){if(authority[close+1]!=':')throw std::invalid_argument("Invalid Nexus origin port");port=authority.substr(close+2);}}
    else{const auto colon=authority.find(':');if(colon!=std::string::npos){if(authority.find(':',colon+1)!=std::string::npos)throw std::invalid_argument("IPv6 Nexus hosts must be bracketed");host=authority.substr(0,colon);port=authority.substr(colon+1);}else host=authority;
        if(host.size()>253||host.front()=='.'||host.back()=='.')throw std::invalid_argument("Invalid Nexus origin host");
        std::size_t label_begin=0;while(label_begin<host.size()){const auto dot=host.find('.',label_begin);const auto end=dot==std::string::npos?host.size():dot;const auto length=end-label_begin;if(length==0||length>63||host[label_begin]=='-'||host[end-1]=='-')throw std::invalid_argument("Invalid Nexus origin host label");for(std::size_t i=label_begin;i<end;++i)if(!(std::isalnum(static_cast<unsigned char>(host[i]))||host[i]=='-'))throw std::invalid_argument("Invalid Nexus origin host label");if(dot==std::string::npos)break;label_begin=dot+1;}}
    if(!port.empty()){unsigned value_port=0;const auto parsed=std::from_chars(port.data(),port.data()+port.size(),value_port);if(parsed.ec!=std::errc{}||parsed.ptr!=port.data()+port.size()||value_port==0||value_port>65535)throw std::invalid_argument("Invalid Nexus origin port");}else if(authority.back()==':')throw std::invalid_argument("Invalid Nexus origin port");
    const auto path=authority_end==std::string::npos?std::string{}:value.substr(authority_end);
    for(unsigned char c:path)if(!(std::isalnum(c)||std::string_view("/._~!$&'()*+,;=:-").find(static_cast<char>(c))!=std::string_view::npos))throw std::invalid_argument("Invalid Nexus origin path");
    if(value.back()=='/')throw std::invalid_argument("Nexus origin must omit a trailing slash");
}
Json parse(const std::string& source){
    if(source.size()>262144)throw std::invalid_argument("Connection profile catalog exceeds limits");const auto first=source.find_first_not_of(" \t\r\n");if(first==std::string::npos)throw std::invalid_argument("Empty connection profile catalog");
    try{return Json::parse(mcp_compact_object(source[first]=='{'||source[first]=='['?source:authoring_yaml_to_json(source)));}
    catch(const McpProtocolError&){throw std::invalid_argument("Invalid connection profile JSON or YAML");}
    catch(const std::exception&){throw std::invalid_argument("Invalid connection profile JSON or YAML");}
}
ConnectionProfile decode_profile(const Json& value,bool stored){
    fields(value,stored?std::initializer_list<const char*>({"id","name","kind","origin","revision"}):std::initializer_list<const char*>({"id","name","kind","origin"}));
    ConnectionProfile result;result.id=text(value,"id",64);identity(result.id);result.name=text(value,"name",128);result.kind=parse_kind(text(value,"kind",16));
    result.origin=value.contains("origin")?text(value,"origin",8192,true):std::string{};
    if(result.kind==ConnectionKind::local){if(result.id!="local"||result.origin.size()!=0)throw std::invalid_argument("Local connection profile must use the reserved ID and have no remote origin");}
    else{if(result.id=="local")throw std::invalid_argument("The local connection identity is reserved");origin(result.origin);}
    if(stored){if(!value.contains("revision")||!value.at("revision").is_number_integer()||value.at("revision")<1||value.at("revision")>revision_limit)throw std::invalid_argument("Invalid stored connection profile revision");result.revision=value.at("revision").get<std::int64_t>();}
    return result;
}
Json encode_profile(const ConnectionProfile& value){return {{"id",value.id},{"name",value.name},{"kind",kind_name(value.kind)},{"origin",value.origin},{"revision",value.revision}};}
ConnectionProfileCatalog default_catalog(){return {1,"local",{{"local","Local","",ConnectionKind::local,1}},{}};}
Json encode_catalog(const ConnectionProfileCatalog& value){auto profiles=Json::array();for(const auto& profile:value.profiles)profiles.push_back(encode_profile(profile));return {{"version",1},{"revision",value.revision},{"active_id",value.active_id},{"profiles",std::move(profiles)},{"retired_ids",value.retired_ids}};}
ConnectionProfileCatalog decode_catalog(const Json& value){
    fields(value,{"version","revision","active_id","profiles","retired_ids"});
    if(!value.contains("version")||value.at("version")!=1||!value.contains("revision")||!value.at("revision").is_number_integer()||value.at("revision")<1||value.at("revision")>revision_limit||!value.contains("active_id")||!value.at("active_id").is_string()||!value.contains("profiles")||!value.at("profiles").is_array()||value.at("profiles").size()>32||!value.contains("retired_ids")||!value.at("retired_ids").is_array()||value.at("retired_ids").size()>4096)throw std::invalid_argument("Invalid stored connection profile catalog");
    ConnectionProfileCatalog result;result.revision=value.at("revision").get<std::int64_t>();result.active_id=value.at("active_id").get<std::string>();std::set<std::string> ids;bool local=false,active=false;
    for(const auto& item:value.at("profiles")){auto profile=decode_profile(item,true);if(profile.revision>result.revision||!ids.insert(profile.id).second)throw std::invalid_argument("Invalid or duplicate stored connection profile");local|=profile.id=="local";active|=profile.id==result.active_id;result.profiles.push_back(std::move(profile));}
    if(!local||!active)throw std::invalid_argument("Stored connection catalog must retain Local and an active profile");
    for(const auto& item:value.at("retired_ids")){if(!item.is_string())throw std::invalid_argument("Invalid retired connection profile ID");const auto id=item.get<std::string>();identity(id);if(id=="local"||!ids.insert(id).second)throw std::invalid_argument("Duplicate or active retired connection profile ID");result.retired_ids.push_back(id);}
    return result;
}
bool same(const ConnectionProfile& left,const ConnectionProfile& right){return left.id==right.id&&left.name==right.name&&left.kind==right.kind&&left.origin==right.origin;}
}
ConnectionProfileCatalog ConnectionProfiles::load_unlocked(){
    try{return decode_catalog(Json::parse(store_.information("native-connections","profiles").get()));}
    catch(const NotFound&){return {};}
    catch(const Json::exception&){throw DatabaseError("Stored connection profile catalog is invalid");}
}
ConnectionProfileCatalog ConnectionProfiles::load(){std::lock_guard lock(mutex_);return load_unlocked();}
ConnectionProfileCatalog ConnectionProfiles::load_or_initialize(){std::lock_guard lock(mutex_);auto current=load_unlocked();if(current.revision)return current;auto initial=default_catalog();store_.put_information("native-connections","profiles",encode_catalog(initial).dump()).get();return initial;}
ConnectionProfileCatalog ConnectionProfiles::apply(const std::string& source,std::int64_t expected_revision){
    std::lock_guard lock(mutex_);auto previous=load_unlocked();if(expected_revision<0||expected_revision!=previous.revision||expected_revision>revision_limit)throw Conflict("Connection profile catalog revision changed");
    const auto desired=parse(source);fields(desired,{"profiles"});if(!desired.contains("profiles")||!desired.at("profiles").is_array()||desired.at("profiles").size()>32)throw std::invalid_argument("Connection catalog accepts at most 32 profiles");
    ConnectionProfileCatalog next;next.revision=previous.revision;next.active_id=previous.active_id.empty()?"local":previous.active_id;next.retired_ids=previous.retired_ids;std::set<std::string> ids;
    for(const auto& item:desired.at("profiles")){
        auto candidate=decode_profile(item,false);if(!ids.insert(candidate.id).second)throw std::invalid_argument("Duplicate connection profile identity");
        if(std::find(previous.retired_ids.begin(),previous.retired_ids.end(),candidate.id)!=previous.retired_ids.end())throw Conflict("Retired connection profile IDs cannot be reused");
        const auto old=std::find_if(previous.profiles.begin(),previous.profiles.end(),[&](const auto& profile){return profile.id==candidate.id;});
        if(old==previous.profiles.end())candidate.revision=1;
        else if(same(*old,candidate))candidate.revision=old->revision;
        else{if(old->revision>=revision_limit)throw Conflict("Connection profile revision exhausted");candidate.revision=old->revision+1;}
        next.profiles.push_back(std::move(candidate));
    }
    const auto local=std::find_if(next.profiles.begin(),next.profiles.end(),[](const auto& profile){return profile.id=="local";});if(local==next.profiles.end()||local->kind!=ConnectionKind::local)throw std::invalid_argument("Every connection catalog must retain Local");
    for(const auto& old:previous.profiles)if(std::none_of(next.profiles.begin(),next.profiles.end(),[&](const auto& profile){return profile.id==old.id;})){if(old.id=="local")throw std::invalid_argument("The Local profile cannot be removed");if(next.retired_ids.size()>=4096)throw Conflict("Retired connection profile limit reached");next.retired_ids.push_back(old.id);}
    if(std::none_of(next.profiles.begin(),next.profiles.end(),[&](const auto& profile){return profile.id==next.active_id;}))throw Conflict("The active connection profile cannot be removed");
    bool changed=previous.revision==0||next.profiles.size()!=previous.profiles.size()||next.retired_ids!=previous.retired_ids;
    for(std::size_t i=0;!changed&&i<next.profiles.size();++i)changed=!same(next.profiles[i],previous.profiles[i])||next.profiles[i].revision!=previous.profiles[i].revision;
    if(!changed)return previous;if(previous.revision>=revision_limit)throw Conflict("Connection profile catalog revision exhausted");next.revision=previous.revision+1;
    store_.put_information("native-connections","profiles",encode_catalog(next).dump()).get();return next;
}
ConnectionProfileCatalog ConnectionProfiles::select(const std::string& id,std::int64_t expected_revision){
    identity(id);std::lock_guard lock(mutex_);auto current=load_unlocked();if(expected_revision<0||expected_revision!=current.revision||expected_revision>revision_limit)throw Conflict("Connection profile catalog revision changed");
    const auto selected=std::find_if(current.profiles.begin(),current.profiles.end(),[&](const auto& profile){return profile.id==id;});
    if(selected==current.profiles.end())throw NotFound("Connection profile not found");
    if(selected->kind!=ConnectionKind::local)throw Conflict("Nexus profile is not authenticated; the active connection remains unchanged");
    if(current.active_id==id)return current;if(current.revision>=revision_limit)throw Conflict("Connection profile catalog revision exhausted");current.active_id=id;++current.revision;store_.put_information("native-connections","profiles",encode_catalog(current).dump()).get();return current;
}
}
