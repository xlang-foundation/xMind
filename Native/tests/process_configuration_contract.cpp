#include "agentflow/process_configuration.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
using namespace agentflow;
namespace {
using Json=nlohmann::json;
void require(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
template<class E,class F>void rejects(F f){try{f();}catch(const E&){return;}throw std::runtime_error("Expected process configuration rejection did not occur");}
struct Directory {
    std::filesystem::path parent=std::filesystem::temp_directory_path(),path=parent/("xmind-process-config-"+std::to_string(std::random_device{}()));
    Directory(){require(std::filesystem::create_directory(path),"Fixture directory must be new");}
    ~Directory(){if(path.parent_path()==parent && path.filename().string().starts_with("xmind-process-config-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
}
int main(int argc,char** argv){if(argc!=4)return 2;try{
    Directory folder;const auto executable=folder.path/"actual-native-copy.exe",workspace=folder.path/"workspace";std::filesystem::copy_file(std::filesystem::u8path(argv[1]),executable);std::filesystem::create_directory(workspace);
    const auto database=(folder.path/"state.sqlite").string();const std::vector<std::string> imports{argv[2],argv[3]};
    Json p{{"id","fixture"},{"executable",executable.string()},{"prefix_arguments",Json::array({"literal fixture argument"})},{"max_timeout_ms",120000}};
    const auto desired=[&]{return Json{{"profiles",Json::array({p})}}.dump();};std::string binding;
    {
        PersistenceService store(database,imports);ProcessConfigurationStore configurations(store);require(configurations.load().empty(),"Absent registry must not invent executable profiles");
        const auto first=configurations.apply(desired());require(first.size()==1,"Import must create exactly one profile");binding=first[0].executable_id;require(first[0].revision==1 && binding==ForegroundProcess::executable_identity(executable.string()),"Import must bind an actual native file and assign revision");
        require(configurations.apply(desired())[0].revision==1,"Identical metadata/bytes must preserve revision");const auto before=store.information("native-process","profiles").get();
        for(const auto* field:{"revision","executable_id","environment","credentials"}){auto invalid=p;invalid[field]="untrusted fixture value";rejects<std::invalid_argument>([&]{configurations.apply(Json{{"profiles",Json::array({invalid})}}.dump());});}
        auto relative=p;relative["executable"]="relative.exe";rejects<std::invalid_argument>([&]{configurations.apply(Json{{"profiles",Json::array({relative})}}.dump());});
        rejects<std::invalid_argument>([&]{configurations.apply(Json{{"profiles",Json::array({p,p})}}.dump());});rejects<std::invalid_argument>([&]{configurations.apply(R"({"profiles":[],"profiles":[]})");});
        require(store.information("native-process","profiles").get()==before,"Every rejected replacement must preserve exact committed configuration");
        p["max_timeout_ms"]=10000;const auto changed=configurations.apply(desired());require(changed[0].revision==2 && changed[0].executable_id==binding,"Policy change rotates revision without inventing binary change");
        {std::ofstream mutation(executable,std::ios::binary|std::ios::app);mutation<<"actual fixture byte change";require(bool(mutation),"Actual executable fixture mutation required");}
        WorkspaceTools boundary(workspace.string());rejects<ProcessBeforeDispatchError>([&]{ProcessExecutor executor(store,boundary,workspace.string(),configurations.load());});
        const auto rebound=configurations.apply(desired());require(rebound[0].revision==3 && rebound[0].executable_id!=binding,"Explicit reimport must detect actual byte change and rotate binding/revision");binding=rebound[0].executable_id;store.close();
    }
    {
        PersistenceService store(database,imports);ProcessConfigurationStore configurations(store);const auto restored=configurations.load();require(restored.size()==1 && restored[0].revision==3 && restored[0].executable_id==binding,"Actual binding and metadata must survive native store reopen");
        configurations.apply(R"({"profiles":[]})");rejects<Conflict>([&]{configurations.apply(desired());});require(configurations.load().empty(),"Retired profile IDs must not be reused");store.close();
    }
    std::cout<<"Native process configuration passed actual executable binding/hash changes, embedded-xlang3 storage/reopen, backend revisions, rejected replacement preservation, spoof/plain environment/credential/duplicate rejection and permanent profile retirement. No child/model/UI activation claimed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
