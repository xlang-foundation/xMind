#include "agentflow/provider_profile_legacy_setup.hpp"
#include <algorithm>
namespace agentflow {
ProviderProfileLegacySetup::ProviderProfileLegacySetup(ProviderProfileRuntime& runtime,std::vector<ProviderProfileRoute> routes,std::string profile,std::string chat,std::string responses)
    :runtime_(runtime),routes_(std::move(routes)),profile_(std::move(profile)),chat_(std::move(chat)),responses_(std::move(responses)){
    const auto& primary=route(chat_);if(primary.provider!="openai"||primary.wire!=ProviderWire::chat_completions)throw std::invalid_argument("Invalid legacy Chat route");
    if(!responses_.empty()){const auto& response=route(responses_);if(response.provider!="openai"||response.wire!=ProviderWire::responses)throw std::invalid_argument("Invalid legacy Responses route");}
}
const ProviderProfileRoute& ProviderProfileLegacySetup::route(const std::string& id)const{
    const auto found=std::find_if(routes_.begin(),routes_.end(),[&](const auto& value){return value.id==id;});
    if(found==routes_.end())throw std::invalid_argument("Legacy provider route is unavailable");return *found;
}
ProviderSetupMetadata ProviderProfileLegacySetup::configuration()const{
    return metadata(runtime_.configuration());
}
ProviderSetupMetadata ProviderProfileLegacySetup::metadata(const ProviderProfileRuntimeMetadata& state)const{
    const auto selected=std::find_if(state.profiles.begin(),state.profiles.end(),[&](const auto& value){return value.id==state.active;});
    if(selected==state.profiles.end()){const auto& primary=route(chat_);return {state.revision,primary.provider,"",primary.endpoint,false,primary.wire};}
    const auto& allowed=route(selected->route_id);return {state.revision,allowed.provider,selected->model,allowed.endpoint,true,allowed.wire};
}
std::vector<std::string> ProviderProfileLegacySetup::discover(SecretBytes key,std::int64_t expected){
    const auto state=runtime_.configuration();if(state.revision!=expected)throw Conflict("Provider configuration revision changed");
    const auto saved=std::find_if(state.profiles.begin(),state.profiles.end(),[&](const auto& value){return value.id==profile_;});
    return runtime_.discover_models(profile_,saved==state.profiles.end()?chat_:saved->route_id,std::move(key),expected);
}
ProviderSetupMetadata ProviderProfileLegacySetup::configure(std::string model,SecretBytes key,std::int64_t expected){
    bool responses=false;
    if(!responses_.empty())for(const auto* family:{"gpt-5.4","gpt-5.5","gpt-5.6","gpt-6","gpt-6.1"})if(model==family||model.starts_with(std::string(family)+"-"))responses=true;
    return metadata(runtime_.save_profile(profile_,responses?responses_:chat_,std::move(model),std::move(key),expected,true));
}
}
