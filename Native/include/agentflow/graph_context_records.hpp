#pragma once
#include "agentflow/dynamic_plan_records.hpp"
#include "agentflow/context_records.hpp"

namespace agentflow {
struct GraphRootRecord {Run run;std::string graph_id;std::int64_t graph_revision;std::string specification_json;std::int64_t checkpoint_revision;std::string checkpoint_json;std::string input_json;};
// Backend-private configured authority. No provider credential or receipt is
// returned to views. Each registered agent keeps its own selected model.
struct GraphContextAgent {
    std::string node_id,model_id,provider_identity_json,backend_identity;
    std::int64_t turn_limit=16;
};
struct GraphContextSpec {
    std::string backend_identity;
    std::vector<GraphContextAgent> agents;
};
struct GraphContextOwnerRecord {
    std::string root_run_id;
    GraphContextSpec authority;
    std::optional<DynamicBudgetSegment> segment;
};
struct GraphContextOpenSpec {
    std::string root_run_id,segment_id,backend_identity;
    std::int64_t expected_checkpoint_revision=0,expected_budget_revision=0;
};
struct GraphContextBoundarySpec {
    std::string root_run_id,segment_id,backend_identity;
    std::int64_t expected_checkpoint_revision=0,expected_budget_revision=0,measured_active_elapsed_ms=0;
};
struct GraphContextOpenRecord {GraphRootRecord graph;DynamicBudgetSegment segment;};
}
