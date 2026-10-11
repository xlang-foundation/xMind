#include "agentflow/a2a_agent_registry.hpp"
#include "agentflow/a2a_client.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <random>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr char category[]="native-a2a-agents";
constexpr char catalog_id[]="catalog";
constexpr char credential_scope[]="a2a-remote-agents";
constexpr std::int64_t maximum_revision=9007199254740991;
bool valid_id(const std::string& id){return !id.empty()&&id.size()<=64&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==std::string::npos;}
void only_fields(const Json& value,std::initializer_list<const char*> names){if(!value.is_object())throw std::invalid_argument("Invalid stored A2A agent registry");for(auto it=value.begin();it!=value.end();++it)if(std::none_of(names.begin(),names.end(),[&](const char* name){return it.key()==name;}))throw std::invalid_argument("Unknown stored A2A registry field");}
std::string text(const Json& value,const char* name,std::size_t limit){if(!value.contains(name)||!value[name].is_string())throw std::invalid_argument("Invalid stored A2A registration");auto result=value[name].get<std::string>();if(result.empty()||result.size()>limit||result.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid stored A2A registration");return result;}
std::int64_t revision(const Json& value,const char* name){if(!value.contains(name)||!value[name].is_number_integer()||value[name]<1||value[name]>maximum_revision)throw std::invalid_argument("Invalid stored A2A revision");return value[name].get<std::int64_t>();}
std::string credential_purpose(const std::string& id){return "a2a.remote-agent."+id;}
std::string random_credential_id(){std::random_device source;static constexpr char hex[]="0123456789abcdef";std::string result="remote.";for(int i=0;i<24;++i){const auto byte=static_cast<unsigned char>(source());result.push_back(hex[byte>>4]);result.push_back(hex[byte&15]);}return result;}
void validate_card_url(const std::string& url){
    A2aJsonRpcClient checked(url);const auto scheme_end=url.find("://");auto scheme=url.substr(0,scheme_end);std::transform(scheme.begin(),scheme.end(),scheme.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});if(scheme!="http")return;
    const auto authority_start=scheme_end+3,authority_end=url.find('/',authority_start);auto authority=url.substr(authority_start,authority_end==std::string::npos?url.size()-authority_start:authority_end-authority_start);std::transform(authority.begin(),authority.end(),authority.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    const auto host=authority.starts_with("[")?authority.substr(0,authority.find(']')+1):authority.substr(0,authority.find(':'));if(host!="127.0.0.1"&&host!="localhost"&&host!="[::1]")throw std::invalid_argument("Remote A2A registrations must use HTTPS");
}
std::optional<std::string> credential_id(const Json& value){if(!value.contains("credential_id")||value["credential_id"].is_null())return {};if(!value["credential_id"].is_string())throw std::invalid_argument("Invalid stored A2A credential reference");auto id=value["credential_id"].get<std::string>();if(id.empty()||id.size()>96||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid stored A2A credential reference");return id;}
}

struct A2aRemoteAgentRegistry::StoredAgent {A2aRemoteAgentDescriptor descriptor;std::optional<std::string> credential;};
struct A2aRemoteAgentRegistry::Snapshot {std::optional<std::string> encoded;std::int64_t revision=0;std::vector<StoredAgent> agents;std::vector<std::string> retired;};

A2aRemoteAgentRegistry::Snapshot A2aRemoteAgentRegistry::snapshot(){
    Snapshot result;try{result.encoded=persistence_.information(category,catalog_id).get();}catch(const NotFound&){return result;}
    Json value;try{value=Json::parse(*result.encoded);}catch(const Json::exception&){throw std::runtime_error("Stored A2A remote-agent registry is invalid");}
    only_fields(value,{"version","revision","agents","retired_ids"});if(!value.contains("version")||value["version"]!=1||!value.contains("agents")||!value["agents"].is_array()||value["agents"].size()>256||!value.contains("retired_ids")||!value["retired_ids"].is_array()||value["retired_ids"].size()>4096)throw std::runtime_error("Stored A2A remote-agent registry is invalid");
    result.revision=revision(value,"revision");std::set<std::string> ids,credentials;
    for(const auto& item:value["agents"]){only_fields(item,{"id","revision","card_url","enabled","credential_id"});A2aRemoteAgentDescriptor descriptor;descriptor.id=text(item,"id",64);if(!valid_id(descriptor.id)||!ids.insert(descriptor.id).second)throw std::runtime_error("Stored A2A remote-agent identity is invalid");descriptor.revision=revision(item,"revision");if(descriptor.revision>result.revision)throw std::runtime_error("Stored A2A revision exceeds registry revision");descriptor.card_url=text(item,"card_url",8192);validate_card_url(descriptor.card_url);if(!item.contains("enabled")||!item["enabled"].is_boolean())throw std::runtime_error("Stored A2A enabled state is invalid");descriptor.enabled=item["enabled"].get<bool>();auto credential=credential_id(item);if(credential&&!credentials.insert(*credential).second)throw std::runtime_error("A2A credential reference is duplicated");descriptor.has_bearer=credential.has_value();result.agents.push_back({std::move(descriptor),std::move(credential)});}
    for(const auto& item:value["retired_ids"]){if(!item.is_string())throw std::runtime_error("Stored retired A2A identity is invalid");auto id=item.get<std::string>();if(!valid_id(id)||!ids.insert(id).second)throw std::runtime_error("Stored A2A identity is duplicated or invalid");result.retired.push_back(std::move(id));}
    return result;
}

A2aRemoteAgentCatalog A2aRemoteAgentRegistry::list(){std::lock_guard lock(mutex_);auto current=snapshot();A2aRemoteAgentCatalog result;result.revision=current.revision;for(auto& item:current.agents)result.entries.push_back(std::move(item.descriptor));return result;}

A2aRemoteAgentDescriptor A2aRemoteAgentRegistry::publish(Snapshot& current,std::vector<StoredAgent> agents,std::vector<std::string> retired,A2aRemoteAgentDescriptor changed){
    if(current.revision>=maximum_revision)throw std::overflow_error("A2A registry revision exhausted");const auto next_revision=current.revision+1;Json entries=Json::array();for(const auto& item:agents)entries.push_back({{"id",item.descriptor.id},{"revision",item.descriptor.revision},{"card_url",item.descriptor.card_url},{"enabled",item.descriptor.enabled},{"credential_id",item.credential?Json(*item.credential):Json(nullptr)}});
    auto encoded=Json{{"version",1},{"revision",next_revision},{"agents",std::move(entries)},{"retired_ids",retired}}.dump();persistence_.compare_information(category,catalog_id,std::move(encoded),current.encoded).get();return changed;
}

A2aRemoteAgentDescriptor A2aRemoteAgentRegistry::register_agent(std::string id,std::string card_url,bool enabled,std::int64_t expected,std::optional<SecretBytes> bearer,bool clear_bearer){
    if(!valid_id(id)||expected<0||expected==maximum_revision)throw std::invalid_argument("Invalid A2A remote-agent registration precondition");validate_card_url(card_url);if(bearer&&clear_bearer)throw std::invalid_argument("Cannot set and clear a bearer credential together");if(bearer){const auto bytes=bearer->view();if(bytes.empty()||bytes.size()>4096||std::any_of(bytes.begin(),bytes.end(),[](std::uint8_t c){return c<0x21||c>0x7e;}))throw std::invalid_argument("Invalid A2A bearer credential");}
    std::lock_guard lock(mutex_);auto current=snapshot();const auto found=std::find_if(current.agents.begin(),current.agents.end(),[&](const auto& item){return item.descriptor.id==id;});const bool exists=found!=current.agents.end();if((exists?found->descriptor.revision:0)!=expected)throw Conflict("A2A registration revision changed");if(!exists&&std::find(current.retired.begin(),current.retired.end(),id)!=current.retired.end())throw Conflict("A2A registration identity is retired");
    if(exists&&found->descriptor.card_url==card_url&&found->descriptor.enabled==enabled&&!bearer&&!clear_bearer)return found->descriptor;
    if(exists&&found->descriptor.revision>=maximum_revision)throw std::overflow_error("A2A agent revision exhausted");A2aRemoteAgentDescriptor descriptor{id,card_url,exists?found->descriptor.revision+1:1,enabled,false};std::optional<std::string> next_credential=exists?found->credential:std::optional<std::string>{};
    std::optional<CredentialMetadata> created;
    if(bearer){next_credential=random_credential_id();created=persistence_.put_credential(credential_scope,*next_credential,credential_purpose(id),"A2A remote-agent bearer",std::move(*bearer),0).get();descriptor.has_bearer=true;}else if(clear_bearer){next_credential.reset();descriptor.has_bearer=false;}else descriptor.has_bearer=next_credential.has_value();
    auto next=current.agents;if(exists)next.erase(next.begin()+std::distance(current.agents.begin(),found));next.push_back({descriptor,next_credential});std::sort(next.begin(),next.end(),[](const auto& left,const auto& right){return left.descriptor.id<right.descriptor.id;});
    try{publish(current,std::move(next),current.retired,descriptor);}catch(...){if(created)try{persistence_.delete_credential(credential_scope,created->id,created->revision).get();}catch(...){}throw;}
    if(exists&&found->credential&&found->credential!=next_credential){const auto metadata=persistence_.credentials(credential_scope).get();const auto old=std::find_if(metadata.begin(),metadata.end(),[&](const auto& item){return item.id==*found->credential&&item.purpose==credential_purpose(id);});if(old!=metadata.end())persistence_.delete_credential(credential_scope,old->id,old->revision).get();}
    return descriptor;
}

A2aRemoteAgentDescriptor A2aRemoteAgentRegistry::set_enabled(const std::string& id,bool enabled,std::int64_t expected){
    std::string url;{std::lock_guard lock(mutex_);auto current=snapshot();const auto found=std::find_if(current.agents.begin(),current.agents.end(),[&](const auto& item){return item.descriptor.id==id;});if(found==current.agents.end())throw NotFound("A2A registration not found");if(found->descriptor.revision!=expected)throw Conflict("A2A registration revision changed");if(found->descriptor.enabled==enabled)return found->descriptor;url=found->descriptor.card_url;}return register_agent(id,std::move(url),enabled,expected);
}

void A2aRemoteAgentRegistry::remove(const std::string& id,std::int64_t expected){
    if(expected<1)throw std::invalid_argument("Invalid A2A removal revision");std::lock_guard lock(mutex_);auto current=snapshot();auto next=current.agents;const auto found=std::find_if(next.begin(),next.end(),[&](const auto& item){return item.descriptor.id==id;});if(found==next.end())throw NotFound("A2A registration not found");if(found->descriptor.revision!=expected)throw Conflict("A2A registration revision changed");if(current.retired.size()>=4096)throw std::overflow_error("Retired A2A identity budget exhausted");const auto credential=found->credential;next.erase(found);auto retired=current.retired;retired.push_back(id);A2aRemoteAgentDescriptor removed{id,"",expected,false,false};publish(current,std::move(next),std::move(retired),removed);if(credential){const auto metadata=persistence_.credentials(credential_scope).get();const auto old=std::find_if(metadata.begin(),metadata.end(),[&](const auto& item){return item.id==*credential&&item.purpose==credential_purpose(id);});if(old!=metadata.end())persistence_.delete_credential(credential_scope,old->id,old->revision).get();}
}

A2aResolvedRemoteAgent A2aRemoteAgentRegistry::resolve(const std::string& id){
    std::lock_guard lock(mutex_);auto current=snapshot();const auto found=std::find_if(current.agents.begin(),current.agents.end(),[&](const auto& item){return item.descriptor.id==id;});if(found==current.agents.end())throw NotFound("A2A registration not found");if(!found->descriptor.enabled)throw std::invalid_argument("A2A registration is disabled");A2aResolvedRemoteAgent result;result.descriptor=found->descriptor;if(found->credential)result.bearer.emplace(persistence_.resolve_credential(credential_scope,*found->credential,credential_purpose(id)).get());return result;
}
}
