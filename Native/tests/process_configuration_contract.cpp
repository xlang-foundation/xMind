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
void module_paths_contract(const std::filesystem::path& folder,const std::vector<std::string>& imports,Json profile){
    const auto one=folder/"module one",two=folder/"module-two";std::filesystem::create_directory(one);std::filesystem::create_directory(two);
    profile["id"]="module-policy";profile["powershell_module_paths"]=Json::array({one.string(),two.string()});const auto paths=profile["powershell_module_paths"];const auto database=(folder/"module-policy.sqlite").string();
    const auto desired=[&]{return Json{{"profiles",Json::array({profile})}}.dump();};
    {
        PersistenceService store(database,imports);ProcessConfigurationStore registry(store);require(registry.apply(desired()).front().revision==1,"Explicit module paths must be accepted by the trusted profile registry");
        const auto saved=store.information("native-process","profiles").get();require(Json::parse(saved)["profiles"][0]["powershell_module_paths"]==paths,"Exact module search order must persist through actual xlang3 SQLite");
        for(const auto& invalid:std::vector<Json>{nullptr,Json::object(),"not-an-array",Json::array({"relative"}),Json::array({(folder/"missing").string()}),Json::array({one.string()+";other"}),Json::array({one.string(),one.string()}),Json::array({std::string("bad\0path",8)})}){
            auto bad=profile;bad["powershell_module_paths"]=invalid;rejects<std::invalid_argument>([&]{registry.apply(Json{{"profiles",Json::array({bad})}}.dump());});require(store.information("native-process","profiles").get()==saved,"Rejected module policies must preserve the actual saved profile");
        }
        auto bad=profile;bad["powershell_module_paths"]=Json::array();for(int i=0;i<17;++i)bad["powershell_module_paths"].push_back(one.string());rejects<std::invalid_argument>([&]{registry.apply(Json{{"profiles",Json::array({bad})}}.dump());});
        require(registry.apply(desired()).front().revision==1,"Identical module paths must preserve their revision");
        profile["powershell_module_paths"]=Json::array({two.string(),one.string()});require(registry.apply(desired()).front().revision==2,"Changing module search order must rotate the profile revision");
        profile.erase("powershell_module_paths");require(registry.apply(desired()).front().revision==3,"Restoring ordinary module discovery must rotate the revision");
        profile["powershell_module_paths"]=paths;require(registry.apply(desired()).front().revision==4,"Restored explicit module paths must receive a new revision");store.close();
    }
    {PersistenceService store(database,imports);ProcessConfigurationStore registry(store);require(registry.load().front().revision==4&&Json::parse(store.information("native-process","profiles").get())["profiles"][0]["powershell_module_paths"]==paths,"Exact module paths and revision must survive SQLite close/reopen");store.close();}
}
}
int main(int argc,char** argv){if(argc!=4)return 2;try{
    Directory folder;const auto executable=folder.path/"actual-native-copy.exe",workspace=folder.path/"workspace";std::filesystem::copy_file(std::filesystem::u8path(argv[1]),executable);std::filesystem::create_directory(workspace);
    const auto database=(folder.path/"state.sqlite").string();const std::vector<std::string> imports{argv[2],argv[3]};
    Json p{{"id","fixture"},{"executable",executable.string()},{"prefix_arguments",Json::array({"literal fixture argument"})},{"max_timeout_ms",120000},{"auto_approve",true}};
    module_paths_contract(folder.path,imports,p);
    const auto desired=[&]{return Json{{"profiles",Json::array({p})}}.dump();};
    const auto yaml=[&]{return std::string("profiles:\n  - id: fixture\n    executable: ")+Json(executable.string()).dump()+"\n    prefix_arguments:\n      - literal fixture argument\n    max_timeout_ms: 120000\n    auto_approve: true\n";};std::string binding;
    {
        PersistenceService store(database,imports);ProcessConfigurationStore configurations(store);require(configurations.load().empty(),"Absent registry must remain empty until the host explicitly initializes its built-in profiles");
        const auto first=configurations.load_or_initialize(yaml);require(first.size()==1&&first[0].auto_approve,"First-run trusted shell initialization must not require approval before each command");binding=first[0].executable_id;require(first[0].revision==1 && binding==ForegroundProcess::executable_identity(executable.string()),"Initialization must bind an actual native file and assign revision");
        require(configurations.load_or_initialize([]()->std::string{throw std::runtime_error("Defaults must remain unevaluated when a registry is saved");})[0].revision==1,"A saved custom profile registry must never be replaced by built-in defaults");
        require(configurations.apply(desired())[0].revision==1,"JSON and YAML imports of identical metadata/bytes must preserve revision");const auto before=store.information("native-process","profiles").get();
        for(const auto* field:{"revision","executable_id","environment","credentials"}){auto invalid=p;invalid[field]="untrusted fixture value";rejects<std::invalid_argument>([&]{configurations.apply(Json{{"profiles",Json::array({invalid})}}.dump());});}
        auto relative=p;relative["executable"]="relative.exe";rejects<std::invalid_argument>([&]{configurations.apply(Json{{"profiles",Json::array({relative})}}.dump());});
        rejects<std::invalid_argument>([&]{configurations.apply(Json{{"profiles",Json::array({p,p})}}.dump());});rejects<std::invalid_argument>([&]{configurations.apply(R"({"profiles":[],"profiles":[]})");});
        rejects<std::invalid_argument>([&]{configurations.apply("profiles:\n  - id: duplicate\n    id: second\n");});
        require(store.information("native-process","profiles").get()==before,"Every rejected replacement must preserve exact committed configuration");
        p["max_timeout_ms"]=10000;const auto changed=configurations.apply(desired());require(changed[0].revision==2 && changed[0].executable_id==binding,"Policy change rotates revision without inventing binary change");
        {std::ofstream mutation(executable,std::ios::binary|std::ios::app);mutation<<"actual fixture byte change";require(bool(mutation),"Actual executable fixture mutation required");}
        WorkspaceTools boundary(workspace.string());rejects<ProcessBeforeDispatchError>([&]{ProcessExecutor executor(store,boundary,workspace.string(),configurations.load());});
        const auto rebound=configurations.apply(desired());require(rebound[0].revision==3 && rebound[0].executable_id!=binding,"Explicit reimport must detect actual byte change and rotate binding/revision");binding=rebound[0].executable_id;store.close();
    }
    {
        PersistenceService store(database,imports);ProcessConfigurationStore configurations(store);const auto restored=configurations.load();require(restored.size()==1 && restored[0].revision==3 && restored[0].executable_id==binding,"Actual binding and metadata must survive native store reopen");
        configurations.apply(R"({"profiles":[]})");rejects<Conflict>([&]{configurations.apply(desired());});require(configurations.load().empty(),"Retired profile IDs must not be reused");require(configurations.load_or_initialize([]()->std::string{throw std::runtime_error("Defaults must remain unevaluated for an explicit empty registry");}).empty(),"An explicitly stored empty profile list must not be repopulated on startup");store.close();
    }
    std::cout<<"Native process configuration passed actual executable binding/hash changes, embedded-xlang3 storage/reopen, backend revisions, rejected replacement preservation, spoof/plain environment/credential/duplicate rejection and permanent profile retirement. No child/model/UI activation claimed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
