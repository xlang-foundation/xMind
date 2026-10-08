#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace agentflow {
struct AgentSettings;
class PersistenceService;
struct CredentialMetadata;
// Metadata only. Credential material is never an input to the authority binder.
struct AgentAuthorityCredentialVersion {
    std::string scope,id,purpose;
    std::int64_t revision=0;
};
// Read the exact configured encrypted-reference metadata without resolving keys.
std::vector<AgentAuthorityCredentialVersion> agent_authority_credentials(
    PersistenceService& store,const AgentSettings& settings);
// Same exact metadata selector used by the persistence reader, without I/O.
std::vector<AgentAuthorityCredentialVersion> select_agent_authority_credentials(
    const AgentSettings& settings,const std::vector<CredentialMetadata>& available);
// Private backend binding for admitted execution and clean-pause adoption.
// Includes destinations, executable identities, instruction bytes and referenced
// credential versions. Never put this digest in model context or public DTOs.
// Actual discovered tool schemas are bound separately before the first model call.
// An empty selected_model means the backend's configured default model.
std::string agent_authority_identity(const AgentSettings& settings,
    const std::string& selected_model,const std::string& workspace_identity,
    const std::vector<AgentAuthorityCredentialVersion>& credentials);
}
