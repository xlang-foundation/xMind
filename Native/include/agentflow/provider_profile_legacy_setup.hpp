#pragma once
#include "agentflow/provider_profile_runtime.hpp"
#include "agentflow/provider_setup.hpp"
namespace agentflow {
// Compatibility adapter for existing OpenAI setup clients. All execution and
// durable setup belong to the same profile runtime; this owns no second engine.
class ProviderProfileLegacySetup final : public ProviderSetup {
public:
    ProviderProfileLegacySetup(ProviderProfileRuntime& runtime,std::vector<ProviderProfileRoute> routes,
        std::string profile="openai",std::string chat="openai.chat",std::string responses="openai.responses");
    ProviderSetupMetadata configuration()const override;
    std::vector<std::string> discover(SecretBytes key,std::int64_t expected)override;
    ProviderSetupMetadata configure(std::string model,SecretBytes key,std::int64_t expected)override;
private:
    ProviderSetupMetadata metadata(const ProviderProfileRuntimeMetadata& state)const;
    const ProviderProfileRoute& route(const std::string& id)const;
    ProviderProfileRuntime& runtime_;std::vector<ProviderProfileRoute> routes_;
    std::string profile_,chat_,responses_;
};
}
