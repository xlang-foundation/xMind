#include "agentflow/workspace_registry.hpp"
#include "agentflow/workspace_tools.hpp"
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace agentflow;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string utf8(const fs::path& value){const auto text=value.u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
struct Fixture {
    fs::path root=fs::temp_directory_path()/(L"xmind-workspace-registry-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    fs::path storage=root/L"profile",first=root/L"ProjectA",second=root/L"ProjectB",third=root/L"ProjectC",fourth=root/L"ProjectD",nested=first/L"nested",private_root=storage/L"private";
    std::string database;
    Fixture(){fs::create_directories(storage);fs::create_directories(first);fs::create_directories(second);fs::create_directories(third);fs::create_directories(fourth);fs::create_directories(nested);fs::create_directories(private_root);database=utf8(storage/L"workspaces.sqlite");}
    ~Fixture(){std::error_code ignored;fs::remove_all(root,ignored);}
};
}
int main(int argc,char** argv){
    if(argc!=3){std::cerr<<"Expected xlang3 module and pure-library directories\n";return 2;}
    try{
        Fixture fixture;const std::vector<std::string> roots{argv[1],argv[2]};
        const auto primary_path=utf8(fixture.first),second_path=utf8(fixture.second),nested_path=utf8(fixture.nested),private_path=utf8(fixture.private_root);
        const auto storage_path=utf8(fixture.storage);
        WorkspaceRegistry registry(fixture.database,roots,storage_path);
        const auto created=registry.create(primary_path);
        require(created.workspace_id==WorkspaceTools(primary_path).identity(),"Workspace id must be anchored to its primary native folder identity");
        require(created.name=="ProjectA"&&created.revision==1,"A new workspace must start with one primary folder at revision one");
        require(created.folders.size()==1&&created.primary_folder_id==created.folders[0].folder_id&&created.folders[0].ordinal==0,"Primary folder must be first and explicit");
        require(created.folders[0].identity==WorkspaceTools(primary_path).identity(),"Folder identity must come from the verified native directory handle");

        const auto attached=registry.add_folder(created.workspace_id,second_path);
        require(attached.workspace_id==created.workspace_id,"Attaching a folder must not create a new workspace");
        require(attached.revision==2&&attached.folders.size()==2,"Folder attachment must advance one membership revision");
        require(attached.folders[0].folder_id==created.primary_folder_id&&attached.folders[1].ordinal==1,"Primary ordering must remain stable");
        require(attached.folders[0].identity!=attached.folders[1].identity,"Two native directories must keep distinct folder identities");

        const auto repeated_create=registry.create(primary_path,"Ignored duplicate name");
        require(repeated_create.workspace_id==created.workspace_id&&repeated_create.revision==2&&repeated_create.name==created.name,"Reopening the same primary folder must preserve the registered workspace");
        bool duplicate_rejected=false;try{(void)registry.add_folder(created.workspace_id,second_path);}catch(const std::invalid_argument&){duplicate_rejected=true;}
        require(duplicate_rejected&&registry.get(created.workspace_id).revision==2,"Duplicate folder attachment must have no membership effect");
        bool overlap_rejected=false;try{(void)registry.add_folder(created.workspace_id,nested_path);}catch(const std::invalid_argument&){overlap_rejected=true;}
        require(overlap_rejected&&registry.get(created.workspace_id).revision==2,"Nested roots must be rejected before membership changes");
        bool private_rejected=false;try{(void)registry.add_folder(created.workspace_id,private_path);}catch(const std::invalid_argument&){private_rejected=true;}
        require(private_rejected&&registry.get(created.workspace_id).revision==2,"Private xMind storage must not become a workspace member");

        WorkspaceRegistry reopened(fixture.database,roots,storage_path);
        const auto groups=reopened.list();
        require(groups.size()==1&&groups.front().workspace_id==created.workspace_id&&groups.front().revision==2&&groups.front().folders.size()==2,"Workspace membership must reopen from xlang3 SQLite");
        std::exception_ptr first_failure,second_failure;
        std::thread first_attach([&]{try{(void)reopened.add_folder(created.workspace_id,utf8(fixture.third));}catch(...){first_failure=std::current_exception();}});
        std::thread second_attach([&]{try{(void)reopened.add_folder(created.workspace_id,utf8(fixture.fourth));}catch(...){second_failure=std::current_exception();}});
        first_attach.join();second_attach.join();
        if(first_failure)std::rethrow_exception(first_failure);if(second_failure)std::rethrow_exception(second_failure);
        const auto concurrent=reopened.get(created.workspace_id);
        require(concurrent.revision==4&&concurrent.folders.size()==4,"Concurrent attachments must serialize membership revisions without losing folders");
        for(std::size_t i=0;i<concurrent.folders.size();++i)require(concurrent.folders[i].ordinal==static_cast<std::int64_t>(i),"Concurrent folder ordering must remain gap-free");
        bool missing_rejected=false;try{(void)reopened.add_folder("windows-local-file-v1:missing",second_path);}catch(const std::out_of_range&){missing_rejected=true;}
        require(missing_rejected&&reopened.get(created.workspace_id).revision==4,"Unknown workspace IDs must not acquire folders");
        std::cout<<"Native xlang3 workspace registry contracts passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
