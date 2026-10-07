#pragma once
#include "agentflow/workspace_tools.hpp"
#include "agentflow/instruction_precondition.hpp"
#include <map>
namespace agentflow {
// Per-run model context. Discovery never grants an effect. A new/changed scope
// must reach the next provider request before a scoped tool can proceed.
class RepositoryInstructionContext {
public:
    RepositoryInstructionContext(WorkspaceTools& workspace,std::vector<WorkspaceSnapshot> root);
    std::string prepare(std::stop_token cancel={});
    std::string metadata() const;
    bool ready(const std::string& directory,std::stop_token cancel={});
    InstructionPrecondition precondition(const std::string& directory);
    static std::string file_directory(const std::string& path);
private:
    using Scopes=std::map<std::string,std::vector<WorkspaceSnapshot>>;
    static std::string render(const Scopes& scopes);
    WorkspaceTools& workspace_;
    Scopes delivered_,requested_;
};
}
