#include "agentflow/xlang_python.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace agentflow;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Function>void rejects(Function action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected rejection did not occur");}
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    try{
        const auto root=std::filesystem::u8path(argv[3]);
        std::filesystem::create_directories(root);
        {std::ofstream file(root/"fixture_tools.py",std::ios::binary|std::ios::trunc);file<<
            "def summarize(args):\n"
            "    return {'text': args['text'].upper(), 'count': len(args['text'])}\n"
            "def invalid(args):\n"
            "    return object()\n";}
        XlangPythonCallable call(argv[1],argv[2],root.string(),"fixture_tools","summarize");
        require(call.invoke_json(R"({"text":"xMind"})")==R"({"count":5,"text":"XMIND"})","Actual embedded xlang3 function must receive and return JSON values");
        rejects<std::invalid_argument>([&]{call.invoke_json("[]");});
        rejects<std::invalid_argument>([&]{XlangPythonCallable invalid(argv[1],argv[2],root.string(),"../fixture_tools","summarize");});
        XlangPythonCallable invalid(argv[1],argv[2],root.string(),"fixture_tools","invalid");
        rejects<std::runtime_error>([&]{invalid.invoke_json("{}");});
        std::stop_source cancellation;cancellation.request_stop();
        rejects<std::runtime_error>([&]{call.invoke_json("{}",cancellation.get_token());});
        std::cout<<"Embedded xlang3 trusted-callable contract passed actual JSON argument/result conversion, callable dispatch, invalid binding/arguments/results and pre-dispatch cancellation. This is not a sandbox or agent registration test.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
