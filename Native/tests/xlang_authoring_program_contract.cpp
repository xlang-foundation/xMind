#include "agentflow/xlang_authoring_program.hpp"
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
            "def invalid(xmind):\n"
            "    xmind.agent({'not_an_agent': True})\n";}
        const auto output=run_xlang_authoring_program(argv[1],argv[2],root.string(),"fixture_author");
        const auto agents=Json::parse(output.agents_json),graphs=Json::parse(output.graphs_json);
        require(agents.at("agents").size()==1&&agents.at("agents")[0].at("id")=="reviewer","xlang3 program must call the native named-agent authoring API");
        require(graphs.at("graphs").size()==1&&graphs.at("graphs")[0].at("spec").at("nodes")[0].at("agent_id")=="reviewer","xlang3 program must call the native graph authoring API");
        require(output.agents_json.size()+output.graphs_json.size()<=256*1024,"Authoring result must remain bounded");
        rejects<std::invalid_argument>([&]{run_xlang_authoring_program(argv[1],argv[2],root.string(),"../fixture_author");});
        rejects<std::runtime_error>([&]{run_xlang_authoring_program(argv[1],argv[2],root.string(),"missing_module");});
        std::cout<<"Embedded xlang3 programming-mode contract passed native agent/graph registration, JSON definition transport, bounded output and invalid module rejection. Persistence/AgentRunner execution are tested independently.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
