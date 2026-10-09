#pragma once
#include <memory>
#include <string>
#include <string_view>

namespace agentflow {
// Native bounded RE2 compilation, shared by literal and regex search modes.
class SearchPattern {
public:
    SearchPattern(const std::string& query,bool regex,bool case_sensitive);
    ~SearchPattern();
    bool matches(std::string_view text) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
