#include "agentflow/language_server_tools.hpp"
#include "agentflow/execution_platform.hpp"
#include "agentflow/http_server.hpp"
#include "httplib.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <thread>
#include "nlohmann/json.hpp"
using namespace agentflow;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::string env(const char* name){const auto* value=std::getenv(name);return value?value:"";}
std::string utf8(const std::filesystem::path& path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
struct TempDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/"xmind-language-server-http-contract";
    TempDirectory(){path+=std::to_string(std::random_device{}());require(std::filesystem::create_directory(path),"HTTP fixture directory must be unique");}
    ~TempDirectory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
struct Serving {
    HttpServer& server;std::thread worker;
    explicit Serving(HttpServer& value):server(value),worker([&value]{value.listen();}){}
    ~Serving(){server.stop();if(worker.joinable())worker.join();}
};
LanguageServerProfile rust_profile(const std::string& executable){
    LanguageServerProfile profile;profile.id="rust-analyzer";profile.executable=executable;profile.language_id="rust";profile.extensions={".rs"};profile.request_timeout=std::chrono::seconds(90);
    const auto path=env("PATH");if(!path.empty())profile.environment.emplace_back("PATH",std::filesystem::u8path(executable).parent_path().string()+";"+path);
    const auto rustup=env("RUSTUP_HOME");if(!rustup.empty())profile.environment.emplace_back("RUSTUP_HOME",rustup);
    const auto user=env("USERPROFILE");if(!user.empty()){profile.environment.emplace_back("USERPROFILE",user);profile.environment.emplace_back("HOME",user);profile.environment.emplace_back("CARGO_HOME",user+"/.cargo");}
    return profile;
}
void http_contract(const std::string& executable,const std::string& workspace_path,const std::string& path,const std::string& modules,const std::string& pure_python){
    TempDirectory temp;PersistenceService persistence(utf8(temp.path/"lsp.sqlite"),{modules,pure_python});AgentSettings settings;settings.workspace=workspace_path;settings.language_servers.push_back(rust_profile(executable));
    ExecutionPlatform runtime(persistence,std::move(settings),1,8);const std::string token="synthetic-language-server-http-access-token-32-bytes";HttpServer server(persistence,token,&runtime);const auto port=server.bind(0);require(port>0,"Native language-server HTTP API failed to bind");Serving serving(server);
    httplib::Client client("127.0.0.1",port);client.set_connection_timeout(2,0);client.set_read_timeout(100,0);client.set_follow_location(false);const httplib::Headers headers{{"Authorization","Bearer "+token}};
    httplib::Result workspace;
    for(int i=0;i<50;++i){workspace=client.Get("/v1/workspace",headers);if(workspace)break;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    require(workspace&&workspace->status==200,"Workspace-bound LSP endpoint requires a running native backend");const auto metadata=Json::parse(workspace->body);require(metadata["configured"]==true,"LSP HTTP runtime must own the configured workspace");
    const auto binding=Json{{"expected_workspace_id",metadata["workspace_id"]},{"expected_workspace_authority_id",metadata["authority_id"]}};
    Json input={{"path",path},{"method","documentSymbol"},{"line",0},{"character",0}};input.update(binding);
    const auto response=client.Post("/v1/language-server/query",headers,input.dump(),"application/json");require(response&&response->status==200,"Authenticated workspace-bound LSP query must reach the registered language server");const auto result=Json::parse(response->body);
    require(result["server_id"]=="rust-analyzer"&&result["path"]==path&&result["result"].is_array()&&result["result"].size()==2&&result["result"][0]["name"]=="add","HTTP editor query must return actual Rust Analyzer symbols from the opened workspace");
    auto unsaved=input;unsaved["document_text"]="pub fn add(left: i32, right: i32) -> i32 { left + right }\npub fn call_add() -> i32 { add(1, 2) }\npub fn unsaved_marker() {}\n";const auto edited=client.Post("/v1/language-server/query",headers,unsaved.dump(),"application/json");require(edited&&edited->status==200,"Editor query must accept the current unsaved document buffer");const auto edited_result=Json::parse(edited->body);require(edited_result["result"].is_array()&&edited_result["result"].size()==3&&edited_result["result"][2]["name"]=="unsaved_marker","Real Rust Analyzer symbols must reflect an unsaved editor-only declaration");
    const auto restored=client.Post("/v1/language-server/query",headers,input.dump(),"application/json");require(restored&&restored->status==200&&Json::parse(restored->body)["result"].size()==2,"Restoring the workspace file text must remove the unsaved symbol without changing the file");
    const auto view_issue=client.Post("/v1/view-sessions",headers,Json{{"origin","http://127.0.0.1:42424"}}.dump(),"application/json");require(view_issue&&view_issue->status==200,"Local browser view must enroll before using workspace-bound LSP metadata");const auto view_token=Json::parse(view_issue->body).at("credential").get<std::string>();const httplib::Headers view_headers{{"Authorization","View "+view_token},{"X-XMind-View-Origin","http://127.0.0.1:42424"}};
    const auto profiles=client.Get("/v1/language-servers",view_headers);require(profiles&&profiles->status==200&&Json::parse(profiles->body)["profiles"].size()==1,"Authorized local views must discover sanitized language-server profiles");
    const auto view_response=client.Post("/v1/language-server/query",view_headers,input.dump(),"application/json");require(view_response&&view_response->status==200&&Json::parse(view_response->body)["result"]==result["result"],"Authorized local views must query the same native language-server process");
    auto stale=input;stale["expected_workspace_authority_id"]=std::string(32,'0');const auto rejected=client.Post("/v1/language-server/query",headers,stale.dump(),"application/json");require(rejected&&rejected->status==409,"Stale workspace authority must be rejected before language-server dispatch");
    httplib::Client anonymous_client("127.0.0.1",port);const auto anonymous=anonymous_client.Post("/v1/language-server/query",input.dump(),"application/json");require(anonymous&&anonymous->status==401,"Editor LSP query must retain native backend authentication");
}
}
int main(int argc,char** argv){
    if(argc!=6)return 2;
    try{
        WorkspaceTools workspace(argv[2]);LanguageServerProfile profile;
        profile=rust_profile(argv[1]);const std::string path=argv[3];
        LanguageServerToolRegistry registry(workspace,{profile});
        require(registry.definitions().size()==1&&registry.definitions()[0].name=="language_server","Agent catalogue must expose one configured read-only language-server tool");
        const auto request=[&](const char* method){return nlohmann::json::parse(registry.invoke(nlohmann::json{{"server_id",profile.id},{"method",method},{"path",path}}.dump()));};
        const auto symbols=request("documentSymbol");
        require(symbols["result"].is_array()&&symbols["result"].size()==2,"Real rust-analyzer must return the two declarations in the workspace source file");
        require(symbols["result"][0]["name"]=="add"&&symbols["result"][1]["name"]=="call_add","Real rust-analyzer symbols must match the Rust fixture");
        const auto repeated=request("documentSymbol");
        require(repeated["result"]==symbols["result"],"A persistent language-server process must answer repeated workspace requests consistently");
        const auto diagnostics=request("diagnostics");
        require(diagnostics["result"].is_object()&&diagnostics["result"]["items"].is_array()&&diagnostics["result"]["items"].empty(),"Real rust-analyzer must report the valid fixture without diagnostics");
        const auto editor=registry.query(LanguageServerRequest{path,"documentSymbol",0,0});require(Json::parse(editor)["result"]==symbols["result"],"Editor and AgentRunner queries must share the same trusted server-selection behavior");
        http_contract(argv[1],argv[2],path,argv[4],argv[5]);
        std::cout<<"Actual rust-analyzer verified persistent native stdio, AgentRunner tool queries, workspace-authority-bound authenticated HTTP editor queries, repeated symbols and clean diagnostics. xlang3 SQLite initialized the real backend; no model or synthetic LSP peer used\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
