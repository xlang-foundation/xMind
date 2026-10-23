#include "agentflow/program_entries.hpp"
#include "agentflow/local_profile.hpp"
#include "agentflow/workspace_registry.hpp"
#include "agentflow/workspace_tools.hpp"
#include "agentflow/agent_worker.hpp"
#include "agentflow/agent_worker_entry.hpp"
#include "nlohmann/json.hpp"
#include <charconv>
#include <cstdlib>
#include <filesystem>
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
        "  xmind ipc --endpoint lrpc:PORT host | connections list | connections save REV JSON_ARRAY | connections select ID REV | connections open ID REV | connections session-create ID REV SESSION TITLE | connections session-open ID REV SESSION | connections run-submit ID REV JSON_OBJECT | connections run-start ID REV RUN SESSION PROMPT | connections run-open ID REV RUN | connections run-cancel ID REV RUN | connections operations ID REV SESSION RUN | connections operation ID REV SESSION RUN OPERATION | connections operation-decide ID REV SESSION RUN OPERATION allow|deny | connections events ID REV SESSION RUN JSON_CURSOR | session create ID TITLE | session open ID | run open ID | run start ID SESSION PROMPT | run cancel ID\n"
        "  xmind serve --db FILE --modules DIR --stdlib DIR [--agent-execution thread|process-read|process-code] [server options]\n"
        "  xmind [--workspace DIR] [--profile-root DIR] [--config FILE] [--python-tools-config FILE] [--language-server-config FILE] [--process-config FILE] [--mcp-config FILE] [COMMAND ARGS...]\n"
        "  xmind --port PORT [--workspace DIR] [COMMAND ARGS...]\n"
        "  xmind [local profile options] profile-info\n"
        "  xmind [local profile options] workspaces\n"
        "  xmind [local profile options] workspace init\n"
        "  xmind --workspace DIR workspace info\n"
        "  xmind --workspace DIR workspace add-folder FOLDER\n"
        "  xmind [connection options] mcp-auth\n"
        "  xmind [connection options] mcp-login SERVER\n"
        "  xmind [connection options] mcp-refresh SERVER\n"
        "  xmind [connection options] mcp-login-status REQUEST_ID\n"
        "  xmind [connection options] mcp-login-open REQUEST_ID\n"
        "  xmind [connection options] mcp-login-cancel REQUEST_ID\n"
        "  xmind [connection options] mcp-login-watch REQUEST_ID\n"
        "  xmind [connection options] git-status [FOLDER_ID]\n"
        "  xmind [connection options] reconcile-process OPERATION_ID --acknowledge-possible-side-effects NOTE\n"
        "  xmind [connection options] git-info [FOLDER_ID]\n"
        "  xmind [connection options] git-branches [FOLDER_ID]\n"
        "  xmind [connection options] git-base [FOLDER_ID]\n"
        "  xmind [connection options] git-diff MODE [BASE [CONTEXT]] [--folder-id ID]\n"
        "  xmind view --ready-file FILE [local profile options]\n"
        "  xmind admin [native administration arguments]\n"
        "  xmind schema-worker   (private bounded schema protocol)\n"
        "  xmind worker         (private xlang3 agent tool worker)\n\n"
        "Without --port, the console discovers or starts its persistent local workspace profile.\n"
        "workspace init uses --workspace (or the current directory) as the first folder. Add folders later with workspace add-folder; workspace info lists IDs accepted by Git commands.\n"
        "--config selects provider YAML; --python-tools-config selects trusted embedded xlang3 tools; --language-server-config selects trusted LSP servers; --process-config selects trusted process profiles; --mcp-config selects trusted MCP servers. --agent-execution selects thread, process-read or process-code for the managed backend.\n"
        "--upgrade-backend safely replaces the managed profile runtime when the current owner supports it.\n"
        "Explicit --port attaches to an operator-selected backend using XMIND_AUTH_TOKEN.\n"
        "No key is accepted on the command line. Worker execution is private to the local runtime.\n"
        "Closing the console leaves backend runs alive; /cancel explicitly cancels a run.\n";
}
int port_number(const std::string& value){
    int port=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),port);
    if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||port<1||port>65535)
        throw std::invalid_argument("Invalid console port; use 1..65535");
    return port;
}
std::string path_text(const std::filesystem::path& path){
    const auto value=path.u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}
#if defined(_WIN32)
std::string package_root(){
    std::wstring image(32768,L'\0');
    const auto size=GetModuleFileNameW(nullptr,image.data(),static_cast<DWORD>(image.size()));
    if(size==0||size>=image.size())throw std::runtime_error("Cannot resolve the unified xMind package directory");
    image.resize(size);
    if(_wcsicmp(std::filesystem::path(image).filename().c_str(),L"xmind.exe")!=0)
        throw std::runtime_error("Workspace registry commands require the unified xmind.exe package");
    return path_text(std::filesystem::path(image).parent_path());
}
agentflow::WorkspaceRegistry workspace_registry(const agentflow::LocalProfileConnection& connection){
    const auto profile_root=std::filesystem::u8path(connection.directory).parent_path();
    const auto root=path_text(profile_root);
    const auto package=std::filesystem::u8path(package_root());
    return agentflow::WorkspaceRegistry(path_text(profile_root/L"workspaces.sqlite"),
        {path_text(package/L"modules"),path_text(package/L"stdlib")},root);
}
nlohmann::json workspace_json(const agentflow::WorkspaceGroupRecord& group){
    auto folders=nlohmann::json::array();
    for(const auto& folder:group.folders)
        folders.push_back({{"folder_id",folder.folder_id},{"name",folder.name},{"root",folder.root},{"ordinal",folder.ordinal}});
    return {{"workspace_id",group.workspace_id},{"name",group.name},{"revision",group.revision},
            {"primary_folder_id",group.primary_folder_id},{"folders",std::move(folders)}};
}
#endif
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
        else if(mode=="ipc")entry=agentflow::run_xmind_ipc_client;
        else if(mode=="view")entry=run_local_view;
        else if(mode=="worker")entry=agentflow::run_agent_worker;
        else if(mode=="agent-channel-worker")entry=agentflow::run_agent_channel_worker;
#endif
        if(entry){std::vector<char*> forwarded{argv[0]};for(int i=2;i<argc;++i)forwarded.push_back(argv[i]);forwarded.push_back(nullptr);return entry(static_cast<int>(forwarded.size()-1),forwarded.data());}
    }
    int first=1;std::string port="8765",workspace;bool named_port=false;agentflow::LocalProfileOptions options;std::set<std::string> seen;
    while(first<argc&&std::string_view(argv[first]).starts_with("--")){
        const std::string option=argv[first++];if(!seen.insert(option).second)throw std::invalid_argument("Duplicate console option");
        if(option=="--read-only"){options.approved_edits=false;continue;}
        if(option=="--workspace-edits"){
            if(first==argc||std::string_view(argv[first++])!="approved")throw std::invalid_argument("--workspace-edits requires the value approved");
            options.approved_edits=true;continue;
        }
        if(option=="--upgrade-backend"){options.upgrade_backend=true;continue;}
        if(option!="--port"&&option!="--workspace"&&option!="--profile-root"&&option!="--config"&&option!="--graphs-config"&&option!="--python-tools-config"&&option!="--language-server-config"&&option!="--process-config"&&option!="--mcp-config"&&option!="--agent-execution")throw std::invalid_argument("Unknown console option");
        if(first==argc||!*argv[first])throw std::invalid_argument("Console option requires a value");const std::string value=argv[first++];
        if(option=="--port"){named_port=true;port=std::to_string(port_number(value));}
        else if(option=="--workspace"){workspace=value;options.workspace=value;}
        else if(option=="--profile-root")options.profile_root=value;
        else if(option=="--config")options.provider_config=value;
        else if(option=="--python-tools-config")options.python_tools_config=value;
        else if(option=="--language-server-config")options.language_server_config=value;
        else if(option=="--process-config")options.process_config=value;
        else if(option=="--mcp-config")options.mcp_config=value;
        else if(option=="--agent-execution")options.agent_execution=value;
        else options.graphs_config=value;
    }
    if(named_port&&(!options.profile_root.empty()||!options.provider_config.empty()||!options.graphs_config.empty()||!options.python_tools_config.empty()||!options.language_server_config.empty()||!options.process_config.empty()||!options.mcp_config.empty()||options.approved_edits||options.agent_execution||options.upgrade_backend))throw std::invalid_argument("Local profile launch options cannot change an explicitly selected backend");
    std::function<agentflow::LocalProfileConnection()> profile;
#if defined(_WIN32)
    if(!named_port)profile=[options]{return agentflow::connect_local_profile(options);};
#else
    if(!named_port)throw std::invalid_argument("Managed local profiles currently require Windows; use an explicit backend port");
#endif
    if(first<argc&&std::string_view(argv[first])=="profile-info"){
        if(named_port||first+1!=argc)throw std::invalid_argument("profile-info requires a managed local profile and no command arguments");
        const auto connected=profile();std::cout<<nlohmann::json{{"origin","http://127.0.0.1:"+std::to_string(connected.port)},{"workspace",connected.workspace},{"profile_directory",connected.directory},{"process_id",connected.process_id},{"started",connected.started},{"workspace_edits",connected.approved_edits?"approved":"read_only"},{"agent_execution",connected.agent_execution}}.dump()<<'\n';return 0;
    }
    if(first<argc&&std::string_view(argv[first])=="workspaces"){
        if(named_port||first+1!=argc)throw std::invalid_argument("workspaces requires local profile storage and no command arguments");
        auto workspaces=nlohmann::json::array();
        for(const auto& value:agentflow::list_local_workspace_profiles(options.profile_root))
            workspaces.push_back({{"workspace_id",value.workspace_id},{"root",value.root},{"name",value.name}});
        std::cout<<nlohmann::json{{"workspaces",std::move(workspaces)}}.dump()<<'\n';return 0;
    }
    if(first<argc&&std::string_view(argv[first])=="workspace"){
        if(named_port)throw std::invalid_argument("Workspace registry commands require a managed local profile");
        if(first+1>=argc)throw std::invalid_argument("workspace requires init, info, or add-folder");
        const std::string_view action=argv[first+1];
        if(action=="init"){
            if(first+2!=argc)throw std::invalid_argument("workspace init takes its primary folder from --workspace or the current directory");
#if defined(_WIN32)
            const auto connected=profile();
            auto registry=workspace_registry(connected);
            std::cout<<workspace_json(registry.create(connected.workspace)).dump()<<'\n';return 0;
#else
            throw std::invalid_argument("Managed local profiles currently require Windows");
#endif
        }
        if(action=="info"){
            if(first+2!=argc)throw std::invalid_argument("workspace info accepts no arguments");
#if defined(_WIN32)
            options.require_existing_profile=true;
            const auto connected=profile();
            auto registry=workspace_registry(connected);
            std::cout<<workspace_json(registry.get(agentflow::WorkspaceTools(connected.workspace).identity())).dump()<<'\n';return 0;
#else
            throw std::invalid_argument("Managed local profiles currently require Windows");
#endif
        }
        if(action=="add-folder"){
            if(first+3!=argc||!argv[first+2]||!*argv[first+2])throw std::invalid_argument("workspace add-folder requires one existing folder path");
#if defined(_WIN32)
            options.require_existing_profile=true;
            const auto connected=profile();
            auto registry=workspace_registry(connected);
            std::cout<<workspace_json(registry.add_folder(agentflow::WorkspaceTools(connected.workspace).identity(),argv[first+2])).dump()<<'\n';return 0;
#else
            throw std::invalid_argument("Managed local profiles currently require Windows");
#endif
        }
        throw std::invalid_argument("Unknown workspace command");
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
