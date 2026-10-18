#include "agentflow/workspace_registry.hpp"
#include "agentflow/workspace_tools.hpp"
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
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

        // Private roots can arrive from TEMP, profile configuration or a DOS
        // short path. Compare their resolved directory, not their spelling.
        for(const auto& storage_alias:{storage_path+"\\.",storage_path+"\\private\\.."}){
            WorkspaceRegistry aliased(fixture.database,roots,storage_alias);
            for(const auto& forbidden:{storage_path,private_path,utf8(fixture.root)}){
                bool add_rejected=false;try{(void)aliased.add_folder(created.workspace_id,forbidden);}catch(const std::invalid_argument&){add_rejected=true;}
                require(add_rejected,"An aliased private root, child or ancestor must not become a workspace member");
                bool create_rejected=false;try{(void)aliased.create(forbidden);}catch(const std::invalid_argument&){create_rejected=true;}
                require(create_rejected,"An aliased private root, child or ancestor must not become a workspace primary");
                require(aliased.get(created.workspace_id).revision==2&&aliased.list().size()==1,"Rejected private aliases must not mutate membership or create workspaces");
            }
        }
        const auto private_config=fixture.root/L".xMind";
        fs::create_directories(private_config);
        WorkspaceRegistry configured(fixture.database,roots,utf8(private_config/L"."));
        require(configured.create(primary_path).workspace_id==created.workspace_id,"A backend-private profile root must resolve without granting it workspace authority");
        WorkspaceRegistry drive_private(fixture.database,roots,utf8(fixture.root.root_path()));
        bool drive_rejected=false;try{(void)drive_private.create(primary_path);}catch(const std::invalid_argument&){drive_rejected=true;}
        require(drive_rejected,"A volume-root private boundary must reject its descendants");
        const auto private_file=fixture.root/L"not-a-directory";
        {std::ofstream file(private_file);file<<"fixture";}
        for(const auto& invalid:{utf8(fixture.root/L"absent"),utf8(private_file),storage_path+std::string(1,'\0')+"suffix"}){
            bool invalid_rejected=false;try{WorkspaceRegistry bad(fixture.database,roots,invalid);}catch(const std::invalid_argument&){invalid_rejected=true;}
            require(invalid_rejected,"Missing, file and NUL private storage paths must fail before registry access");
        }

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
        {
            Fixture unicode_fixture;
            const auto unicode_storage=unicode_fixture.root/L"\u5bc6\u94a5",unicode_private=unicode_storage/L"private";
            fs::create_directories(unicode_private);
            WorkspaceRegistry unicode_registry(utf8(unicode_storage/L"workspaces.sqlite"),roots,utf8(unicode_storage/L"."));
            const auto unicode_group=unicode_registry.create(utf8(unicode_fixture.first));
            bool unicode_rejected=false;try{(void)unicode_registry.add_folder(unicode_group.workspace_id,utf8(unicode_private));}catch(const std::invalid_argument&){unicode_rejected=true;}
            require(unicode_rejected&&unicode_registry.get(unicode_group.workspace_id).revision==1,"Unicode private roots must reject children without membership changes");
            require(unicode_registry.add_folder(unicode_group.workspace_id,utf8(unicode_fixture.second)).revision==2,"Unicode private boundaries must still allow unrelated public folders");
        }
        std::cout<<"Native xlang3 workspace registry contracts passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
