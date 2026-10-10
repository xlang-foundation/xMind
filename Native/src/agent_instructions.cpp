#include "agentflow/agent_instructions.hpp"
#include "agentflow/authoring_document.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
namespace {
using Json=nlohmann::json;constexpr std::int64_t revision_limit=9007199254740991;
Json parse(const std::string& source){
    if(source.size()>256*1024)throw std::invalid_argument("Instruction configuration exceeds limits");
    const auto first=source.find_first_not_of(" \t\r\n");if(first==std::string::npos)return Json{{"instructions",source}};
    try{
        if(source[first]=='{'||source[first]=='[')return Json::parse(mcp_compact_object(source));
        std::size_t position=first;std::string_view line;bool yaml=false;
        while(position<source.size()){
            const auto end=source.find_first_of("\r\n",position);line=std::string_view(source).substr(position,end==std::string::npos?source.size()-position:end-position);
            const auto begin=line.find_first_not_of(" \t");if(begin!=std::string_view::npos&&!line.substr(begin).starts_with('#')){line.remove_prefix(begin);yaml=line.starts_with("instructions:")||line.starts_with("version:");break;}
            if(end==std::string::npos)break;position=end+1;if(source[end]=='\r'&&position<source.size()&&source[position]=='\n')++position;
        }
        if(yaml)return Json::parse(authoring_yaml_to_json(source));
        return Json{{"instructions",source}};
    }
    catch(const McpProtocolError&){throw std::invalid_argument("Invalid instruction configuration JSON");}
    catch(const Json::exception&){throw std::invalid_argument("Invalid instruction configuration JSON");}
}
void fields(const Json& value,bool stored){
    if(!value.is_object())throw std::invalid_argument("Instruction configuration must be an object");
    for(auto it=value.begin();it!=value.end();++it)if(it.key()!="instructions" && (!stored || (it.key()!="version" && it.key()!="revision")))throw std::invalid_argument("Unknown or backend-owned instruction configuration field");
}
std::string instructions(const Json& value){
    if(!value.contains("instructions") || !value["instructions"].is_string())throw std::invalid_argument("Instruction text is required");
    auto text=value["instructions"].get<std::string>();if(text.size()>32768 || text.find('\0')!=std::string::npos)throw std::invalid_argument("Instruction text exceeds limits or contains NUL");return text;
}
}
AgentInstructionPolicy AgentInstructionStore::load(){
    std::string source;try{source=store_.information("native-agent","instructions").get();}catch(const NotFound&){return {};}
    const auto saved=parse(source);fields(saved,true);
    if(!saved.contains("version") || !saved["version"].is_number_integer() || saved["version"]!=1 || !saved.contains("revision") || !saved["revision"].is_number_integer() || saved["revision"]<1 || saved["revision"]>revision_limit)throw std::invalid_argument("Invalid stored instruction configuration version or revision");
    return {instructions(saved),saved["revision"].get<std::int64_t>()};
}
AgentInstructionPolicy AgentInstructionStore::apply(const std::string& source){
    const auto desired=parse(source);fields(desired,false);const auto text=instructions(desired);const auto previous=load();
    if(previous.revision>0 && previous.instructions==text)return previous;
    if(previous.revision==revision_limit)throw std::overflow_error("Instruction revision exhausted");
    AgentInstructionPolicy result{text,previous.revision+1};
    store_.put_information("native-agent","instructions",Json{{"version",1},{"revision",result.revision},{"instructions",result.instructions}}.dump()).get();return result;
}
}
