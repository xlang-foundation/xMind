#pragma once

#include "agentflow/mcp_configuration.hpp"
#include <string>
#include <vector>

namespace agentflow {
class McpResourceCatalog {
public:
    virtual ~McpResourceCatalog() = default;
    virtual std::string list_json() = 0;
};

class NativeMcpResourceCatalog final : public McpResourceCatalog {
public:
    NativeMcpResourceCatalog(PersistenceService& persistence, std::vector<McpServerSetting> servers);
    std::string list_json() override;
private:
    PersistenceService& persistence_;
    std::vector<McpServerSetting> servers_;
};
}
