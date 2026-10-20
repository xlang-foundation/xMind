#include "agentflow/agent_worker_closed_ports.hpp"
#include "agentflow/a2a_tool_registry.hpp"

namespace agentflow {
namespace {
class ClosedContext final:public AgentContextJournal {
public:
    std::future<ContextSnapshot> context_snapshot([[maybe_unused]] ContextScope scope,[[maybe_unused]] ContextBinding binding,[[maybe_unused]] ContextReadBound bound={})override{throw std::logic_error("Worker context capability is not admitted: context_snapshot");}
    std::future<std::vector<ContextGroupPayload>> context_group_payloads([[maybe_unused]] ContextSnapshot snapshot,[[maybe_unused]] std::vector<std::int64_t> ordinals,[[maybe_unused]] ContextReadBound bound={})override{throw std::logic_error("Worker context capability is not admitted: context_group_payloads");}
    std::future<std::vector<ContextHistoryPayload>> context_history_payloads([[maybe_unused]] ContextSnapshot snapshot,[[maybe_unused]] std::vector<std::int64_t> ordinals,[[maybe_unused]] ContextReadBound bound={})override{throw std::logic_error("Worker context capability is not admitted: context_history_payloads");}
    std::future<void> record_context_history_replay([[maybe_unused]] ContextHistoryReplaySpec spec)override{throw std::logic_error("Worker context capability is not admitted: record_context_history_replay");}
    std::future<ProviderHistorySnapshot> provider_history([[maybe_unused]] ProviderHistoryOwner owner,[[maybe_unused]] ProviderHistorySelection selection={})override{throw std::logic_error("Worker context capability is not admitted: provider_history");}
    std::future<void> record_provider_history_replay([[maybe_unused]] ProviderHistoryReplaySpec spec)override{throw std::logic_error("Worker context capability is not admitted: record_provider_history_replay");}
    std::future<std::optional<ContextProjection>> context_projection([[maybe_unused]] ContextScope scope,[[maybe_unused]] ContextBinding binding,[[maybe_unused]] ContextReadBound bound={})override{throw std::logic_error("Worker context capability is not admitted: context_projection");}
    std::future<ContextStepReservation> begin_context_compaction([[maybe_unused]] ContextCompactionSpec spec)override{throw std::logic_error("Worker context capability is not admitted: begin_context_compaction");}
    std::future<ContextStepReservation> start_context_compaction([[maybe_unused]] std::string id,[[maybe_unused]] std::string maintenance_attempt)override{throw std::logic_error("Worker context capability is not admitted: start_context_compaction");}
    std::future<ContextProjection> record_context_compaction_response([[maybe_unused]] ContextCompactionCommit result)override{throw std::logic_error("Worker context capability is not admitted: record_context_compaction_response");}
    std::future<ContextProjection> commit_context_compaction([[maybe_unused]] ContextCompactionCommit result)override{throw std::logic_error("Worker context capability is not admitted: commit_context_compaction");}
    std::future<void> retire_context_compaction([[maybe_unused]] ContextCompactionFailure failure)override{throw std::logic_error("Worker context capability is not admitted: retire_context_compaction");}
    std::future<ContextManualRequest> retire_context_request([[maybe_unused]] std::string id,[[maybe_unused]] ContextBinding binding,[[maybe_unused]] ContextFailureCode code)override{throw std::logic_error("Worker context capability is not admitted: retire_context_request");}
    std::future<ContextMeasureReservation> begin_context_measure([[maybe_unused]] ContextMeasureRequestSpec spec)override{throw std::logic_error("Worker context capability is not admitted: begin_context_measure");}
    std::future<ContextMeasureReservation> start_context_measure([[maybe_unused]] std::string id)override{throw std::logic_error("Worker context capability is not admitted: start_context_measure");}
    std::future<void> finish_context_measure([[maybe_unused]] ContextInputMeasure measure)override{throw std::logic_error("Worker context capability is not admitted: finish_context_measure");}
    std::future<void> retire_context_measure([[maybe_unused]] std::string id,[[maybe_unused]] ContextFailureCode code,[[maybe_unused]] std::int64_t elapsed_ms)override{throw std::logic_error("Worker context capability is not admitted: retire_context_measure");}
    std::future<InferenceStepRecord> reserve_inference_step([[maybe_unused]] InferenceStepSpec spec)override{throw std::logic_error("Worker context capability is not admitted: reserve_inference_step");}
    std::future<InferenceStepRecord> reserve_inference_rebuild([[maybe_unused]] InferenceRebuildSpec spec)override{throw std::logic_error("Worker context capability is not admitted: reserve_inference_rebuild");}
};
class ClosedCatalogue final:public AgentBackendCatalog {
public:
    std::vector<AgentAuthorityCredentialVersion> credential_versions(const AgentSettings&)override{throw std::logic_error("Worker credential catalogue capability is not admitted");}
    AgentDefinitionCatalog agent_definitions()override{throw std::logic_error("Worker agent catalogue capability is not admitted");}
};
class ClosedTools final:public AgentToolHostFactory {
public:
    std::unique_ptr<AgentFileToolHost> file(AgentFileToolOwnerContext)override{throw std::logic_error("Worker native file effect capability is not admitted");}
    std::unique_ptr<AgentProcessOwnerHost> process(AgentProcessToolOwnerContext)override{throw std::logic_error("Worker native process owner capability is not admitted");}
    std::unique_ptr<AgentReadToolHost> read(AgentReadToolOwnerContext)override{throw std::logic_error("Worker child-local read capability is not admitted; use the captured read proxy");}
    std::unique_ptr<AgentRemoteToolHost> remote(AgentRemoteToolOwnerContext)override{throw std::logic_error("Worker native remote effect capability is not admitted");}
    std::unique_ptr<AgentGuidanceToolHost> guidance(AgentGuidanceToolOwnerContext)override{throw std::logic_error("Worker child-local guidance capability is not admitted");}
    std::unique_ptr<AgentCallableToolHost> callable(AgentCallableToolOwnerContext)override{throw std::logic_error("Worker trusted callable capability is not admitted");}
};
class ClosedRuntime final:public AgentBackendRuntime {
public:
    std::unique_ptr<AgentProcessToolHost> process(WorkspaceTools&,const std::string&,std::vector<ProcessProfile>)override{throw std::logic_error("Worker native process runtime capability is not admitted");}
    std::shared_ptr<AgentMcpPeer> connect_mcp(const McpServerSetting&,std::chrono::steady_clock::time_point,std::stop_token)override{throw std::logic_error("Worker native MCP runtime capability is not admitted");}
    A2aRemoteAgentCatalog a2a_catalog()override{throw std::logic_error("Worker native A2A catalogue capability is not admitted");}
    std::unique_ptr<A2aAgentToolRegistry> discover_a2a(WorkspaceTools&,std::chrono::milliseconds,std::stop_token)override{throw std::logic_error("Worker native A2A runtime capability is not admitted");}
};
}
AgentWorkerClosedPorts make_agent_worker_closed_ports(){return {std::make_unique<ClosedContext>(),std::make_shared<ClosedCatalogue>(),std::make_shared<ClosedTools>(),std::make_shared<ClosedRuntime>()};}
}
