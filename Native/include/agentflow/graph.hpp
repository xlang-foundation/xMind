#pragma once
#include "agentflow/persistence_service.hpp"
#include <optional>
namespace agentflow {
enum class GraphNodeKind {agent,tool,human};
enum class GraphNodeState {pending,running,waiting_human,completed,skipped,failed,uncertain,cancelled};
enum class GraphRestoreMode {recover,live};
struct GraphNodeDefinition {
    std::string id;
    GraphNodeKind kind;
    std::vector<std::string> dependencies;
    std::string prompt,model_id,tool,arguments_json;
    std::string condition_json;
};
// Immutable validated specification, independent of model/provider/runtime values.
class GraphPlan {
public:
    explicit GraphPlan(const std::string& json);
    const std::vector<GraphNodeDefinition>& nodes() const{return nodes_;}
    const std::vector<std::size_t>& order() const{return order_;}
    const std::string& json() const{return json_;}
private:
    std::vector<GraphNodeDefinition> nodes_;
    std::vector<std::size_t> order_;
    std::string json_;
};
struct GraphDecision {
    std::vector<std::string> ready,skippable,waiting_human,running;
    bool finished=false,halted=false;
};
struct GraphPreparedNode {GraphNodeDefinition definition;std::string dependency_outputs_json;};
// A single scheduler owner calls these transitions only after its actual
// executor/controller observations. This is not a public state mutation API.
// Persistence/child execution integration belongs to the graph service.
class GraphCoordinator {
public:
    explicit GraphCoordinator(GraphPlan plan,const std::string& checkpoint="",GraphRestoreMode mode=GraphRestoreMode::recover);
    GraphDecision inspect() const;
    GraphPreparedNode start(const std::string& id);
    void skip(const std::string& id);
    void complete(const std::string& id,const std::string& actual_output_json);
    void provide_human(const std::string& id,const std::string& authenticated_input_json);
    void fail(const std::string& id,bool effect_uncertain);
    void cancel_pending();
    GraphNodeState state(const std::string& id) const;
    std::string checkpoint() const;
private:
    struct Node {GraphNodeState state=GraphNodeState::pending;std::string output;};
    std::size_t index(const std::string& id) const;
    bool eligible(std::size_t index,bool& skipped) const;
    void output(std::size_t index,const std::string& json);
    GraphPlan plan_;
    std::vector<Node> states_;
};
struct GraphCatalogEntry {std::string id;std::int64_t revision;GraphPlan plan;};
struct GraphCatalog {std::int64_t revision=0;std::vector<GraphCatalogEntry> entries;std::vector<std::string> retired_ids;};
// Trusted startup/offline import. All database I/O goes through embedded xlang3.
// A catalog is configuration only; importing it does not execute a graph.
class GraphCatalogStore {
public:
    explicit GraphCatalogStore(PersistenceService& persistence):store_(persistence){}
    GraphCatalog load();
    GraphCatalog apply(const std::string& trusted_json);
private:
    PersistenceService& store_;
};
}
