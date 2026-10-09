#include "agentflow/path_ignore.hpp"
#include "agentflow/workspace_tools.hpp"

namespace agentflow {
void PathIgnore::add(const std::string& directory,int priority,const std::string& content){
    if(content.size()>32768)throw ToolFileError("Ignore metadata exceeds the per-file limit");
    if(content.size()>256*1024-bytes_)throw IgnoreMetadataBudgetExceeded{};
    bytes_+=content.size();++files_;std::size_t start=content.starts_with("\xef\xbb\xbf")?3:0;
    while(start<content.size()){
        auto end=content.find('\n',start);if(end==std::string::npos)end=content.size();auto pattern=content.substr(start,end-start);start=end+1;
        if(!pattern.empty()&&pattern.back()=='\r')pattern.pop_back();
        while(!pattern.empty()&&pattern.back()==' '){std::size_t escapes=0;for(auto i=pattern.size()-1;i&&pattern[i-1]=='\\';--i)++escapes;if(escapes%2)break;pattern.pop_back();}
        if(pattern.empty()||pattern.front()=='#')continue;
        bool excluded=true;if(pattern.front()=='!'){excluded=false;pattern.erase(0,1);}
        if(pattern.empty())continue;
        const bool directory_only=pattern.back()=='/';if(directory_only)pattern.pop_back();
        const bool anchored=!pattern.empty()&&pattern.front()=='/';if(anchored)pattern.erase(0,1);
        if(pattern.empty())continue;
        if(pattern.size()>1024)throw ToolFileError("Ignore pattern exceeds the configured limit");
        if(++total_rules_>4096)throw IgnoreMetadataBudgetExceeded{};
        try{rules_.emplace_back(directory,priority,directory_only,excluded,PathGlob(pattern,anchored,true));}
        catch(const std::invalid_argument& error){
            // Git-style malformed wildcards do not match. Capability exhaustion
            // is a coverage failure, never permission to silently drop a rule.
            if(std::string_view(error.what()).find("exceeds")!=std::string_view::npos)throw ToolFileError("Ignore pattern exceeds matching capability");
        }
    }
}
bool PathIgnore::ignored(const std::string& path,bool directory,std::size_t& steps,std::stop_token cancel) const {
    for(int priority=2;priority>=-1;--priority)for(auto rule=rules_.rbegin();rule!=rules_.rend();++rule){
        if(cancel.stop_requested())throw ToolCancelled("Workspace ignore matching cancelled");
        if(rule->priority!=priority||(rule->directory_only&&!directory))continue;
        if(priority<=0&&(repository_.empty()||(repository_!="."&&rule->scope!=repository_&&!rule->scope.starts_with(repository_+"/"))))continue;
        std::string relative;
        if(rule->scope==".")relative=path;
        else if(path.starts_with(rule->scope+"/"))relative=path.substr(rule->scope.size()+1);
        else continue;
        if(rule->matcher.matches(relative,steps,cancel))return rule->excluded;
    }
    return false;
}
}
