#include "agentflow/search_pattern.hpp"
#include "re2/re2.h"
#include <stdexcept>

namespace agentflow {
struct SearchPattern::Impl {
    RE2 program;
    static RE2::Options options(bool regex,bool case_sensitive){
        RE2::Options value;value.set_encoding(RE2::Options::EncodingUTF8);
        value.set_literal(!regex);value.set_case_sensitive(case_sensitive);
        value.set_never_capture(true);value.set_log_errors(false);value.set_max_mem(4*1024*1024);
        return value;
    }
    Impl(const std::string& query,bool regex,bool case_sensitive):program(query,options(regex,case_sensitive)){
        if(!program.ok())throw std::invalid_argument("Search pattern is invalid, unsupported or exceeds regex memory limits");
    }
};
SearchPattern::SearchPattern(const std::string& query,bool regex,bool case_sensitive):impl_(std::make_unique<Impl>(query,regex,case_sensitive)){}
SearchPattern::~SearchPattern()=default;
bool SearchPattern::matches(std::string_view text)const{return RE2::PartialMatch(text,impl_->program);}
}
