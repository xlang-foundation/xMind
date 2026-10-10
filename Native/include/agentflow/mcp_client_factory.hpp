#pragma once
#include "agentflow/mcp_configuration.hpp"
#include "agentflow/mcp_tool_client.hpp"
#include "agentflow/secret_protection.hpp"
#include <memory>
namespace agentflow {
// Trusted native configuration only. Resolve exact purpose-bound credentials,
// check cancellation/deadline before connecting, and return one actual owner.
// Optional private copies support admin catalogue-reflection rejection; callers
// must never expose them as public metadata. SecretBytes wipes those copies.
std::unique_ptr<McpToolClient> connect_mcp_client(const McpServerSetting& setting,
    PersistenceService& store,McpDeadline deadline,std::stop_token cancel={},
    std::vector<SecretBytes>* private_catalogue_credentials=nullptr);
}
