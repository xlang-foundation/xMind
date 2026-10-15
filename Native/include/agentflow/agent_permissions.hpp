#pragma once
#include "agentflow/agent_definition_records.hpp"
#include <string_view>

namespace agentflow {
namespace detail {
inline bool agent_permission_glob(std::string_view pattern, std::string_view value) {
    if (value.size() > 8192) return pattern == "*" || pattern == "**";
    const auto width=value.size()+1;
    std::vector<std::int8_t> memo((pattern.size()+1)*width,-1);
    const auto match=[&](auto&& self,std::size_t p,std::size_t v)->bool {
        auto& cached=memo[p*width+v];if(cached!=-1)return cached!=0;
        bool result=false;
        if(p==pattern.size())result=v==value.size();
        else if(pattern[p]=='*'){
            const bool doublestar=p+1<pattern.size()&&pattern[p+1]=='*';
            const auto after=p+(doublestar?2:1);
            if(doublestar&&after<pattern.size()&&pattern[after]=='/')
                result=self(self,after+1,v);
            if(!result&&v<value.size()&&(doublestar||value[v]!='/'))result=self(self,p,v+1);
            if(!result&&doublestar&&v<value.size()&&value[v]=='/')result=self(self,p,v+1);
            if(!result)result=self(self,after,v);
        }else if(v<value.size()&&(pattern[p]=='?'?value[v]!='/':pattern[p]==value[v]))result=self(self,p+1,v+1);
        cached=static_cast<std::int8_t>(result?1:0);return result;
    };
    return match(match,0,0);
}

inline std::string agent_permission_normalize_resource(std::string_view input) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= input.size()) {
        const auto end = input.find('/', start);
        const auto part = input.substr(start, end == std::string_view::npos ? input.size() - start : end - start);
        if (part.empty() || part == ".") {}
        else if (part == "..") { if (!parts.empty()) parts.pop_back(); else parts.emplace_back(".."); }
        else parts.emplace_back(part);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    std::string result;
    for (const auto& part : parts) { if (!result.empty()) result.push_back('/'); result += part; }
    return result.empty() ? "." : result;
}

inline bool agent_permission_may_cover_directory(const AgentPermissionRule& rule,
                                                  const std::string& action,
                                                  const std::string& directory) {
    if (rule.action != "*" && rule.action != action) return false;
    const auto wildcard = rule.resource.find_first_of("*?");
    auto prefix = rule.resource.substr(0, wildcard);
    while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
    if (prefix.empty()) return true;
    const auto normalized_directory = agent_permission_normalize_resource(directory);
    if (normalized_directory == ".") return true;
    return prefix == normalized_directory || prefix.starts_with(normalized_directory + "/") ||
           normalized_directory.starts_with(prefix + "/") ||
           agent_permission_glob(rule.resource, normalized_directory);
}
}

enum class AgentPermissionEffect { no_match, allow, deny };

inline AgentPermissionEffect agent_permission_effect(const std::vector<AgentPermissionRule>& rules,
                                                       const std::string& action, const std::string& resource) {
    const auto normalized_resource=detail::agent_permission_normalize_resource(resource);
    for (auto it=rules.rbegin();it!=rules.rend();++it) {
        const auto& rule=*it;
        if ((rule.action == "*" || rule.action == action) &&
            detail::agent_permission_glob(rule.resource, normalized_resource))
            return rule.effect=="allow"?AgentPermissionEffect::allow:AgentPermissionEffect::deny;
    }
    return AgentPermissionEffect::no_match;
}

inline bool agent_permission_denies(const std::vector<AgentPermissionRule>& rules,
                                    const std::string& action, const std::string& resource) {
    return agent_permission_effect(rules,action,resource)==AgentPermissionEffect::deny;
}

inline bool agent_permission_denies_directory_enumeration(const std::vector<AgentPermissionRule>& rules,
                                                           const std::string& action,
                                                           const std::string& directory) {
    for (const auto& rule : rules)
        if (rule.effect == "deny" && detail::agent_permission_may_cover_directory(rule, action, directory))
            return true;
    return false;
}
}
