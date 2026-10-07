#pragma once
#include "agentflow/persistence_service.hpp"
#include <chrono>
#include <mutex>
#include <string_view>

namespace agentflow {
struct ViewSession {std::string credential;std::int64_t expires_unix_ms;};
// Local access sessions only. Team identity and workspace authorization remain
// separate. Call issue only after authenticating the server's master token.
class ViewSessions {
public:
    ViewSessions(PersistenceService& store,std::string_view authority,
        std::chrono::seconds lifetime=std::chrono::hours(8));
    ViewSession issue(const std::string& origin);
    bool accepts(std::string_view credential,const std::string& origin);
    void revoke(std::string_view credential,const std::string& origin);
private:
    PersistenceService& store_;
    std::string binding_;
    std::chrono::seconds lifetime_;
    std::mutex mutex_;
};
}
