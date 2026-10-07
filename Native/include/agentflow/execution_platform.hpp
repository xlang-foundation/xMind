#pragma once
#include "agentflow/agent_service.hpp"
#include "agentflow/graph_service.hpp"
namespace agentflow {
// A configured backend execution generation. Provider changes are permitted
// only when both single-agent and graph ownership have become idle.
class ExecutionPlatform final : public RunExecutor,public GraphExecution {
public:
    ExecutionPlatform(PersistenceService& store,AgentSettings settings,
        std::size_t workers=2,std::size_t capacity=128);
    ~ExecutionPlatform();
    Run submit(std::string id,std::string session,std::string prompt) override;
    Run submit_model(std::string id,std::string session,std::string prompt,std::string model) override;
    Run submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity) override;
    std::vector<std::string> models() const override;
    void cancel(const std::string& id) override;
    bool healthy() const override;
    bool available() const override;
    bool idle() const;
    std::vector<GraphExecutionMetadata> graphs() const override;
    Run submit_graph(std::string id,std::string session,std::string graph,std::int64_t revision,
        std::string prompt,std::string model={}) override;
    GraphRootRecord human_input(const std::string& root,const std::string& node,
        const std::string& input,const std::string& actor,std::int64_t revision) override;
private:
    PersistenceService& store_;
    std::unique_ptr<AgentService> agents_;
    std::unique_ptr<GraphService> graphs_;
};
}
