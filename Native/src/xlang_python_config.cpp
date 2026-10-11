#include "agentflow/xlang_python_config.hpp"
#include "agentflow/authoring_document.hpp"
#include "agentflow/agent_definition_records.hpp"
#include "agentflow/json_schema.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <filesystem>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
void exact_fields(const Json& value,std::initializer_list<const char*> allowed){
    if(!value.is_object())throw std::invalid_argument("Invalid trusted xlang3 tool configuration");
    for(auto it=value.begin();it!=value.end();++it)if(std::none_of(allowed.begin(),allowed.end(),[&](const auto* key){return it.key()==key;}))
        throw std::invalid_argument("Unknown trusted xlang3 tool configuration field");
}
std::string required_text(const Json& value,const char* key,std::size_t maximum){
    if(!value.contains(key)||!value.at(key).is_string())throw std::invalid_argument("Invalid trusted xlang3 tool configuration text");
    auto text=value.at(key).get<std::string>();
    if(text.empty()||text.size()>maximum||text.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid trusted xlang3 tool configuration text");
    return text;
}
bool module_name(const std::string& value,bool dotted){
    if(value.empty()||value.size()>256)return false;bool start=true;
    for(const unsigned char c:value){
        if(dotted&&c=='.'){if(start)return false;start=true;continue;}
        if(start){if(!(c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z')))return false;start=false;}
        else if(!(c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')))return false;
    }
    return !start;
}
}

XlangPythonToolConfig parse_xlang_python_tool_config(std::string_view source){
    if(source.empty()||source.size()>262144||source.find('\0')!=std::string_view::npos)throw std::invalid_argument("Trusted xlang3 tool configuration exceeds limits");
    Json root;
    try{
        const auto first=source.find_first_not_of(" \t\r\n");if(first==std::string_view::npos)throw std::invalid_argument("Empty trusted xlang3 tool configuration");
        const auto encoded=(source[first]=='{'||source[first]=='[')?std::string(source):authoring_yaml_to_json(source);
        root=Json::parse(encoded);
    }catch(const std::exception&){throw std::invalid_argument("Invalid trusted xlang3 tool configuration JSON/YAML");}
    exact_fields(root,{"code_root","tools"});
    XlangPythonToolConfig result;result.code_root=required_text(root,"code_root",8192);
    if(!std::filesystem::u8path(result.code_root).is_absolute()||!root.contains("tools")||!root.at("tools").is_array()||root.at("tools").size()>32)
        throw std::invalid_argument("Trusted xlang3 tool configuration requires an absolute code root and at most 32 tools");
    std::set<std::string> names;
    for(const auto& item:root.at("tools")){
        exact_fields(item,{"name","description","input_schema","module","function"});
        XlangPythonToolDefinition tool;
        tool.name=required_text(item,"name",128);tool.description=required_text(item,"description",4096);
        tool.module=required_text(item,"module",256);tool.function=required_text(item,"function",256);
        validate_agent_tool_allowlist(std::vector<std::string>{tool.name});
        if(!names.insert(tool.name).second||!module_name(tool.module,true)||!module_name(tool.function,false)||
            !item.contains("input_schema")||!item.at("input_schema").is_object())
            throw std::invalid_argument("Invalid or duplicate trusted xlang3 tool definition");
        tool.input_schema_json=item.at("input_schema").dump();
        JsonSchema schema(tool.input_schema_json);(void)schema;
        result.tools.push_back(std::move(tool));
    }
    return result;
}

XlangPythonToolConfig load_xlang_python_tool_config(PersistenceService& store){
    try{return parse_xlang_python_tool_config(store.information("xlang-python-tools","catalog").get());}
    catch(const NotFound&){return {};}
}

XlangPythonToolConfig import_xlang_python_tool_config(PersistenceService& store,std::string_view source){
    auto config=parse_xlang_python_tool_config(source);
    Json tools=Json::array();
    for(const auto& tool:config.tools)tools.push_back({{"name",tool.name},{"description",tool.description},
        {"input_schema",Json::parse(tool.input_schema_json)},{"module",tool.module},{"function",tool.function}});
    const auto encoded=Json{{"code_root",config.code_root},{"tools",std::move(tools)}}.dump();
    store.put_information("xlang-python-tools","catalog",encoded).get();
    return config;
}
}
