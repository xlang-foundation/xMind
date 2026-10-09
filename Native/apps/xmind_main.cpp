#include "agentflow/program_entries.hpp"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
namespace {
void help(){
    std::cout<<"xMind Runtime, Coding Harness and AI Model Gateway\n"
        "Usage:\n"
        "  xmind serve --db FILE --modules DIR --stdlib DIR [server options]\n"
        "  xmind [--port PORT] [--workspace DIR] [chat [SESSION [MODEL]] | COMMAND ARGS...]\n"
        "  xmind admin [native administration arguments]\n"
        "  xmind schema-worker   (private bounded schema protocol)\n\n"
        "The console attaches to an authenticated local backend (default port 8765).\n"
        "Set XMIND_AUTH_TOKEN in the client environment. No key is accepted on the command line.\n"
        "Automatic workspace-profile discovery/start and agent-worker mode are pending.\n"
        "Closing the console leaves backend runs alive; /cancel explicitly cancels a run.\n";
}
int port_number(const std::string& value){
    int port=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),port);
    if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||port<1||port>65535)
        throw std::invalid_argument("Invalid console port; use 1..65535");
    return port;
}
int dispatch(int argc,char** argv){
    if(argc<1||!argv||!argv[0])throw std::invalid_argument("Missing program identity");
    if(argc==2&&(std::string_view(argv[1])=="--help"||std::string_view(argv[1])=="help")){help();return 0;}
    if(argc>1){
        const std::string_view mode=argv[1];
        int (*entry)(int,char**)=nullptr;
        if(mode=="serve")entry=run_server;
        else if(mode=="schema-worker")entry=run_schema_worker;
#if defined(_WIN32)
        else if(mode=="admin")entry=run_admin;
#endif
        if(entry){std::vector<char*> forwarded{argv[0]};for(int i=2;i<argc;++i)forwarded.push_back(argv[i]);forwarded.push_back(nullptr);return entry(static_cast<int>(forwarded.size()-1),forwarded.data());}
        if(mode=="worker")throw std::invalid_argument("Agent worker execution is not implemented yet");
    }
    int first=1;std::string port="8765",workspace;bool named_port=false,named_workspace=false;
    while(first<argc&&(std::string_view(argv[first])=="--port"||std::string_view(argv[first])=="--workspace")){
        const std::string_view option=argv[first++];if(first==argc)throw std::invalid_argument("Console option requires a value");
        if(option=="--port"){if(named_port)throw std::invalid_argument("Duplicate console port");named_port=true;port=std::to_string(port_number(argv[first++]));}
        else{if(named_workspace||!*argv[first])throw std::invalid_argument("Invalid or duplicate console workspace");named_workspace=true;workspace=argv[first++];}
    }
    std::string chat="chat";
    std::vector<char*> forwarded{argv[0],port.data()};
    if(first==argc)forwarded.push_back(chat.data());
    else for(int i=first;i<argc;++i)forwarded.push_back(argv[i]);
    forwarded.push_back(nullptr);return cli_main(static_cast<int>(forwarded.size()-1),forwarded.data(),workspace);
}
int guarded(int argc,char** argv){try{return dispatch(argc,argv);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}
}
#if defined(_WIN32)
int wmain(int argc,wchar_t** argv){
    try{
        std::vector<std::string> arguments;arguments.reserve(argc);
        for(int i=0;i<argc;++i){
            const std::wstring value=argv[i];if(value.size()>32768)throw std::invalid_argument("Command argument exceeds limits");
            if(value.empty()){arguments.emplace_back();continue;}
            const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
            if(length<=0)throw std::invalid_argument("Invalid Unicode command argument");
            std::string bytes(length,'\0');
            if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),bytes.data(),length,nullptr,nullptr)!=length)
                throw std::invalid_argument("Cannot encode command argument");
            arguments.push_back(std::move(bytes));
        }
        std::vector<char*> pointers;for(auto& value:arguments)pointers.push_back(value.data());pointers.push_back(nullptr);
        return guarded(argc,pointers.data());
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
#else
int main(int argc,char** argv){return guarded(argc,argv);}
#endif
