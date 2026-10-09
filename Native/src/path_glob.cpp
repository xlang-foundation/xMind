#include "agentflow/path_glob.hpp"
#include "agentflow/workspace_tools.hpp"
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace agentflow {
namespace {
std::vector<std::uint32_t> scalars(const std::string& input){
    std::vector<std::uint32_t> output;
    for(std::size_t i=0;i<input.size();){
        const auto first=static_cast<unsigned char>(input[i++]);std::uint32_t code=first,minimum=0;unsigned extra=0;
        if(!first)throw std::invalid_argument("Glob text cannot contain NUL");
        if(first<0x80){}
        else if(first>=0xc2&&first<=0xdf){code=first&0x1f;minimum=0x80;extra=1;}
        else if(first>=0xe0&&first<=0xef){code=first&0x0f;minimum=0x800;extra=2;}
        else if(first>=0xf0&&first<=0xf4){code=first&0x07;minimum=0x10000;extra=3;}
        else throw std::invalid_argument("Glob text must be UTF-8");
        while(extra){--extra;if(i==input.size())throw std::invalid_argument("Glob text must be UTF-8");const auto byte=static_cast<unsigned char>(input[i++]);if((byte&0xc0)!=0x80)throw std::invalid_argument("Glob text must be UTF-8");code=(code<<6)|(byte&0x3f);}
        if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))throw std::invalid_argument("Glob text must be UTF-8");output.push_back(code);
    }
    return output;
}
std::vector<std::string> parts(const std::string& source,bool pattern=false){
    std::string path;path.reserve(source.size());
    for(std::size_t i=0;i<source.size();++i){
        if(pattern&&source[i]=='\\'&&i+1<source.size()){
            if(source[i+1]=='/'){path+='/';++i;continue;}
            path+=source[i];path+=source[++i];continue;
        }
        path+=source[i];
    }
    std::vector<std::string> output;std::size_t start=0;
    for(std::size_t i=0;i<=path.size();++i)if(i==path.size()||path[i]=='/'){output.push_back(path.substr(start,i-start));start=i+1;}
    return output;
}
}
PathGlob::PathGlob(const std::string& pattern,bool anchored,bool ignore_syntax):recursive_tail_requires_child_(ignore_syntax){
    if(pattern.empty()||pattern.size()>1024||pattern.starts_with('/')||(!ignore_syntax&&pattern.find(':')!=std::string::npos))throw std::invalid_argument("Use a relative glob pattern of 1-1024 UTF-8 bytes with slash separators");
    scalars(pattern);
    std::vector<std::string> expanded;
    std::function<void(const std::string&,unsigned)> expand=[&](const std::string& input,unsigned depth){
        if(depth>4)throw std::invalid_argument("Glob brace expansion exceeds four layers");
        std::size_t open=std::string::npos;bool in_class=false;
        for(std::size_t i=0;i<input.size();++i){if(input[i]=='\\'){if(++i==input.size())throw std::invalid_argument("Dangling glob escape");continue;}if(input[i]=='[')in_class=true;else if(input[i]==']')in_class=false;else if(!in_class&&input[i]=='{'){open=i;break;}else if(!in_class&&input[i]=='}')throw std::invalid_argument("Unmatched glob brace");}
        if(open==std::string::npos){if(expanded.size()==64)throw std::invalid_argument("Glob has more than 64 alternatives");expanded.push_back(input);return;}
        std::vector<std::string> choices;std::size_t start=open+1,close=std::string::npos;unsigned nesting=0;in_class=false;
        for(std::size_t i=start;i<input.size();++i){const auto byte=input[i];if(byte=='\\'){if(++i==input.size())throw std::invalid_argument("Dangling glob escape");continue;}if(byte=='[')in_class=true;else if(byte==']')in_class=false;if(in_class)continue;
            if(byte=='{')++nesting;
            else if(byte=='}'){if(nesting){--nesting;continue;}choices.push_back(input.substr(start,i-start));close=i;break;}
            else if(byte==','&&!nesting){choices.push_back(input.substr(start,i-start));start=i+1;}
        }
        if(close==std::string::npos||choices.size()<2)throw std::invalid_argument("Glob braces require comma-separated alternatives");
        for(const auto& choice:choices){if(choice.empty())throw std::invalid_argument("Glob alternatives cannot be empty");expand(input.substr(0,open)+choice+input.substr(close+1),depth+1);}
    };
    if(ignore_syntax)expanded.push_back(pattern);else expand(pattern,0);
    for(const auto& alternative:expanded){
        const auto names=parts(alternative,true);if(names.size()>32)throw std::invalid_argument("Glob path exceeds 32 components");std::vector<Component> compiled;
        for(const auto& name:names){
            if(name.empty()||name=="."||name=="..")throw std::invalid_argument("Glob path contains an empty or traversal component");
            Component component;
            if(name=="**"){component.recursive=true;compiled.push_back(std::move(component));continue;}
            if(!ignore_syntax&&name.find("**")!=std::string::npos)throw std::invalid_argument("Recursive glob stars must occupy a whole component");
            const auto characters=scalars(name);
            for(std::size_t i=0;i<characters.size();++i){Token token;const auto character=characters[i];
                if(character=='\\'){if(++i==characters.size())throw std::invalid_argument("Dangling glob escape");token.value=characters[i];}
                else if(character=='*')token.kind=Token::star;
                else if(character=='?')token.kind=Token::any;
                else if(character=='['){
                    token.kind=Token::character_class;const auto begin=++i;if(i<characters.size()&&(characters[i]=='!'||characters[i]=='^')){token.negated=true;++i;}
                    if(i<characters.size()&&characters[i]==']'&&ignore_syntax){token.ranges.push_back({']',']'});++i;}
                    while(i<characters.size()&&characters[i]!=']'){
                        auto low=characters[i++],high=low;const bool escaped=low=='\\';
                        if(escaped){if(i==characters.size())throw std::invalid_argument("Dangling glob class escape");low=high=characters[i++];}
                        if(!escaped&&low=='['&&i<characters.size()&&characters[i]==':'){
                            ++i;std::string cls;while(i<characters.size()&&characters[i]!=':'){if(characters[i]>127)throw std::invalid_argument("Invalid named glob class");cls+=static_cast<char>(characters[i++]);}
                            if(i+1>=characters.size()||characters[i+1]!=']')throw std::invalid_argument("Invalid named glob class");i+=2;
                            const auto add=[&](std::uint32_t a,std::uint32_t b){token.ranges.push_back({a,b});};
                            if(cls=="alnum"||cls=="alpha"){if(cls=="alnum")add('0','9');add('A','Z');add('a','z');}
                            else if(cls=="ascii")add(0,127);else if(cls=="blank"){add(9,9);add(32,32);}else if(cls=="cntrl"){add(0,31);add(127,127);}
                            else if(cls=="digit")add('0','9');else if(cls=="graph")add(33,126);else if(cls=="lower")add('a','z');else if(cls=="print")add(32,126);
                            else if(cls=="punct"){add(33,47);add(58,64);add(91,96);add(123,126);}else if(cls=="space"){add(9,13);add(32,32);}else if(cls=="upper")add('A','Z');
                            else if(cls=="xdigit"){add('0','9');add('A','F');add('a','f');}else throw std::invalid_argument("Unknown named glob class");
                            if(token.ranges.size()>32)throw std::invalid_argument("Glob character class exceeds limits");continue;
                        }
                        if(i+1<characters.size()&&characters[i]=='-'&&characters[i+1]!=']'){++i;high=characters[i++];if(high=='\\'){if(i==characters.size())throw std::invalid_argument("Dangling glob range escape");high=characters[i++];}if(high<low)throw std::invalid_argument("Glob character range is reversed");}
                        token.ranges.push_back({low,high});if(token.ranges.size()>32)throw std::invalid_argument("Glob character class exceeds limits");
                    }
                    if(i>=characters.size()||i==begin||token.ranges.empty())throw std::invalid_argument("Invalid glob character class");
                }else{if((!ignore_syntax&&(character==']'||character=='{'||character=='}'))||character<32)throw std::invalid_argument("Invalid literal glob character");token.value=character;}
                if(token.kind==Token::star&&!component.tokens.empty()&&component.tokens.back().kind==Token::star)continue;
                component.tokens.push_back(std::move(token));if(component.tokens.size()>256)throw std::invalid_argument("Glob component exceeds 256 tokens");
            }
            compiled.push_back(std::move(component));
        }
        // Like the reference's basename glob, a pattern without a slash can
        // match a filename at any depth within the admitted search directory.
        if(!anchored&&pattern.find('/')==std::string::npos&&!compiled.front().recursive){Component recursive;recursive.recursive=true;compiled.insert(compiled.begin(),std::move(recursive));}
        patterns_.push_back(std::move(compiled));
    }
}
bool PathGlob::matches(const std::string& path,std::size_t& steps,std::stop_token cancel) const {
    const auto names=parts(path);std::vector<std::vector<std::uint32_t>> characters;for(const auto& name:names)characters.push_back(scalars(name));
    const auto tick=[&]{if(++steps>50000000)throw GlobMatchBudgetExceeded{};};
    const auto component_match=[&](const Component& component,const std::vector<std::uint32_t>& name){
        std::vector<bool> before(name.size()+1),after(name.size()+1);before[0]=true;
        for(const auto& token:component.tokens){
            if(cancel.stop_requested())throw ToolCancelled("Workspace glob cancelled");std::fill(after.begin(),after.end(),false);
            if(token.kind==Token::star){after[0]=before[0];for(std::size_t i=1;i<=name.size();++i){tick();after[i]=before[i]||after[i-1];}}
            else for(std::size_t i=1;i<=name.size();++i){tick();bool accepted=token.kind==Token::any||(token.kind==Token::literal&&token.value==name[i-1]);
                if(token.kind==Token::character_class){bool included=false;for(const auto& range:token.ranges){tick();included|=name[i-1]>=range.first&&name[i-1]<=range.second;}accepted=included!=token.negated;}
                after[i]=before[i-1]&&accepted;
            }
            before.swap(after);
        }
        return static_cast<bool>(before.back());
    };
    for(const auto& pattern:patterns_){
        std::vector<bool> before(names.size()+1),after(names.size()+1);before[0]=true;
        for(std::size_t index=0;index<pattern.size();++index){const auto& component=pattern[index];
            if(cancel.stop_requested())throw ToolCancelled("Workspace glob cancelled");std::fill(after.begin(),after.end(),false);
            if(component.recursive){const bool require_child=recursive_tail_requires_child_&&index+1==pattern.size();after[0]=!require_child&&before[0];for(std::size_t i=1;i<=names.size();++i){tick();after[i]=(require_child?before[i-1]:before[i])||after[i-1];}}
            else for(std::size_t i=1;i<=names.size();++i){tick();if(before[i-1])after[i]=component_match(component,characters[i-1]);}
            before.swap(after);
        }
        if(before.back())return true;
    }
    return false;
}
}
