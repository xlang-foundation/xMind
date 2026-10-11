#pragma once
#include "agentflow/workspace_tools.hpp"
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace agentflow {
class VcsInitializationFailure : public std::runtime_error {
public:
    VcsInitializationFailure(int http_status,std::string message)
        :std::runtime_error(std::move(message)),http_status_(http_status){}
    int http_status()const noexcept{return http_status_;}
private:
    int http_status_;
};

// Performs an explicit controller-requested VCS initialization in an already
// captured workspace. Git runs as a bounded native child process; no shell or
// model-selected executable/arguments are accepted.
void initialize_workspace_vcs(WorkspaceTools& workspace,std::string_view provider);
}
