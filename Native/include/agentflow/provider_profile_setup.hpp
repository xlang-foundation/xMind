#pragma once
#include "agentflow/model_provider.hpp"
namespace agentflow {
struct ProviderProfileMetadata {
    std::string id,route_id,provider,model;
    std::int64_t revision=0;
};
struct ProviderProfileRuntimeMetadata {
    std::int64_t revision=0;
    std::string active;
    std::vector<ProviderProfileMetadata> profiles;
};
struct ProviderProfileRouteMetadata {
    std::string id,provider;
    ProviderWire wire=ProviderWire::chat_completions;
    bool discovery=false;
};
// Authenticated local-owner setup contract. Public metadata intentionally omits
// credential references, keys and configurable network destinations.
class ProviderProfileSetup {
public:
    virtual ~ProviderProfileSetup()=default;
    virtual ProviderProfileRuntimeMetadata configuration() const=0;
    virtual std::vector<ProviderProfileRouteMetadata> profile_routes() const=0;
    virtual std::vector<std::string> discover_models(std::string id,std::string route,SecretBytes key,
        std::int64_t expected_revision,std::stop_token cancel={})=0;
    virtual ProviderProfileRuntimeMetadata save_profile(std::string id,std::string route,std::string model,
        SecretBytes key,std::int64_t expected_revision,bool activate=false)=0;
    virtual ProviderProfileRuntimeMetadata select_profile(std::string id,std::int64_t expected_revision)=0;
};
}
