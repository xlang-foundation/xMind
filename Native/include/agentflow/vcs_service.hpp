#pragma once
#include "agentflow/workspace_tools.hpp"
#include <stdexcept>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agentflow {
class VcsInitializationFailure : public std::runtime_error {
public:
    VcsInitializationFailure(int http_status,std::string message)
        :std::runtime_error(std::move(message)),http_status_(http_status){}
    int http_status()const noexcept{return http_status_;}
private:
    int http_status_;
};
struct VcsFileStatus {std::string file;std::uint64_t additions=0,deletions=0;std::string status;};
class VcsStatusFailure : public std::runtime_error {
public:
    VcsStatusFailure(int http_status,std::string message)
        :std::runtime_error(std::move(message)),http_status_(http_status){}
    int http_status()const noexcept{return http_status_;}
private:
    int http_status_;
};

// Performs an explicit controller-requested VCS initialization in an already
// captured workspace. Git runs as a bounded native child process; no shell or
// model-selected executable/arguments are accepted.
void initialize_workspace_vcs(WorkspaceTools& workspace,std::string_view provider);
std::vector<VcsFileStatus> workspace_vcs_status(WorkspaceTools& workspace);
}
