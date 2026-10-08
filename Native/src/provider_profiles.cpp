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
const char* wire_name(ProviderWire wire){switch(wire){case ProviderWire::chat_completions:return "chat-completions";case ProviderWire::responses:return "responses";case ProviderWire::anthropic_messages:return "anthropic-messages";case ProviderWire::gemini_generate_content:return "gemini-generate-content";}throw std::invalid_argument("Invalid provider profile wire");}
std::int64_t revision(const Json& value){if(!value.is_number_integer()||value<1||value>maximum)throw DatabaseError("Invalid provider profile revision");return value.get<std::int64_t>();}
std::string identifier(){std::random_device random;std::ostringstream output;output<<"profile-key-"<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)output<<std::setw(8)<<random();return output.str();}
bool same_secret(const std::string& model,const SecretBytes& key){const auto bytes=key.view();std::size_t difference=model.size()^bytes.size();for(std::size_t i=0;i<bytes.size();++i)difference|=bytes[i]^(i<model.size()?static_cast<unsigned char>(model[i]):0);return difference==0;}
bool same_secret(const SecretBytes& left,const SecretBytes& right){const auto a=left.view(),b=right.view();std::size_t difference=a.size()^b.size();for(std::size_t i=0;i<std::max(a.size(),b.size());++i)difference|=(i<a.size()?a[i]:0)^(i<b.size()?b[i]:0);return difference==0;}
void config_secret(const std::string& id,const std::string& model,const SecretBytes& key){
    const auto bytes=key.view();if(bytes.empty()||bytes.size()>32768)throw std::invalid_argument("Provider configuration requires a bounded key");
    for(const auto byte:bytes)if(byte<33||byte>126)throw std::invalid_argument("Provider configuration key must be printable without spaces");
    const auto reflects=[&](const std::string& value){return std::search(value.begin(),value.end(),bytes.begin(),bytes.end(),[](char a,std::uint8_t b){return static_cast<unsigned char>(a)==b;})!=value.end();};
    if(reflects(id)||reflects(model))throw std::invalid_argument("Provider credentials are not public profile identities");
}
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
            identity(profile.id);if(!profile.model.empty())identity(profile.model);identity(profile.credential_id);const auto& policy=route(profile.route_id);
            if(!ids.insert(profile.id).second||profile.revision>state.snapshot.revision||value.at("provider")!=policy.provider||value.at("endpoint")!=policy.endpoint||value.at("wire")!=wire_name(policy.wire)||value.at("credential_scope")!=policy.credential_scope||value.at("credential_purpose")!=policy.credential_purpose)throw DatabaseError("Stored provider profile differs from backend policy");
            state.snapshot.profiles.push_back(std::move(profile));
        }
        if(!state.snapshot.active.empty()&&!ids.contains(state.snapshot.active))throw DatabaseError("Active provider profile does not exist");return state;
    }catch(const DatabaseError&){throw;}catch(...){throw DatabaseError("Stored provider profiles are invalid");}
}
ProviderProfileSnapshot ProviderProfiles::snapshot()const{return load().snapshot;}
SecretBytes ProviderProfiles::credential(const std::string& id)const{identity(id);const auto state=load();for(const auto& profile:state.snapshot.profiles)if(profile.id==id){const auto& policy=route(profile.route_id);return store_.resolve_credential(policy.credential_scope,profile.credential_id,policy.credential_purpose).get();}throw NotFound("Provider profile not found");}
ProviderProfileSnapshot ProviderProfiles::save(std::string id,std::string route_id,std::string model,SecretBytes key,std::int64_t expected,bool activate,Validator validate){
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
    const SavedProviderProfile saved{id,route_id,model,credential_id,profile_revision};
    if(validate)validate(saved,policy);
    store_.compare_information("native-provider-profiles","registry",encoded,state.source).get();
    state.snapshot.revision=expected+1;if(activate)state.snapshot.active=id;
    const auto previous=std::find_if(state.snapshot.profiles.begin(),state.snapshot.profiles.end(),[&](const auto& value){return value.id==id;});
    if(previous==state.snapshot.profiles.end())state.snapshot.profiles.push_back(saved);else *previous=saved;return state.snapshot;
}
ProviderProfileSnapshot ProviderProfiles::save_config(std::vector<ProviderProfileConfigEntry> entries,
    std::optional<std::string> active,std::int64_t expected,Validator validate_each,Validator prepare_active){
    if(entries.size()>32||(entries.empty()&&!active))throw std::invalid_argument("Provider configuration batch requires profiles or an explicit active selection");
    auto state=load();if(expected<0||expected!=state.snapshot.revision||expected>maximum)throw Conflict("Provider profile registry revision changed");
    if(active)identity(*active);
    struct Candidate {SavedProviderProfile profile;const ProviderProfileRoute* policy;SecretBytes secret;bool changed;};
    std::vector<Candidate> candidates;std::set<std::string> supplied;std::size_t additions=0;
    bool changed=active&&*active!=state.snapshot.active;
    // Finish all semantic/key/family validation before candidate encryption.
    for(auto& entry:entries){
        identity(entry.id);if(!supplied.insert(entry.id).second)throw std::invalid_argument("Duplicate provider configuration identity");
        const auto& allowed=route(entry.route_id);
        const auto old=std::find_if(state.snapshot.profiles.begin(),state.snapshot.profiles.end(),[&](const auto& value){return value.id==entry.id;});
        const bool exists=old!=state.snapshot.profiles.end();
        if(!exists&&++additions+state.snapshot.profiles.size()>32)throw std::invalid_argument("Provider profile limit reached");
        if(exists&&route(old->route_id).provider!=allowed.provider)throw std::invalid_argument("A profile cannot change provider family");
        std::string selected=entry.model?*entry.model:exists?old->model:std::string{};
        if(entry.model)identity(selected);
        std::optional<SecretBytes> previous;
        if(exists){const auto& old_policy=route(old->route_id);previous.emplace(store_.resolve_credential(old_policy.credential_scope,old->credential_id,old_policy.credential_purpose).get());}
        if(entry.key.view().empty()){
            if(!previous)throw std::invalid_argument("New provider profile requires a key");
            entry.key=SecretBytes(previous->view());
        }
        config_secret(entry.id,selected,entry.key);
        const bool identical=exists&&old->route_id==entry.route_id&&old->model==selected&&same_secret(*previous,entry.key);
        if(exists&&!identical&&old->revision>=maximum)throw Conflict("Provider profile revision exhausted");
        SavedProviderProfile candidate{entry.id,entry.route_id,std::move(selected),identical?old->credential_id:identifier(),exists?(identical?old->revision:old->revision+1):1};
        changed|=!identical;candidates.push_back({std::move(candidate),&allowed,std::move(entry.key),!identical});
    }
    auto& profiles=state.record["profiles"];
    for(const auto& candidate:candidates)if(candidate.changed){const auto& p=candidate.profile;const auto& r=*candidate.policy;
        Json encoded={{"id",p.id},{"route_id",p.route_id},{"model",p.model},{"credential_id",p.credential_id},{"revision",p.revision},{"provider",r.provider},{"endpoint",r.endpoint},{"wire",wire_name(r.wire)},{"credential_scope",r.credential_scope},{"credential_purpose",r.credential_purpose}};
        const auto old=std::find_if(profiles.begin(),profiles.end(),[&](const auto& value){return value.at("id")==p.id;});
        if(old==profiles.end())profiles.push_back(std::move(encoded));else *old=std::move(encoded);
    }
    if(active)state.record["active"]=*active;
    const auto active_id=state.record.at("active").get<std::string>();
    const auto selected=std::find_if(profiles.begin(),profiles.end(),[&](const auto& value){return value.at("id")==active_id;});
    if(!active_id.empty()&&selected==profiles.end())throw NotFound("Active provider profile not found");
    if(changed&&expected>=maximum)throw Conflict("Provider profile registry revision exhausted");
    if(changed)state.record["revision"]=expected+1;
    const auto encoded=state.record.dump();if(encoded.size()>65536)throw std::invalid_argument("Provider profile registry exceeds limits");
    for(auto& candidate:candidates)if(candidate.changed){const auto& r=*candidate.policy;store_.put_credential(r.credential_scope,candidate.profile.credential_id,r.credential_purpose,"Model provider profile",std::move(candidate.secret),0).get();}
    for(const auto& candidate:candidates)if(validate_each)validate_each(candidate.profile,*candidate.policy);
    if(prepare_active&&selected!=profiles.end()){
        const SavedProviderProfile selected_profile{selected->at("id").get<std::string>(),selected->at("route_id").get<std::string>(),selected->at("model").get<std::string>(),selected->at("credential_id").get<std::string>(),revision(selected->at("revision"))};
        const auto& r=route(selected_profile.route_id);
        auto verified=store_.resolve_credential(r.credential_scope,selected_profile.credential_id,r.credential_purpose).get();
        config_secret(selected_profile.id,selected_profile.model,verified);
        prepare_active(selected_profile,r);
    }
    // One publication for the entire document. Even a no-op verifies the exact
    // old registry so another native writer cannot be silently ignored.
    ProviderProfileSnapshot next=state.snapshot;next.revision=changed?expected+1:expected;next.active=active_id;
    for(const auto& candidate:candidates){const auto found=std::find_if(next.profiles.begin(),next.profiles.end(),[&](const auto& value){return value.id==candidate.profile.id;});if(found==next.profiles.end())next.profiles.push_back(candidate.profile);else *found=candidate.profile;}
    store_.compare_information("native-provider-profiles","registry",encoded,state.source).get();
    // Return precisely the candidate that won this CAS, never a fresh load
    // that could belong to a later concurrent writer/prepared service.
    return next;
}
ProviderProfileSnapshot ProviderProfiles::select(std::string id,std::int64_t expected,Validator validate){
    identity(id);auto state=load();if(expected<0||expected!=state.snapshot.revision||expected>=maximum)throw Conflict("Provider profile registry revision changed");
    const auto selected=std::find_if(state.snapshot.profiles.begin(),state.snapshot.profiles.end(),[&](const auto& value){return value.id==id;});
    if(selected==state.snapshot.profiles.end())throw NotFound("Provider profile not found");
    // Confirm credential ownership before publishing selection; no key forwarding.
    const auto& policy=route(selected->route_id);
    auto verified=store_.resolve_credential(policy.credential_scope,selected->credential_id,policy.credential_purpose).get();
    if(validate)validate(*selected,policy);
    state.record["revision"]=expected+1;state.record["active"]=id;store_.compare_information("native-provider-profiles","registry",state.record.dump(),state.source).get();state.snapshot.revision=expected+1;state.snapshot.active=id;return state.snapshot;
}
ProviderProfileSnapshot ProviderProfiles::import_existing(std::string id,std::string route_id,std::string model,std::string credential_id,std::int64_t initial,Validator validate){
    identity(id);if(!model.empty())identity(model);identity(credential_id);const auto& policy=route(route_id);
    if(initial<1||initial>maximum)throw std::invalid_argument("Invalid imported provider revision");
    auto state=load();if(state.source)throw Conflict("Provider profile registry already exists");
    auto verified=store_.resolve_credential(policy.credential_scope,credential_id,policy.credential_purpose).get();
    if(same_secret(model,verified))throw std::invalid_argument("Provider key is not a model identity");
    const SavedProviderProfile saved{id,route_id,model,credential_id,initial};
    state.record["revision"]=initial;state.record["active"]=id;
    state.record["profiles"].push_back({{"id",id},{"route_id",route_id},{"model",model},{"credential_id",credential_id},{"revision",initial},{"provider",policy.provider},{"endpoint",policy.endpoint},{"wire",wire_name(policy.wire)},{"credential_scope",policy.credential_scope},{"credential_purpose",policy.credential_purpose}});
    if(validate)validate(saved,policy);
    const auto encoded=state.record.dump();if(encoded.size()>65536)throw std::invalid_argument("Provider profile registry exceeds limits");
    store_.compare_information("native-provider-profiles","registry",encoded,{}).get();
    return {initial,id,{saved}};
}
}
