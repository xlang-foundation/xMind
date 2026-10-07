#pragma once
#include "agentflow/agent_service.hpp"
namespace agentflow {
struct ProviderSetupMetadata {std::int64_t revision=0;std::string provider,model,endpoint;bool configured=false;};
// Full-access local-owner setup. The endpoint/workspace remain backend policy;
// clients supply model identity and a key, never an arbitrary destination.
class ProviderSetup {
public:
    virtual ~ProviderSetup()=default;
    virtual ProviderSetupMetadata configuration() const=0;
    virtual ProviderSetupMetadata configure(std::string model,SecretBytes key,std::int64_t expected_revision)=0;
};
class ProviderRuntime final : public RunExecutor,public ProviderSetup {
public:
    ProviderRuntime(PersistenceService& store,AgentSettings base,std::size_t workers=2,std::size_t capacity=128,
        std::string endpoint="https://api.openai.com/v1/chat/completions");
    ~ProviderRuntime();
    ProviderSetupMetadata configuration() const override;
    ProviderSetupMetadata configure(std::string model,SecretBytes key,std::int64_t expected_revision) override;
    Run submit(std::string id,std::string session,std::string prompt) override;
    Run submit_model(std::string id,std::string session,std::string prompt,std::string model) override;
    std::vector<std::string> models() const override;
    void cancel(const std::string& id) override;
    bool healthy() const override;
    bool available() const override;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
