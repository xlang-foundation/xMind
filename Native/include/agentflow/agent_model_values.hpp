#pragma once
#include "agentflow/model_provider.hpp"
namespace agentflow {
struct Message;
// One shared history projection for the native loop and backend-owned windows.
ModelMessage model_message_from_history(const Message&);
// Model-only file bounds and closed inspection-group references. Originals stay
// exact; callers can remap protected provider-message positions after compaction.
bool bound_historical_file_reads(ModelRequest&,std::vector<std::size_t>* protected_indices=nullptr);
std::string execution_budget_advisory(std::int64_t remaining_ms,std::size_t inspections);
// Owned native DTOs, not provider configuration or credential envelopes.
// Opaque provider strings/canonical windows retain exact supplied bytes.
std::string encode_agent_model_request(const ModelRequest&);
ModelRequest decode_agent_model_request(const std::string&);
std::string encode_agent_model_completion(const ModelCompletion&);
ModelCompletion decode_agent_model_completion(const std::string&);
std::string encode_agent_model_event(const ModelEvent&);
ModelEvent decode_agent_model_event(const std::string&);
// Shared native assistant rendering; timing/context are supplied by the owner.
std::string format_agent_model_assistant(const ModelCompletion&,const std::string& model,std::int64_t elapsed_ms,std::optional<std::int64_t> first_token_ms,const std::string& provider_context_json={});
}
