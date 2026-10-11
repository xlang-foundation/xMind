#include "agentflow/xlang_python_config.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace agentflow;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Function>void rejects(Function action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected rejection did not occur");}
std::string source(const std::string& root,const std::string& tool="python.upper"){
    return "code_root: \""+root+"\"\ntools:\n  - name: "+tool+"\n    description: Uppercase text\n    module: sample\n    function: uppercase\n    input_schema:\n      type: object\n      properties:\n        text:\n          type: string\n      required: [text]\n      additionalProperties: false\n";
}
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    try{
        const auto database=std::filesystem::u8path(argv[1]);const auto root=database.parent_path().generic_string();
        const auto yaml=source(root);const auto parsed=parse_xlang_python_tool_config(yaml);
        require(parsed.code_root==root&&parsed.tools.size()==1&&parsed.tools[0].name=="python.upper"&&parsed.tools[0].module=="sample"&&parsed.tools[0].function=="uppercase","Trusted YAML tool metadata must be validated and normalized");
        rejects<std::invalid_argument>([&]{parse_xlang_python_tool_config(source("relative/path"));});
        rejects<std::invalid_argument>([&]{parse_xlang_python_tool_config(source(root,"bad name"));});
        rejects<std::invalid_argument>([&]{parse_xlang_python_tool_config("code_root: \""+root+"\"\nunknown: true\ntools: []\n");});
        rejects<std::invalid_argument>([&]{parse_xlang_python_tool_config("code_root: \""+root+"\"\ntools:\n  - name: bad\n    description: invalid schema\n    module: sample\n    function: uppercase\n    input_schema: []\n");});
        {
            PersistenceService store(argv[1],{argv[2],argv[3]});const auto imported=import_xlang_python_tool_config(store,yaml);
            require(imported.tools.size()==1,"Trusted configuration must import the registered callable into xlang3-backed SQLite");
        }
        {
            PersistenceService reopened(argv[1],{argv[2],argv[3]});const auto saved=load_xlang_python_tool_config(reopened);
            require(saved.code_root==root&&saved.tools.size()==1&&saved.tools.front().input_schema_json==parsed.tools.front().input_schema_json,"Tool definitions and code root must survive backend reopen");
            const auto replacement="code_root: \""+root+"\"\ntools: []\n";
            require(import_xlang_python_tool_config(reopened,replacement).tools.empty()&&load_xlang_python_tool_config(reopened).tools.empty(),"A trusted empty catalogue must retire prior registered tools");
        }
        std::cout<<"Trusted xlang3 tool configuration passed YAML/schema validation, malformed-field rejection and xlang3 SQLite import/reopen/replacement.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
