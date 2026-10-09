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
std::vector<std::string> parts(const std::string& path){
    std::vector<std::string> output;std::size_t start=0;
    for(std::size_t i=0;i<=path.size();++i)if(i==path.size()||path[i]=='/'){output.push_back(path.substr(start,i-start));start=i+1;}
    return output;
}
}
PathGlob::PathGlob(const std::string& pattern){
    if(pattern.empty()||pattern.size()>1024||pattern.starts_with('/')||pattern.find_first_of("\\:")!=std::string::npos)throw std::invalid_argument("Use a relative glob pattern of 1-1024 UTF-8 bytes with slash separators");
    scalars(pattern);
    std::vector<std::string> expanded;
    std::function<void(const std::string&,unsigned)> expand=[&](const std::string& input,unsigned depth){
        if(depth>4)throw std::invalid_argument("Glob brace expansion exceeds four layers");
        std::size_t open=std::string::npos;bool in_class=false;
        for(std::size_t i=0;i<input.size();++i){if(input[i]=='[')in_class=true;else if(input[i]==']')in_class=false;else if(!in_class&&input[i]=='{'){open=i;break;}else if(!in_class&&input[i]=='}')throw std::invalid_argument("Unmatched glob brace");}
        if(open==std::string::npos){if(expanded.size()==64)throw std::invalid_argument("Glob has more than 64 alternatives");expanded.push_back(input);return;}
        std::vector<std::string> choices;std::size_t start=open+1,close=std::string::npos;unsigned nesting=0;in_class=false;
        for(std::size_t i=start;i<input.size();++i){const auto byte=input[i];if(byte=='[')in_class=true;else if(byte==']')in_class=false;if(in_class)continue;
            if(byte=='{')++nesting;
            else if(byte=='}'){if(nesting){--nesting;continue;}choices.push_back(input.substr(start,i-start));close=i;break;}
            else if(byte==','&&!nesting){choices.push_back(input.substr(start,i-start));start=i+1;}
        }
        if(close==std::string::npos||choices.size()<2)throw std::invalid_argument("Glob braces require comma-separated alternatives");
        for(const auto& choice:choices){if(choice.empty())throw std::invalid_argument("Glob alternatives cannot be empty");expand(input.substr(0,open)+choice+input.substr(close+1),depth+1);}
    };
    expand(pattern,0);
    for(const auto& alternative:expanded){
        const auto names=parts(alternative);if(names.size()>32)throw std::invalid_argument("Glob path exceeds 32 components");std::vector<Component> compiled;
        for(const auto& name:names){
            if(name.empty()||name=="."||name=="..")throw std::invalid_argument("Glob path contains an empty or traversal component");
            Component component;
            if(name=="**"){component.recursive=true;compiled.push_back(std::move(component));continue;}
            if(name.find("**")!=std::string::npos)throw std::invalid_argument("Recursive glob stars must occupy a whole component");
            const auto characters=scalars(name);
            for(std::size_t i=0;i<characters.size();++i){Token token;const auto character=characters[i];
                if(character=='*')token.kind=Token::star;
                else if(character=='?')token.kind=Token::any;
                else if(character=='['){
                    token.kind=Token::character_class;const auto begin=++i;if(i<characters.size()&&(characters[i]=='!'||characters[i]=='^')){token.negated=true;++i;}
                    while(i<characters.size()&&characters[i]!=']'){
                        auto low=characters[i++],high=low;
                        if(i+1<characters.size()&&characters[i]=='-'&&characters[i+1]!=']'){++i;high=characters[i++];if(high<low)throw std::invalid_argument("Glob character range is reversed");}
                        token.ranges.push_back({low,high});if(token.ranges.size()>32)throw std::invalid_argument("Glob character class exceeds limits");
                    }
                    if(i>=characters.size()||i==begin||token.ranges.empty())throw std::invalid_argument("Invalid glob character class");
                }else{if(character==']'||character=='{'||character=='}'||character<32)throw std::invalid_argument("Invalid literal glob character");token.value=character;}
                component.tokens.push_back(std::move(token));if(component.tokens.size()>256)throw std::invalid_argument("Glob component exceeds 256 tokens");
            }
            compiled.push_back(std::move(component));
        }
        // Like the reference's basename glob, a pattern without a slash can
        // match a filename at any depth within the admitted search directory.
        if(pattern.find('/')==std::string::npos&&!compiled.front().recursive){Component recursive;recursive.recursive=true;compiled.insert(compiled.begin(),std::move(recursive));}
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
        for(const auto& component:pattern){
            if(cancel.stop_requested())throw ToolCancelled("Workspace glob cancelled");std::fill(after.begin(),after.end(),false);
            if(component.recursive){after[0]=before[0];for(std::size_t i=1;i<=names.size();++i){tick();after[i]=before[i]||after[i-1];}}
            else for(std::size_t i=1;i<=names.size();++i){tick();if(before[i-1])after[i]=component_match(component,characters[i-1]);}
            before.swap(after);
        }
        if(before.back())return true;
    }
    return false;
}
}
