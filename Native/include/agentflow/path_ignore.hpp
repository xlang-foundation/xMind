#pragma once
#include "agentflow/path_glob.hpp"
#include <string>
#include <vector>

namespace agentflow {
struct IgnoreMetadataBudgetExceeded {};
// Ordered workspace-local ignore snapshots. Higher source priority wins before
// directory depth: rgignore > ignore > gitignore/info-exclude.
class PathIgnore {
public:
    struct Checkpoint {std::size_t rules;std::string repository;};
    Checkpoint checkpoint() const {return {rules_.size(),repository_};}
    void restore(const Checkpoint& checkpoint) {rules_.erase(rules_.begin()+checkpoint.rules,rules_.end());repository_=checkpoint.repository;}
    void begin_repository(const std::string& directory) {repository_=directory;}
    bool in_repository() const {return !repository_.empty();}
    void add(const std::string& directory,int priority,const std::string& content);
    bool ignored(const std::string& path,bool directory,std::size_t& steps,std::stop_token cancel={}) const;
    std::size_t source_files() const {return files_;}
private:
    struct Rule {
        std::string scope;
        int priority=0;
        bool directory_only=false,excluded=true;
        PathGlob matcher;
        Rule(std::string directory,int level,bool directories,bool exclude,PathGlob compiled)
            :scope(std::move(directory)),priority(level),directory_only(directories),excluded(exclude),matcher(std::move(compiled)){}
    };
    std::vector<Rule> rules_;
    std::size_t bytes_=0,files_=0,total_rules_=0;
    std::string repository_;
};
}
