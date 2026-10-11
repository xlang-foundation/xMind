#include "agentflow/xlang_authoring_program.hpp"
#include "agentflow/xlang_python.hpp"
#include "agentflow/xlang_python_config.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace agentflow;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Function>void rejects(Function action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected rejection did not occur");}
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    try{
        const auto root=std::filesystem::u8path(argv[3]);std::filesystem::create_directories(root);
        {std::ofstream file(root/"fixture_author.py",std::ios::binary|std::ios::trunc);file<<
            "def configure(xmind):\n"
            "    xmind.agent({'id': 'reviewer', 'instructions': 'Review the selected change.'})\n"
            "    xmind.graph({'id': 'review', 'spec': {'nodes': [{'id': 'inspect', 'type': 'agent', 'agent_id': 'reviewer', 'prompt': 'Inspect the selected change.'}]}})\n"
            "    xmind.tool({'name': 'text.uppercase', 'description': 'Uppercase supplied text.', 'input_schema': {'type': 'object', 'properties': {'text': {'type': 'string'}}, 'required': ['text'], 'additionalProperties': False}, 'module': 'fixture_author', 'function': 'uppercase'})\n"
            "def uppercase(args):\n"
            "    return {'text': args['text'].upper()}\n"
            "def invalid(xmind):\n"
            "    xmind.agent({'not_an_agent': True})\n"
            "def invalid_tool(xmind):\n"
            "    xmind.tool({'name': 'bad', 'description': 'Invalid schema.', 'input_schema': [], 'module': 'fixture_author', 'function': 'uppercase'})\n";}
        const auto code_root_path=std::filesystem::absolute(root).u8string();
        const std::string code_root(reinterpret_cast<const char*>(code_root_path.data()),code_root_path.size());
        const auto output=run_xlang_authoring_program(argv[1],argv[2],code_root,"fixture_author");
        const auto agents=Json::parse(output.agents_json),graphs=Json::parse(output.graphs_json);
        require(agents.at("agents").size()==1&&agents.at("agents")[0].at("id")=="reviewer","xlang3 program must call the native named-agent authoring API");
        require(graphs.at("graphs").size()==1&&graphs.at("graphs")[0].at("spec").at("nodes")[0].at("agent_id")=="reviewer","xlang3 program must call the native graph authoring API");
        const auto tool_config=Json::parse(output.python_tools_config_json);
        require(tool_config.at("code_root")==code_root&&tool_config.at("tools").size()==1&&tool_config.at("tools")[0].at("name")=="text.uppercase","xlang3 program must export trusted callable tool registrations");
        require(output.agents_json.size()+output.graphs_json.size()<=256*1024,"Authoring result must remain bounded");
        PersistenceService persistence((root/"program.sqlite").string(),{argv[1],argv[2]});
        const auto saved=import_xlang_python_tool_config(persistence,output.python_tools_config_json);
        require(saved.tools.size()==1&&load_xlang_python_tool_config(persistence).tools[0].name=="text.uppercase","Program tools must persist through xlang3 SQLite");
        const auto result=XlangPythonCallable(argv[1],argv[2],saved.code_root,saved.tools[0].module,saved.tools[0].function).invoke_json(R"({"text":"xMind"})");
        require(result==R"({"text":"XMIND"})","Persisted program-defined callable must execute in the native AgentRunner bridge");
        const auto invalid_tool=run_xlang_authoring_program(argv[1],argv[2],code_root,"fixture_author","invalid_tool");
        rejects<std::invalid_argument>([&]{parse_xlang_python_tool_config(invalid_tool.python_tools_config_json);});
        rejects<std::invalid_argument>([&]{run_xlang_authoring_program(argv[1],argv[2],root.string(),"../fixture_author");});
        rejects<std::runtime_error>([&]{run_xlang_authoring_program(argv[1],argv[2],root.string(),"missing_module");});
        std::cout<<"Embedded xlang3 programming-mode contract passed native agent/graph/tool registration, xlang3 SQLite persistence, callable execution, schema rejection and invalid module rejection.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
