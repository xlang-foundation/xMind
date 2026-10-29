#pragma once
#include "agentflow/secret_protection.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
namespace httplib { struct Request; struct Response; }
namespace agentflow {
struct NativeRequestSession;
struct NativeRequestSessionCache {
    std::mutex mutex;
    std::shared_ptr<NativeRequestSession> session;
};
struct LocalProfileOptions {
    std::string workspace, profile_root, provider_config, graphs_config, python_tools_config, language_server_config, process_config, mcp_config;
    std::string web_assets,web_packages;
    std::optional<bool> approved_edits;
    bool require_existing_profile = false;
    bool upgrade_backend = false;
    bool defer_busy_upgrade = false;
    std::optional<std::string> agent_execution;
};
struct LocalProfileConnection {
    std::string workspace, directory;
    std::uint16_t port = 0;
    std::uint32_t process_id = 0;
    bool started = false;
    SecretBytes auth;
    bool approved_edits = true;
    std::string agent_execution="thread";
    std::string ipc_endpoint;
    std::function<void(httplib::Request&,httplib::Response&)> bound_requests;
    // Move this cache with its authenticated backend connection. Outstanding
    // replies retain their original session across connection replacement.
    std::shared_ptr<NativeRequestSessionCache> request_session=std::make_shared<NativeRequestSessionCache>();
};
struct LocalWorkspaceProfile {
    std::string workspace_id, root, name;
};
// Native machine-local process rendezvous. Agent state and all SQLite I/O remain
// in the backend. A failed/unknown owner never authorizes a new empty profile.
LocalProfileConnection connect_local_profile(const LocalProfileOptions &options);
// Authenticate a freshly spawned profile server against its owner-controlled
// DPAPI profile record before allowing the managed-copy runtime fast path.
void validate_local_profile_launch(const std::string &state_file,const std::string &workspace,
                                   const std::string &runtime,const std::string &manifest,
                                   const std::string &auth_token,const std::string &owner_receipt={},
                                   const std::string &language_server_config={},
                                   const std::string &mcp_config={},
                                   std::string *mcp_document=nullptr,const std::string &agent_execution="thread",
                                   std::string *provider_route_config=nullptr);
// Returns only validated profiles already present under the user-local profile
// root. This never opens a client-supplied path or creates profile storage.
std::vector<LocalWorkspaceProfile> list_local_workspace_profiles(const std::string &profile_root = {});
void publish_local_profile_ready(const std::string &state_file, int port, const std::string &token,
                                 const std::string &workspace, const std::string &workspace_id,
                                 const std::string &authority,const std::string &ipc_endpoint={});
// Publish only public adapter metadata outside the selected workspace.
void validate_local_view_ready(const std::string &file, const std::string &workspace);
void publish_local_view_ready(const std::string &file, const std::string &metadata,
                              const std::string &workspace);
} // namespace agentflow
