#include "agentflow/vcs_service.hpp"
#include "agentflow/process.hpp"
#include <algorithm>
#include <filesystem>
#include <optional>
#include <vector>
#define NOMINMAX
#include <windows.h>

namespace agentflow {
namespace {
std::optional<std::wstring> environment_value(const wchar_t* name){
    const auto size=GetEnvironmentVariableW(name,nullptr,0);if(!size)return {};
    if(size>32768)throw VcsInitializationFailure(503,"Git environment configuration exceeds limits");
    std::wstring value(size,L'\0');const auto used=GetEnvironmentVariableW(name,value.data(),size);
    if(!used||used>=size)throw VcsInitializationFailure(503,"Git environment configuration changed during lookup");
    value.resize(used);return value;
}
std::string utf8(const std::wstring& value){
    if(value.empty())return {};const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(size<=0)throw VcsInitializationFailure(503,"Git configuration contains invalid Unicode");
    std::string result(static_cast<std::size_t>(size),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr)!=size)
        throw VcsInitializationFailure(503,"Git configuration contains invalid Unicode");
    return result;
}
std::string utf8(const std::filesystem::path& value){const auto encoded=value.u8string();return {reinterpret_cast<const char*>(encoded.data()),encoded.size()};}
std::filesystem::path git_executable(){
    const auto path=environment_value(L"PATH");if(!path)throw VcsInitializationFailure(503,"Git is not available on this machine");
    std::size_t start=0;
    while(start<=path->size()){
        const auto end=path->find(L';',start);auto component=path->substr(start,end==std::wstring::npos?std::wstring::npos:end-start);start=end==std::wstring::npos?path->size()+1:end+1;
        if(component.size()>=2&&component.front()==L'"'&&component.back()==L'"')component=component.substr(1,component.size()-2);
        if(component.empty())continue;
        const std::filesystem::path directory(component);if(!directory.is_absolute())continue;
        const auto candidate=(directory/L"git.exe").lexically_normal();
        try{
            if(!std::filesystem::is_regular_file(candidate))continue;
            const auto canonical=std::filesystem::canonical(candidate);
            if(!std::filesystem::is_regular_file(canonical))continue;
            (void)ForegroundProcess::executable_identity(utf8(canonical));
            return canonical;
        }catch(const VcsInitializationFailure&){throw;}
        catch(...){continue;}
    }
    throw VcsInitializationFailure(503,"Git is not available on this machine");
}
std::vector<std::pair<std::string,std::string>> git_environment(){
    std::vector<std::pair<std::string,std::string>> result;
    for(const auto& item:std::initializer_list<std::pair<const wchar_t*,const char*>>{{L"PATH","PATH"},{L"USERPROFILE","USERPROFILE"},{L"APPDATA","APPDATA"}})
        if(const auto value=environment_value(item.first))result.emplace_back(item.second,utf8(*value));
    if(const auto profile=environment_value(L"USERPROFILE"))result.emplace_back("HOME",utf8(*profile));
    return result;
}
void ensure_no_vcs(const WorkspaceListing& entries){
    if(entries.truncated)throw VcsInitializationFailure(503,"Cannot verify repository state in a truncated workspace root");
    const auto found=std::find_if(entries.entries.begin(),entries.entries.end(),[](const WorkspaceEntry& entry){
        auto name=entry.name;while(!name.empty()&&(name.back()=='.'||name.back()==' '))name.pop_back();
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):static_cast<char>(c);});
        return name==".git"||name==".hg"||name==".svn"||name==".bzr";
    });
    if(found!=entries.entries.end())throw VcsInitializationFailure(409,"Project already has version control");
}
}

void initialize_workspace_vcs(WorkspaceTools& workspace,std::string_view provider){
    if(provider.empty()||provider.size()>64)throw VcsInitializationFailure(400,"Invalid version control provider");
    if(provider!="git"){
        if(provider=="hg"||provider=="mercurial")throw VcsInitializationFailure(501,"Mercurial initialization is not supported");
        throw VcsInitializationFailure(400,"Unknown version control provider");
    }
    const auto before=workspace.list_files(".");ensure_no_vcs(before);
    const auto root=workspace.root_path(),root_id=workspace.identity();
    ProcessConfiguration launch;launch.executable=utf8(git_executable());launch.working_directory=root;
    launch.working_directory_id=workspace.directory_identity(".");launch.workspace_root=root;launch.workspace_root_id=root_id;
    launch.arguments={"init"};launch.environment=git_environment();launch.timeout=std::chrono::seconds(30);launch.output_limit=8192;
    ProcessResult result;
    try{result=ForegroundProcess::run(launch);}
    catch(const ProcessBeforeDispatchError&){throw VcsInitializationFailure(503,"Git initialization could not be started safely");}
    catch(const ProcessEffectUncertain&){throw VcsInitializationFailure(503,"Git initialization was interrupted; inspect the repository before retrying");}
    if(result.termination!=ProcessTermination::exited||result.exit_code!=0)
        throw VcsInitializationFailure(503,"Git reported that repository initialization failed");
    const auto after=workspace.list_files(".");
    if(after.truncated||std::none_of(after.entries.begin(),after.entries.end(),[](const WorkspaceEntry& entry){return entry.name==".git"&&entry.kind=="directory";}))
        throw VcsInitializationFailure(503,"Git exited without creating a verifiable repository marker");
}
}
