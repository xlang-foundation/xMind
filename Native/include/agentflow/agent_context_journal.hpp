#pragma once

#include "agentflow/repository.hpp"
#include <future>

namespace agentflow {

// Owned-value backend port for context snapshots and maintenance outcomes.
// Provider work stays in AgentModelGateway; no repository or runtime handle
// crosses this boundary.
class AgentContextJournal {
public:
    virtual ~AgentContextJournal()=default;
    virtual std::future<ContextSnapshot> context_snapshot(ContextScope scope,ContextBinding binding,ContextReadBound bound={})=0;
    virtual std::future<std::vector<ContextGroupPayload>> context_group_payloads(ContextSnapshot snapshot,std::vector<std::int64_t> ordinals,ContextReadBound bound={})=0;
    virtual std::future<std::vector<ContextHistoryPayload>> context_history_payloads(ContextSnapshot snapshot,std::vector<std::int64_t> ordinals,ContextReadBound bound={})=0;
    virtual std::future<void> record_context_history_replay(ContextHistoryReplaySpec spec)=0;
    virtual std::future<std::optional<ContextProjection>> context_projection(ContextScope scope,ContextBinding binding,ContextReadBound bound={})=0;
    virtual std::future<ContextStepReservation> begin_context_compaction(ContextCompactionSpec spec)=0;
    virtual std::future<ContextStepReservation> start_context_compaction(std::string id,std::string maintenance_attempt)=0;
    virtual std::future<ContextProjection> record_context_compaction_response(ContextCompactionCommit result)=0;
    virtual std::future<ContextProjection> commit_context_compaction(ContextCompactionCommit result)=0;
    virtual std::future<void> retire_context_compaction(ContextCompactionFailure failure)=0;
    virtual std::future<ContextManualRequest> retire_context_request(std::string id,ContextBinding binding,ContextFailureCode code)=0;
    virtual std::future<ContextMeasureReservation> begin_context_measure(ContextMeasureRequestSpec spec)=0;
    virtual std::future<ContextMeasureReservation> start_context_measure(std::string id)=0;
    virtual std::future<void> finish_context_measure(ContextInputMeasure measure)=0;
    virtual std::future<void> retire_context_measure(std::string id,ContextFailureCode code,std::int64_t elapsed_ms)=0;
    virtual std::future<InferenceStepRecord> reserve_inference_step(InferenceStepSpec spec)=0;
    virtual std::future<InferenceStepRecord> reserve_inference_rebuild(InferenceRebuildSpec spec)=0;
};

}
