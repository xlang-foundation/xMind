#pragma once
#include "agentflow/secret_protection.hpp"
#include <cstdint>
#include <optional>
#include <string>
namespace agentflow {
struct LocalProfileOptions {
    std::string workspace, profile_root, provider_config, graphs_config;
    std::optional<bool> approved_edits;
};
struct LocalProfileConnection {
    std::string workspace, directory;
    std::uint16_t port = 0;
    std::uint32_t process_id = 0;
    bool started = false;
    SecretBytes auth;
};
// Native machine-local process rendezvous. Agent state and all SQLite I/O remain
// in the backend. A failed/unknown owner never authorizes a new empty profile.
LocalProfileConnection connect_local_profile(const LocalProfileOptions &options);
void publish_local_profile_ready(const std::string &state_file, int port, const std::string &token,
                                 const std::string &workspace, const std::string &workspace_id,
                                 const std::string &authority);
// Publish only public adapter metadata outside the selected workspace.
void publish_local_view_ready(const std::string &file, const std::string &metadata,
                              const std::string &workspace);
} // namespace agentflow
