#pragma once
#include "agentflow/workspace_tools.hpp"
#include "agentflow/instruction_precondition.hpp"
#include "agentflow/skill_context.hpp"
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
    SkillContext& skills(){return skills_;}
    std::string activate_skill(const std::string& arguments,std::size_t base_instruction_bytes,std::stop_token cancel={});
    static std::string file_directory(const std::string& path);
private:
    using Scopes=std::map<std::string,std::vector<WorkspaceSnapshot>>;
    static std::string render(const Scopes& scopes);
    WorkspaceTools& workspace_;
    SkillContext skills_;
    Scopes delivered_,requested_;
};
}
