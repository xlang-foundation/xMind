#include "agentflow/provider_profile_runtime.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>
#include <thread>
using namespace agentflow;using Json=nlohmann::json;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct Directory {
    std::filesystem::path root=std::filesystem::temp_directory_path(),path=root/("xmind-lsp-responsive-"+std::to_string(std::random_device{}()));
    Directory(){require(std::filesystem::create_directory(path),"Unique LSP fixture required");std::ofstream(path/"check.ts")<<"const value = 1;\n";}
    ~Directory(){if(path.parent_path()==root&&path.filename().string().starts_with("xmind-lsp-responsive-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
void entered(const std::filesystem::path& file){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!std::filesystem::exists(file)){require(std::chrono::steady_clock::now()<deadline,"Waiting LSP did not receive diagnostics");std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
void release(const std::filesystem::path& directory){std::ofstream(directory/"release")<<"continue";}
std::vector<ProviderProfileExecutionPolicy> policies(){
    ProviderProfileRoute route{"openai.chat","openai","http://127.0.0.1:1/chat","server","fixture:responsive-lsp",ProviderWire::chat_completions};ChatProviderConfig provider;provider.endpoint=route.endpoint;provider.tools=Capability::supported;provider.stream_usage=Capability::supported;provider.output_limit=Capability::supported;return {{route,provider}};
}
void scenario(const char* node,const char* peer,const char* modules,const char* pure,bool replace){
    Directory fixture;PersistenceService store((fixture.path/"state.sqlite").string(),{modules,pure});AgentSettings settings;settings.workspace=fixture.path.string();settings.a2a_enabled=false;
    LanguageServerProfile profile;profile.id="waiting";profile.executable=node;profile.arguments={peer,fixture.path.string()};profile.language_id="typescript";profile.extensions={".ts"};profile.request_timeout=std::chrono::seconds(5);settings.language_servers.push_back(profile);
    ProviderProfileRuntime runtime(store,std::move(settings),policies(),1,8);const auto workspace=runtime.execution_workspace();LanguageServerRequest request;request.path="check.ts";request.method="diagnostics";
    auto query=std::async(std::launch::async,[&]{return runtime.language_server_query(request);});entered(fixture.path/"entered");
    if(!replace){
        auto metadata=std::async(std::launch::async,[&]{const auto configuration=runtime.configuration();const auto actual=runtime.execution_workspace();return configuration.revision==0&&actual.workspace_id==workspace.workspace_id;});
        const bool responsive=metadata.wait_for(std::chrono::seconds(1))==std::future_status::ready;release(fixture.path);
        const auto result=Json::parse(query.get());const bool correct=metadata.get();require(responsive,"A waiting editor LSP query must not block provider/workspace metadata");require(correct&&result["result"]["items"].empty(),"Unchanged generation must return its actual LSP result");
    }else{
        auto change=std::async(std::launch::async,[&]{const std::string secret="synthetic-responsive-language-profile-key";return runtime.save_profile("local","openai.chat","fixture-model",SecretBytes({reinterpret_cast<const std::uint8_t*>(secret.data()),secret.size()}),0,true);});
        const bool responsive=change.wait_for(std::chrono::seconds(1))==std::future_status::ready;release(fixture.path);
        bool stale=false;try{(void)query.get();}catch(const Conflict&){stale=true;}const auto configured=change.get();
        require(responsive,"Provider configuration must not wait for an old editor LSP query");require(configured.revision==1&&stale,"An LSP reply from a replaced execution generation must be rejected");
        require(runtime.execution_workspace().workspace_id==workspace.workspace_id,"Replacing a provider must preserve the captured workspace");
    }
}
}
int main(int argc,char** argv){if(argc!=5)return 2;try{scenario(argv[1],argv[2],argv[3],argv[4],false);scenario(argv[1],argv[2],argv[3],argv[4],true);std::cout<<"Native provider runtime keeps workspace/settings responsive during an actual waiting LSP request, retains its service lifetime, and rejects replies from replaced generations. Synthetic LSP only; no provider request.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
