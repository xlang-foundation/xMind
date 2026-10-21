#pragma once
#include "agentflow/agent_callable_tool_host.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include "agentflow/delegation_records.hpp"
#include "agentflow/workspace_tools.hpp"
#include "agentflow/agent_file_tool_host.hpp"
#include "agentflow/agent_process_tool_host.hpp"
#include "agentflow/agent_remote_tool_host.hpp"
#include <variant>
#include <stdexcept>

namespace agentflow {
struct AgentWorkerAuthorityCancelled:std::runtime_error {using std::runtime_error::runtime_error;};
struct AgentWorkerAuthorityExpired:std::runtime_error {using std::runtime_error::runtime_error;};
// Backend-observed dispatch failure only; no child RPC accepts this value.
// Unknown retains the existing uncertainty receipt without inventing a cause.
enum class AgentWorkerModelFailure {unknown,cancelled,deadline,provider_failure,dispatch_error,context_overflow};
// Backend-created immutable process/admission metadata. No session channel key,
// provider credential, executable path or worker-selected authority is stored.
struct AgentWorkerLeaseSpec {
    std::string generation,root_run_id,owner_run_id,workspace_id;
    std::int64_t workspace_revision=0;
    std::uint32_t pid=0;
    std::string creation_time,executable_identity,provider_identity_json;
    bool operator==(const AgentWorkerLeaseSpec&)const=default;
};
struct AgentWorkerLeaseRecord {
    AgentWorkerLeaseSpec spec;
    std::string state,retirement_reason;
    std::int64_t revision=1;
};
struct AgentWorkerModelClaimSpec {
    std::string generation,root_run_id,owner_run_id,attempt_id,request_binding;
};
struct AgentWorkerModelClaimRecord {
    AgentWorkerModelClaimSpec spec;
    std::string state,actual_response_json,response_binding;
    std::string actual_assistant_json,assistant_binding;
};
struct AgentWorkerReadClaimRecord {
    AgentWorkerModelClaimSpec model;
    AgentWorkspaceInvocation invocation;
    std::string state;
    bool dispatched=false,success=false;
    std::string actual_output_json,output_binding;
};
struct AgentWorkerToolTurnRecord {
    AgentWorkerModelClaimSpec model;
    std::vector<std::int64_t> message_sequences;
    std::string messages_binding;
};
// Backend-only ownership of one exact provider file call. Invocation entry is
// distinct from each approved native mutation; operation IDs are backend facts.
struct AgentWorkerFileClaimRecord {
    AgentWorkerModelClaimSpec model;
    AgentFileToolInvocation invocation;
    std::string state;
    std::vector<std::string> operation_ids;
    bool success=false;
    std::string actual_output_json,output_binding;
};
// Backend-owned foreground run_process claim; observation/control admission is separate.
struct AgentWorkerProcessClaimRecord {
    AgentWorkerModelClaimSpec model;
    AgentProcessToolInvocation invocation;
    std::string state;
    std::vector<std::string> operation_ids;
    bool success=false;
    std::string actual_output_json,output_binding;
};
// Backend-owned exact remote invocation and its single native dispatch receipt.
struct AgentWorkerRemoteClaimRecord {
    AgentWorkerModelClaimSpec model;
    AgentRemoteToolInvocation invocation;
    std::string state;
    std::vector<std::string> operation_ids;
    bool success=false;
    std::string actual_output_json,output_binding;
};
// Backend-owned trusted callable and its single native operation receipt.
struct AgentWorkerCallableClaimRecord {
    AgentWorkerModelClaimSpec model;
    AgentCallableInvocation invocation;
    std::string state;
    std::vector<std::string> operation_ids;
    bool success=false;
    std::string actual_output_json,output_binding;
};
// An explicit read vocabulary, never arbitrary repository reflection. The
// backend validates the exact captured lease in the same transaction as a read.
enum class AgentWorkerJournalReadKind {run,session,history,events,root_budget,child_admission};
struct AgentWorkerJournalRead {
    AgentWorkerJournalReadKind kind;
    std::string target;
    std::int64_t after=0;
};
using AgentWorkerJournalValue=std::variant<Run,Session,std::vector<Message>,std::vector<Event>,RootBudgetRecord,ChildAdmissionRecord>;
}
