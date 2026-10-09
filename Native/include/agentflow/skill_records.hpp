#pragma once
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace agentflow {
// Native model-activated selection identities, not cached bodies or permissions.
struct SkillSelections {std::string workspace_id;std::vector<std::string> ids;};
inline void validate_skill_selections(const SkillSelections& value){
    if(value.workspace_id.empty()||value.workspace_id.size()>1024||value.workspace_id.find('\0')!=std::string::npos||value.ids.size()>8)throw std::invalid_argument("Invalid native skill selection scope");
    std::set<std::string> unique;
    for(const auto& id:value.ids)if(id.empty()||id.size()>256||id=="."||id==".."||id.find_first_of("/\\:")!=std::string::npos||id.find('\0')!=std::string::npos||!unique.insert(id).second)throw std::invalid_argument("Invalid native skill selection identity");
}
}
