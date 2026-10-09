#pragma once
#include "agentflow/graph_runner.hpp"
#include "agentflow/run_executor.hpp"

namespace agentflow {
struct GraphExecutionMetadata {
    std::string id;
    std::int64_t revision;
    std::size_t node_count;
    bool executable;
};
struct GraphContextMetadata {
    bool enabled=false,resumable=false;
    std::optional<std::int64_t> remaining_active_ms;
};
// Access adapters authenticate/authorize callers before invoking this boundary.
// Plans are immutable backend catalog entries, never request-supplied code.
class GraphExecution {
public:
    virtual ~GraphExecution()=default;
    virtual bool healthy()const{return false;}
    virtual bool idle()const{return false;}
    virtual std::vector<GraphExecutionMetadata> graphs() const=0;
    virtual bool supports_graph_profile_admission()const{return false;}
    virtual Run submit_graph_workspace(std::string,std::string,std::string,std::int64_t,std::string,std::string,
        WorkspaceAdmission,std::optional<ProviderProfileAdmission> = {}){throw RunUnavailable("Graph workspace-bound admission is unavailable");}
    virtual Run submit_graph_profile(std::string,std::string,std::string,std::int64_t,std::string,std::string,ProviderProfileAdmission){throw RunUnavailable("Graph provider profile admission is unavailable");}
    virtual Run submit_graph(std::string id,std::string session,std::string graph,
        std::int64_t revision,std::string prompt,std::string model={})=0;
    virtual GraphRootRecord human_input(const std::string& root,const std::string& node,
        const std::string& input,const std::string& actor,std::int64_t revision)=0;
    virtual Run resume_graph(const std::string&,const std::string&,std::int64_t){throw RunUnavailable("Graph resume is unavailable");}
    virtual GraphContextMetadata graph_context(const std::string&)const{return {};}
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
    Run resume_graph(const std::string& root,const std::string& authenticated_actor,
        std::int64_t expected_checkpoint_revision) override;
    GraphContextMetadata graph_context(const std::string& root)const override;
    void cancel(const std::string& id,const std::string& actor);
    bool healthy() const override;
    bool idle() const override;
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
