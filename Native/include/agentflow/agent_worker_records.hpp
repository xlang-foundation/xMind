#pragma once
#include <cstdint>
#include <string>

namespace agentflow {
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
};
}
