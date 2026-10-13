#pragma once

#include "agentflow/mcp_tool_client.hpp"
#include "agentflow/persistence_service.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace agentflow {

std::optional<McpSubscriptionFilter> mcp_run_subscription_filter(const McpServerDescription& server);

// A run-owned independent MCP listener. Only validated, non-payload metadata
// is persisted. Call stop() before committing the owner's terminal state.
class McpRunSubscriptionObserver {
public:
    McpRunSubscriptionObserver(PersistenceService& store,std::string run,std::string server,
        std::int64_t revision,std::unique_ptr<McpToolClient> client,McpSubscriptionFilter filter,
        McpToolClient::Deadline deadline,std::shared_ptr<std::atomic<bool>> catalogue_invalidated={});
    ~McpRunSubscriptionObserver();
    McpRunSubscriptionObserver(const McpRunSubscriptionObserver&)=delete;
    McpRunSubscriptionObserver& operator=(const McpRunSubscriptionObserver&)=delete;
    void start();
    void stop() noexcept;
private:
    void persist(const char* kind,std::string data_json);
    void observe(std::stop_token stop) noexcept;
    PersistenceService& store_;
    std::string run_,server_;
    std::int64_t revision_;
    std::unique_ptr<McpToolClient> client_;
    McpSubscriptionFilter filter_;
    McpToolClient::Deadline deadline_;
    std::shared_ptr<std::atomic<bool>> catalogue_invalidated_;
    std::jthread worker_;
};

} // namespace agentflow
