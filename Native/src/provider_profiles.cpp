#include "agentflow/provider_profiles.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::int64_t maximum=9007199254740991;
void identity(const std::string& value){if(value.empty()||value.size()>256||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos||value.starts_with("sk-"))throw std::invalid_argument("Invalid provider profile identity");}
const char* wire_name(ProviderWire wire){switch(wire){case ProviderWire::chat_completions:return "chat-completions";case ProviderWire::responses:return "responses";case ProviderWire::anthropic_messages:return "anthropic-messages";}throw std::invalid_argument("Invalid provider profile wire");}
std::int64_t revision(const Json& value){if(!value.is_number_integer()||value<1||value>maximum)throw DatabaseError("Invalid provider profile revision");return value.get<std::int64_t>();}
std::string identifier(){std::random_device random;std::ostringstream output;output<<"profile-key-"<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)output<<std::setw(8)<<random();return output.str();}
bool same_secret(const std::string& model,const SecretBytes& key){const auto bytes=key.view();std::size_t difference=model.size()^bytes.size();for(std::size_t i=0;i<bytes.size();++i)difference|=bytes[i]^(i<model.size()?static_cast<unsigned char>(model[i]):0);return difference==0;}
Json parse(const std::string& source){if(source.size()>65536)throw DatabaseError("Provider profiles exceed limits");std::vector<std::set<std::string>> fields;return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){if(depth>16)throw DatabaseError("Provider profile nesting exceeds limits");if(event==Json::parse_event_t::object_start)fields.emplace_back();else if(event==Json::parse_event_t::object_end)fields.pop_back();else if(event==Json::parse_event_t::key&&!fields.back().insert(value.get<std::string>()).second)throw DatabaseError("Duplicate provider profile field");return true;});}
}
struct ProviderProfiles::State {ProviderProfileSnapshot snapshot;Json record={{"version",1},{"revision",0},{"active",""},{"profiles",Json::array()}};std::optional<std::string> source;};
ProviderProfiles::ProviderProfiles(PersistenceService& store,std::vector<ProviderProfileRoute> policy):store_(store),policy_(std::move(policy)){
    if(policy_.empty()||policy_.size()>64)throw std::invalid_argument("Invalid provider profile policy");std::set<std::string> names;
    for(const auto& value:policy_){identity(value.id);identity(value.provider);identity(value.credential_scope);identity(value.credential_purpose);wire_name(value.wire);if(!names.insert(value.id).second||value.endpoint.empty()||value.endpoint.size()>8192||value.endpoint.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid provider profile route");}
}
const ProviderProfileRoute& ProviderProfiles::route(const std::string& id)const{const auto found=std::find_if(policy_.begin(),policy_.end(),[&](const auto& value){return value.id==id;});if(found==policy_.end())throw std::invalid_argument("Provider route is not allowed by backend policy");return *found;}
ProviderProfiles::State ProviderProfiles::load()const{
    State state;try{state.source=store_.information("native-provider-profiles","registry").get();}catch(const NotFound&){return state;}
    try{
        state.record=parse(*state.source);const auto& record=state.record;if(!record.is_object()||record.size()!=4||!record.at("version").is_number_integer()||record["version"]!=1||!record.at("profiles").is_array()||record["profiles"].empty()||record["profiles"].size()>32)throw DatabaseError("Invalid provider profile registry");
        state.snapshot.revision=revision(record.at("revision"));state.snapshot.active=record.at("active").get<std::string>();std::set<std::string> ids;
        for(const auto& value:record["profiles"]){
            if(!value.is_object()||value.size()!=10)throw DatabaseError("Invalid provider profile record");SavedProviderProfile profile{value.at("id").get<std::string>(),value.at("route_id").get<std::string>(),value.at("model").get<std::string>(),value.at("credential_id").get<std::string>(),revision(value.at("revision"))};
            identity(profile.id);identity(profile.model);identity(profile.credential_id);const auto& policy=route(profile.route_id);
            if(!ids.insert(profile.id).second||profile.revision>state.snapshot.revision||value.at("provider")!=policy.provider||value.at("endpoint")!=policy.endpoint||value.at("wire")!=wire_name(policy.wire)||value.at("credential_scope")!=policy.credential_scope||value.at("credential_purpose")!=policy.credential_purpose)throw DatabaseError("Stored provider profile differs from backend policy");
            state.snapshot.profiles.push_back(std::move(profile));
        }
        if(!state.snapshot.active.empty()&&!ids.contains(state.snapshot.active))throw DatabaseError("Active provider profile does not exist");return state;
    }catch(const DatabaseError&){throw;}catch(...){throw DatabaseError("Stored provider profiles are invalid");}
}
ProviderProfileSnapshot ProviderProfiles::snapshot()const{return load().snapshot;}
SecretBytes ProviderProfiles::credential(const std::string& id)const{identity(id);const auto state=load();for(const auto& profile:state.snapshot.profiles)if(profile.id==id){const auto& policy=route(profile.route_id);return store_.resolve_credential(policy.credential_scope,profile.credential_id,policy.credential_purpose).get();}throw NotFound("Provider profile not found");}
ProviderProfileSnapshot ProviderProfiles::save(std::string id,std::string route_id,std::string model,SecretBytes key,std::int64_t expected,bool activate){
    identity(id);identity(model);const auto& policy=route(route_id);auto state=load();if(expected<0||expected!=state.snapshot.revision||expected>=maximum)throw Conflict("Provider profile registry revision changed");
    auto& profiles=state.record["profiles"];auto found=std::find_if(profiles.begin(),profiles.end(),[&](const Json& value){return value.at("id")==id;});
    if(found==profiles.end()&&profiles.size()>=32)throw std::invalid_argument("Provider profile limit reached");
    if(found!=profiles.end()&&found->at("provider")!=policy.provider)throw std::invalid_argument("A profile cannot change provider family");
    if(key.view().empty()){
        if(found==profiles.end())throw std::invalid_argument("New provider profile requires a key");const auto& old=route(found->at("route_id").get<std::string>());key=store_.resolve_credential(old.credential_scope,found->at("credential_id").get<std::string>(),old.credential_purpose).get();
    }
    if(key.view().size()>32768)throw std::invalid_argument("Provider profile key exceeds limits");for(auto byte:key.view())if(byte<33||byte>126)throw std::invalid_argument("Provider profile key must be printable without spaces");if(same_secret(model,key))throw std::invalid_argument("Provider key is not a model identity");
    const auto credential_id=identifier();const auto profile_revision=found==profiles.end()?1:revision(found->at("revision"))+1;
    Json profile={{"id",id},{"route_id",route_id},{"model",model},{"credential_id",credential_id},{"revision",profile_revision},{"provider",policy.provider},{"endpoint",policy.endpoint},{"wire",wire_name(policy.wire)},{"credential_scope",policy.credential_scope},{"credential_purpose",policy.credential_purpose}};
    if(found==profiles.end())profiles.push_back(std::move(profile));else *found=std::move(profile);state.record["revision"]=expected+1;if(activate)state.record["active"]=id;
    const auto encoded=state.record.dump();if(encoded.size()>65536)throw std::invalid_argument("Provider profile registry exceeds limits");
    // Candidate encryption precedes CAS; failed publication leaves an unreferenced
    // encrypted candidate and never rotates/deletes any active credential.
    store_.put_credential(policy.credential_scope,credential_id,policy.credential_purpose,"Model provider profile",std::move(key),0).get();
    store_.compare_information("native-provider-profiles","registry",encoded,state.source).get();
    state.snapshot.revision=expected+1;if(activate)state.snapshot.active=id;
    const SavedProviderProfile saved{id,route_id,model,credential_id,profile_revision};
    const auto previous=std::find_if(state.snapshot.profiles.begin(),state.snapshot.profiles.end(),[&](const auto& value){return value.id==id;});
    if(previous==state.snapshot.profiles.end())state.snapshot.profiles.push_back(saved);else *previous=saved;return state.snapshot;
}
ProviderProfileSnapshot ProviderProfiles::select(std::string id,std::int64_t expected){
    identity(id);auto state=load();if(expected<0||expected!=state.snapshot.revision||expected>=maximum)throw Conflict("Provider profile registry revision changed");
    if(std::none_of(state.snapshot.profiles.begin(),state.snapshot.profiles.end(),[&](const auto& value){return value.id==id;}))throw NotFound("Provider profile not found");
    // Confirm credential ownership before publishing selection; no key forwarding.
    auto verified=credential(id);state.record["revision"]=expected+1;state.record["active"]=id;store_.compare_information("native-provider-profiles","registry",state.record.dump(),state.source).get();state.snapshot.revision=expected+1;state.snapshot.active=id;return state.snapshot;
}
}
