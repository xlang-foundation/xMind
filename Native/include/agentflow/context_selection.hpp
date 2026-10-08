#pragma once
#include "agentflow/context_records.hpp"

namespace agentflow {
// Pure metadata-only selection. Invalid/incomplete metadata fails closed with
// std::invalid_argument; a complete snapshot without a reclaimable prefix
// returns nullopt. The independently pinned user is not a prefix cutoff.
std::optional<ContextSelection> select_context(const ContextSnapshot&,const ContextPolicy&);

// Shared private framing, used by both repository CAS validation and the
// execution controller. Exact UTF-8 fields are length-prefixed; no original
// message, provider receipt or projection payload is loaded or normalized.
std::string context_source_binding(const ContextSnapshot&,std::int64_t covered_through);
std::string context_tail_binding(const ContextSnapshot&,std::int64_t protected_from);
std::string context_user_binding(const ContextSnapshot&,const std::vector<std::int64_t>& protected_ordinals);

// This is backend-registered evidence, never a model-supplied capacity grant.
// Absent ceilings/defaults remain absent. No built-in model/window guesses.
struct VerifiedContextCapacity {
    std::string provider_identity_json,wire,model_id;
    // Private digest of the actual registered endpoint/wire/profile route.
    // Generic {wire,model_id} provider identity alone does not bind an endpoint.
    std::string route_identity;
    std::int64_t revision=0;
    std::string source_binding;
    bool verified=false;
    std::optional<std::int64_t> input_tokens,output_tokens,context_tokens,default_output_tokens;
};
struct ContextCapacityRequest {
    ContextScope scope;
    ContextBinding binding;
    std::int64_t head_revision=0,source_watermark=0;
    std::string payload_binding,wire,model_id;
    // Computed independently from the selected native provider configuration,
    // never copied from the capacity record being checked.
    std::string route_identity;
    std::int64_t capacity_revision=0;
    std::string capacity_source_binding;
    std::optional<ContextInputMeasure> measure;
    std::optional<std::int64_t> requested_output_tokens;
    std::int64_t buffer_tokens=0,serialized_bytes=0,max_serialized_bytes=8*1024*1024;
    bool automatic_enabled=true;
};
enum class ContextCapacityTriggerScope {unknown,input_only,total_context};
struct ContextCapacityEvaluation {
    bool serialized_bytes_fit=false,compatible_measure=false,verified_capacity=false;
    std::optional<ContextMeasureSemantics> measure_semantics;
    std::optional<std::int64_t> output_allowance_tokens,trigger_input_tokens;
    std::optional<double> input_percentage,context_percentage;
    std::optional<bool> input_fit,token_fit;
    ContextCapacityTriggerScope trigger_scope=ContextCapacityTriggerScope::unknown;
    bool automatic_compaction=false;
};
ContextCapacityEvaluation evaluate_context_capacity(const ContextCapacityRequest&,
    const std::optional<VerifiedContextCapacity>& capacity={});
}
