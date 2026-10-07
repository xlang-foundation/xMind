#pragma once
#include "agentflow/graph_runner.hpp"

namespace agentflow {
struct GraphExecutionMetadata {
    std::string id;
    std::int64_t revision;
    std::size_t node_count;
    bool executable;
};
// Access adapters authenticate/authorize callers before invoking this boundary.
// Plans are immutable backend catalog entries, never request-supplied code.
class GraphExecution {
public:
    virtual ~GraphExecution()=default;
    virtual std::vector<GraphExecutionMetadata> graphs() const=0;
    virtual Run submit_graph(std::string id,std::string session,std::string graph,
        std::int64_t revision,std::string prompt,std::string model={})=0;
    virtual GraphRootRecord human_input(const std::string& root,const std::string& node,
        const std::string& input,const std::string& actor,std::int64_t revision)=0;
};
// Owns queued/running/paused graph roots independently of connected views.
// Persistence must outlive the service and its bounded worker pool.
class GraphService final : public GraphExecution {
public:
    GraphService(PersistenceService& store,AgentSettings settings,
        std::size_t workers=2,std::size_t capacity=128,std::size_t node_parallel=2);
    ~GraphService();
    std::vector<GraphExecutionMetadata> graphs() const override;
    Run submit_graph(std::string id,std::string session,std::string graph,
        std::int64_t revision,std::string prompt,std::string model={}) override;
    GraphRootRecord human_input(const std::string& root,const std::string& node,
        const std::string& input,const std::string& actor,std::int64_t revision) override;
    void cancel(const std::string& id,const std::string& actor);
    bool healthy() const;
    bool idle() const;
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
