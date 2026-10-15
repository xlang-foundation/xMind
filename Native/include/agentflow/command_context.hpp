#pragma once
#include "agentflow/workspace_tools.hpp"
#include <map>

namespace agentflow {

struct LocalCommand {
    std::string id;
    std::optional<std::string> description;
    std::string prompt_template;
    WorkspaceSnapshot source;
};

// Workspace Markdown commands are prompt templates. They are never evaluated
// as shell, Python, or another executable language.
class CommandContext {
public:
    explicit CommandContext(WorkspaceTools& workspace):workspace_(workspace){}
    std::string catalogue_json(std::stop_token cancel={}) const;
    std::string expand(const std::string& id,const std::string& arguments,
        std::stop_token cancel={}) const;
private:
    std::map<std::string,LocalCommand> discover(std::stop_token cancel) const;
    WorkspaceTools& workspace_;
};

}
