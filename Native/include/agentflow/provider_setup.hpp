#pragma once
#include "agentflow/agent_service.hpp"
#include "agentflow/graph_service.hpp"
namespace agentflow {
struct ProviderSetupMetadata {std::int64_t revision=0;std::string provider,model,endpoint;bool configured=false;};
// Full-access local-owner setup. The endpoint/workspace remain backend policy;
// clients supply model identity and a key, never an arbitrary destination.
class ProviderSetup {
public:
    virtual ~ProviderSetup()=default;
    virtual ProviderSetupMetadata configuration() const=0;
    virtual std::vector<std::string> discover(SecretBytes key,std::int64_t expected_revision)=0;
    virtual ProviderSetupMetadata configure(std::string model,SecretBytes key,std::int64_t expected_revision)=0;
};
class ProviderRuntime final : public RunExecutor,public ProviderSetup,public GraphExecution {
public:
    ProviderRuntime(PersistenceService& store,AgentSettings base,std::size_t workers=2,std::size_t capacity=128,
        std::string endpoint="https://api.openai.com/v1/chat/completions",
        std::string discovery_endpoint="https://api.openai.com/v1/models");
    ~ProviderRuntime();
    ProviderSetupMetadata configuration() const override;
    std::vector<std::string> discover(SecretBytes key,std::int64_t expected_revision) override;
    ProviderSetupMetadata configure(std::string model,SecretBytes key,std::int64_t expected_revision) override;
    Run submit(std::string id,std::string session,std::string prompt) override;
    Run submit_model(std::string id,std::string session,std::string prompt,std::string model) override;
    Run submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity) override;
    std::vector<std::string> models() const override;
    void cancel(const std::string& id) override;
    bool healthy() const override;
    bool available() const override;
    std::vector<GraphExecutionMetadata> graphs() const override;
    Run submit_graph(std::string id,std::string session,std::string graph,std::int64_t revision,
        std::string prompt,std::string model={}) override;
    GraphRootRecord human_input(const std::string& root,const std::string& node,
        const std::string& input,const std::string& actor,std::int64_t revision) override;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
