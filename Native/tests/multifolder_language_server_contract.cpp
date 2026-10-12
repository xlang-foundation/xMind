#include "agentflow/execution_platform.hpp"
#include "agentflow/http_server.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>

using namespace agentflow;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Function> void rejects(Function action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected rejection did not occur");}
std::string env(const char* name){const auto* value=std::getenv(name);return value?value:"";}
std::string utf8(const std::filesystem::path& value){const auto text=value.u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
struct TempDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/"xmind-multifolder-lsp-contract";
    TempDirectory(){path+=std::to_string(std::random_device{}());require(std::filesystem::create_directory(path),"LSP fixture directory must be unique");}
    ~TempDirectory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
struct Serving {
    HttpServer& server;std::thread worker;
    explicit Serving(HttpServer& value):server(value),worker([&value]{value.listen();}){}
    ~Serving(){server.stop();if(worker.joinable())worker.join();}
};
void create_project(const std::filesystem::path& root,const std::string& function){
    std::filesystem::create_directories(root/"src");
    std::ofstream manifest(root/"Cargo.toml",std::ios::binary);manifest<<"[package]\nname = \"xmind_lsp_"<<function<<"\"\nversion = \"0.1.0\"\nedition = \"2021\"\n[lib]\npath = \"src/lib.rs\"\n";manifest.close();
    std::ofstream source(root/"src/lib.rs",std::ios::binary);source<<"pub fn "<<function<<"() {}\n";
}
LanguageServerProfile rust_profile(const std::string& executable){
    LanguageServerProfile profile;profile.id="rust-analyzer";profile.executable=executable;profile.language_id="rust";profile.extensions={".rs"};profile.request_timeout=std::chrono::seconds(90);
    const auto path=env("PATH");if(!path.empty())profile.environment.emplace_back("PATH",std::filesystem::u8path(executable).parent_path().string()+";"+path);
    const auto rustup=env("RUSTUP_HOME");if(!rustup.empty())profile.environment.emplace_back("RUSTUP_HOME",rustup);
    const auto user=env("USERPROFILE");if(!user.empty()){profile.environment.emplace_back("USERPROFILE",user);profile.environment.emplace_back("HOME",user);profile.environment.emplace_back("CARGO_HOME",user+"/.cargo");}
    return profile;
}
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    try{
        TempDirectory temp;const auto primary_path=utf8(temp.path/"primary"),secondary_path=utf8(temp.path/"secondary");
        create_project(std::filesystem::u8path(primary_path),"primary_only");create_project(std::filesystem::u8path(secondary_path),"secondary_only");
        WorkspaceTools primary(primary_path),secondary(secondary_path);const auto workspace_id=primary.identity(),primary_root=primary.root_path(),secondary_id=secondary.identity(),secondary_root=secondary.root_path();
        std::atomic_bool secondary_registered=true;PersistenceService persistence(utf8(temp.path/"profile.sqlite"),{argv[2],argv[3]});AgentSettings settings;settings.workspace=primary_path;settings.language_servers.push_back(rust_profile(argv[1]));
        settings.workspace_membership_snapshot=[workspace_id,primary_root,secondary_id,secondary_root,&secondary_registered]{WorkspaceMembershipSnapshot snapshot{workspace_id,secondary_registered?2:1,{{workspace_id,primary_root}}};if(secondary_registered)snapshot.folders.push_back({secondary_id,secondary_root});return snapshot;};
        ExecutionPlatform runtime(persistence,std::move(settings),1,8);const std::string token="native-multifolder-language-server-contract-token";HttpServer server(persistence,token,&runtime);const auto port=server.bind(0);require(port>0,"Native language-server endpoint must bind");Serving serving(server);
        httplib::Client client("127.0.0.1",port);client.set_connection_timeout(2,0);client.set_read_timeout(100,0);client.set_follow_location(false);const httplib::Headers headers{{"Authorization","Bearer "+token}};
        httplib::Result workspace;for(int attempt=0;attempt<50;++attempt){workspace=client.Get("/v1/workspace",headers);if(workspace)break;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
        require(workspace&&workspace->status==200,"Native workspace metadata must be available");const auto metadata=Json::parse(workspace->body);Json binding{{"expected_workspace_id",metadata["workspace_id"]},{"expected_workspace_authority_id",metadata["authority_id"]}};
        const auto symbols_for=[&](const std::string& folder_id,const std::string& expected){Json input{{"path","src/lib.rs"},{"method","documentSymbol"}};if(!folder_id.empty())input["folder_id"]=folder_id;input.update(binding);httplib::Result response;Json symbols;
            for(int attempt=0;attempt<60;++attempt){response=client.Post("/v1/language-server/query",headers,input.dump(),"application/json");if(response&&response->status==200){const auto body=Json::parse(response->body);symbols=body["result"];if(symbols.is_array()&&!symbols.empty()&&symbols[0].value("name","")==expected){if(!folder_id.empty())require(body.value("folder_id","")==folder_id,"LSP response must echo the selected folder identity");return;}}std::this_thread::sleep_for(std::chrono::milliseconds(500));}
            throw std::runtime_error("Rust Analyzer did not return symbols from the requested workspace folder: "+expected);
        };
        symbols_for({},"primary_only");symbols_for(secondary_id,"secondary_only");
        LanguageServerToolRegistry agent_tools(primary,{rust_profile(argv[1])});
        const std::vector<WorkspaceToolFolder> admitted{{workspace_id,primary_root},{secondary_id,secondary_root}};
        const auto tool_result=Json::parse(agent_tools.invoke(Json{{"server_id","rust-analyzer"},{"method","documentSymbol"},{"path","src/lib.rs"},{"folder_id",secondary_id}}.dump(),{},&admitted,
            [&](const std::string& server_id,const LanguageServerRequest& request,const std::vector<WorkspaceToolFolder>& folders){
                require(server_id=="rust-analyzer","Agent language-server calls must retain the selected trusted profile");
                require(folders.size()==2&&folders[1].folder_id==secondary_id&&folders[1].root==secondary_root,"Agent language-server dispatch must receive the immutable admitted folder roots");
                return runtime.language_server_query(request);
            }));
        require(tool_result.is_object()&&tool_result.at("result").is_array()&&!tool_result.at("result").empty()&&tool_result.at("result")[0].value("name","")=="secondary_only","Model-visible language-server tool must query the selected admitted folder");
        rejects<ToolAccessDenied>([&]{agent_tools.invoke(Json{{"server_id","rust-analyzer"},{"method","documentSymbol"},{"path","src/lib.rs"},{"folder_id","windows-local-file-v1:unregistered"}}.dump(),{},&admitted,
            [&](const std::string&,const LanguageServerRequest&,const std::vector<WorkspaceToolFolder>&)->std::string{throw std::runtime_error("Unadmitted model folder must never reach dispatch");} );});
        Json unknown{{"path","src/lib.rs"},{"method","documentSymbol"},{"folder_id","windows-local-file-v1:unregistered"}};unknown.update(binding);const auto unknown_response=client.Post("/v1/language-server/query",headers,unknown.dump(),"application/json");require(unknown_response&&unknown_response->status==403,"Unregistered folder IDs must be rejected before language-server dispatch");
        secondary_registered=false;Json removed{{"path","src/lib.rs"},{"method","documentSymbol"},{"folder_id",secondary_id}};removed.update(binding);const auto removed_response=client.Post("/v1/language-server/query",headers,removed.dump(),"application/json");require(removed_response&&removed_response->status==403,"Folder IDs removed from current membership must be rejected");
        std::cout<<"Real Rust Analyzer resolved colliding src/lib.rs paths under their selected registered roots; unknown and removed folder IDs were rejected.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
