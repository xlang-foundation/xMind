#pragma once
#include "agentflow/workspace_tools.hpp"
#include "agentflow/instruction_precondition.hpp"
#include "agentflow/skill_records.hpp"
#include <map>
#include <set>
#include <utility>
namespace agentflow {
struct LocalSkill {
    std::string id;
    std::optional<std::string> description;
    std::string body;
    bool model_invocable=true;
    WorkspaceSnapshot source;
    // Automatic suggestion policy is independent of explicit loading.
    std::optional<bool> autoinvoke;
    std::string name;
};
// Native per-run guidance, never an executable or an effect permission. Only
// the already-authorized workspace's .agents/skills directory is discovered.
class SkillContext {
public:
    explicit SkillContext(WorkspaceTools& workspace):workspace_(workspace){}
    static std::vector<ModelToolDefinition> definitions();
    static LocalSkill parse(WorkspaceSnapshot source,const std::string& skill_id);
    std::string prepare(std::stop_token cancel={});
    void restore(const SkillSelections& selections,std::stop_token cancel={});
    SkillSelections selections() const;
    std::string catalogue_json(std::stop_token cancel={}) const;
    std::string activate(const std::string& arguments,std::stop_token cancel={},std::size_t instruction_budget=49152);
    std::string activation_directory(const std::string& arguments,std::stop_token cancel={}) const;
    bool ready(std::stop_token cancel={}) const;
    InstructionPrecondition precondition() const;
    std::string metadata() const;
private:
    static std::pair<std::string,std::map<std::string,LocalSkill>> render(
        const std::map<std::string,LocalSkill>& catalogue,const std::set<std::string>& selected,const std::set<std::string>& manual={});
    std::map<std::string,LocalSkill> discover(std::stop_token cancel) const;
    WorkspaceTools& workspace_;
    std::set<std::string> requested_;
    std::set<std::string> manual_;
    std::map<std::string,LocalSkill> delivered_;
};
}
