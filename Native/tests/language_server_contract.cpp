#include "agentflow/language_server_tools.hpp"
#include <cstdlib>
#include <iostream>
#include "nlohmann/json.hpp"
using namespace agentflow;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::string env(const char* name){const auto* value=std::getenv(name);return value?value:"";}
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    try{
        WorkspaceTools workspace(argv[2]);LanguageServerProfile profile;
        profile.id="rust-analyzer";profile.executable=argv[1];profile.language_id="rust";profile.extensions={".rs"};profile.request_timeout=std::chrono::seconds(90);
        const auto path=env("PATH");if(!path.empty())profile.environment.emplace_back("PATH",std::string(argv[1]).substr(0,std::string(argv[1]).find_last_of("/\\"))+";"+path);
        const auto rustup=env("RUSTUP_HOME");if(!rustup.empty())profile.environment.emplace_back("RUSTUP_HOME",rustup);
        const auto user=env("USERPROFILE");if(!user.empty()){profile.environment.emplace_back("USERPROFILE",user);profile.environment.emplace_back("HOME",user);profile.environment.emplace_back("CARGO_HOME",user+"/.cargo");}
        LanguageServerToolRegistry registry(workspace,{profile});
        require(registry.definitions().size()==1&&registry.definitions()[0].name=="language_server","Agent catalogue must expose one configured read-only language-server tool");
        const auto request=[&](const char* method){return nlohmann::json::parse(registry.invoke(nlohmann::json{{"server_id",profile.id},{"method",method},{"path",argv[3]}}.dump()));};
        const auto symbols=request("documentSymbol");
        require(symbols["result"].is_array()&&symbols["result"].size()==2,"Real rust-analyzer must return the two declarations in the workspace source file");
        require(symbols["result"][0]["name"]=="add"&&symbols["result"][1]["name"]=="call_add","Real rust-analyzer symbols must match the Rust fixture");
        const auto repeated=request("documentSymbol");
        require(repeated["result"]==symbols["result"],"A persistent language-server process must answer repeated workspace requests consistently");
        const auto diagnostics=request("diagnostics");
        require(diagnostics["result"].is_object()&&diagnostics["result"]["items"].is_array()&&diagnostics["result"]["items"].empty(),"Real rust-analyzer must report the valid fixture without diagnostics");
        std::cout<<"Actual rust-analyzer LSP stdio verified persistent workspace initialization, repeated symbols and clean diagnostics against a real Rust project. No model or synthetic LSP peer used\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
