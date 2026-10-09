#pragma once
#include <cstdint>
#include <stop_token>
#include <string>
#include <vector>

namespace agentflow {
struct GlobMatchBudgetExceeded {};
// Compiled, case-sensitive path glob. No regex engine or recursive backtracking.
class PathGlob {
public:
    explicit PathGlob(const std::string& pattern);
    bool matches(const std::string& path,std::size_t& steps,std::stop_token cancel={}) const;
private:
    struct Token {
        enum Kind {literal,star,any,character_class} kind=literal;
        std::uint32_t value=0;
        bool negated=false;
        std::vector<std::pair<std::uint32_t,std::uint32_t>> ranges;
    };
    struct Component {bool recursive=false;std::vector<Token> tokens;};
    std::vector<std::vector<Component>> patterns_;
};
}
