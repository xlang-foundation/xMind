#include "agentflow/program_entries.hpp"
#include "nlohmann/json.hpp"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <set>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
namespace {
void help(){
    std::cout<<"xMind Runtime, Coding Harness and AI Model Gateway\n"
        "Usage:\n"
        "  xmind serve --db FILE --modules DIR --stdlib DIR [server options]\n"
        "  xmind [--workspace DIR] [--profile-root DIR] [--config FILE] [COMMAND ARGS...]\n"
        "  xmind --port PORT [--workspace DIR] [COMMAND ARGS...]\n"
        "  xmind [local profile options] profile-info\n"
        "  xmind admin [native administration arguments]\n"
        "  xmind schema-worker   (private bounded schema protocol)\n\n"
        "Without --port, the console discovers or starts its persistent local workspace profile.\n"
        "--config selects the one provider YAML; existing profiles retain their configuration.\n"
        "Explicit --port attaches to an operator-selected backend using XMIND_AUTH_TOKEN.\n"
        "No key is accepted on the command line. Agent-worker execution remains pending.\n"
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
    int first=1;std::string port="8765",workspace;bool named_port=false;agentflow::LocalProfileOptions options;std::set<std::string> seen;
    while(first<argc&&std::string_view(argv[first]).starts_with("--")){
        const std::string option=argv[first++];if(!seen.insert(option).second)throw std::invalid_argument("Duplicate console option");
        if(option=="--read-only"){options.approved_edits=false;continue;}
        if(option!="--port"&&option!="--workspace"&&option!="--profile-root"&&option!="--config"&&option!="--graphs-config")throw std::invalid_argument("Unknown console option");
        if(first==argc||!*argv[first])throw std::invalid_argument("Console option requires a value");const std::string value=argv[first++];
        if(option=="--port"){named_port=true;port=std::to_string(port_number(value));}
        else if(option=="--workspace"){workspace=value;options.workspace=value;}
        else if(option=="--profile-root")options.profile_root=value;
        else if(option=="--config")options.provider_config=value;
        else options.graphs_config=value;
    }
    if(named_port&&(!options.profile_root.empty()||!options.provider_config.empty()||!options.graphs_config.empty()||options.approved_edits))throw std::invalid_argument("Local profile launch options cannot change an explicitly selected backend");
    std::function<agentflow::LocalProfileConnection()> profile;
#if defined(_WIN32)
    if(!named_port)profile=[options]{return agentflow::connect_local_profile(options);};
#else
    if(!named_port)throw std::invalid_argument("Managed local profiles currently require Windows; use an explicit backend port");
#endif
    if(first<argc&&std::string_view(argv[first])=="profile-info"){
        if(named_port||first+1!=argc)throw std::invalid_argument("profile-info requires a managed local profile and no command arguments");
        const auto connected=profile();std::cout<<nlohmann::json{{"origin","http://127.0.0.1:"+std::to_string(connected.port)},{"workspace",connected.workspace},{"profile_directory",connected.directory},{"process_id",connected.process_id},{"started",connected.started}}.dump()<<'\n';return 0;
    }
    std::string chat="chat";
    std::vector<char*> forwarded{argv[0],port.data()};
    if(first==argc)forwarded.push_back(chat.data());
    else for(int i=first;i<argc;++i)forwarded.push_back(argv[i]);
    forwarded.push_back(nullptr);return cli_main(static_cast<int>(forwarded.size()-1),forwarded.data(),workspace,profile);
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
