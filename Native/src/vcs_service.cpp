#include "agentflow/vcs_service.hpp"
#include "agentflow/process.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <unordered_map>
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
ProcessResult run_git(const std::string& executable,WorkspaceTools& workspace,std::vector<std::string> arguments,
    std::size_t output_limit=1024*1024,std::chrono::milliseconds timeout=std::chrono::seconds(10)){
    ProcessConfiguration launch;launch.executable=executable;launch.working_directory=workspace.root_path();
    launch.working_directory_id=workspace.directory_identity(".");launch.workspace_root=launch.working_directory;
    launch.workspace_root_id=workspace.identity();launch.arguments={"--no-optional-locks","-c","core.autocrlf=false","-c","core.fsmonitor=false","-c","core.quotepath=false","-c","diff.external=","--no-pager"};
    launch.arguments.insert(launch.arguments.end(),std::make_move_iterator(arguments.begin()),std::make_move_iterator(arguments.end()));
    launch.environment=git_environment();launch.environment.emplace_back("GIT_OPTIONAL_LOCKS","0");launch.environment.emplace_back("GIT_TERMINAL_PROMPT","0");launch.environment.emplace_back("GIT_CONFIG_NOSYSTEM","1");launch.timeout=timeout;launch.output_limit=output_limit;
    try{return ForegroundProcess::run(launch);}
    catch(const ProcessBeforeDispatchError&){throw VcsStatusFailure(503,"Git status could not be started safely");}
    catch(const ProcessEffectUncertain&){throw VcsStatusFailure(503,"Git status was interrupted; refresh repository state before retrying");}
}
ProcessResult run_git_until(const std::string& executable,WorkspaceTools& workspace,std::vector<std::string> arguments,
    std::chrono::steady_clock::time_point deadline,std::size_t output_limit=1024*1024){
    const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
    if(remaining<=std::chrono::milliseconds::zero())throw VcsStatusFailure(503,"Git status exceeded its 30-second operation limit");
    return run_git(executable,workspace,std::move(arguments),output_limit,std::min(remaining,std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(10))));
}
std::pair<std::uint64_t,std::uint64_t> parse_numstat(std::string_view output){
    const auto first=output.find('\t'),second=first==std::string_view::npos?first:output.find('\t',first+1);
    if(first==std::string_view::npos||second==std::string_view::npos)throw VcsStatusFailure(503,"Git returned an invalid file statistic");
    const auto count=[](std::string_view value){
        if(value=="-")return std::uint64_t{0};if(value.empty())throw VcsStatusFailure(503,"Git returned an invalid line count");
        std::uint64_t result=0;for(const char digit:value){if(digit<'0'||digit>'9'||result>(UINT64_MAX-static_cast<unsigned>(digit-'0'))/10)throw VcsStatusFailure(503,"Git returned an invalid line count");result=result*10+static_cast<unsigned>(digit-'0');}return result;
    };
    return {count(output.substr(0,first)),count(output.substr(first+1,second-first-1))};
}
std::unordered_map<std::string,std::pair<std::uint64_t,std::uint64_t>> tracked_stats(const std::string& executable,WorkspaceTools& workspace,
    std::chrono::steady_clock::time_point deadline){
    const auto head=run_git_until(executable,workspace,{"rev-parse","--verify","--quiet","HEAD"},deadline,8192);
    if(head.termination!=ProcessTermination::exited||head.truncated)throw VcsStatusFailure(503,"Git could not inspect repository history");
    if(head.exit_code==1)return {};
    if(head.exit_code!=0)throw VcsStatusFailure(503,"Git could not inspect repository history");
    const auto result=run_git_until(executable,workspace,{"diff","--numstat","--no-renames","--no-ext-diff","--no-textconv","-z","HEAD","--","."},deadline);
    if(result.termination!=ProcessTermination::exited||result.exit_code!=0||result.truncated)throw VcsStatusFailure(503,"Git could not compute workspace change statistics");
    std::unordered_map<std::string,std::pair<std::uint64_t,std::uint64_t>> stats;std::size_t start=0;
    while(start<result.stdout_bytes.size()){
        const auto end=result.stdout_bytes.find('\0',start);if(end==std::string::npos)throw VcsStatusFailure(503,"Git returned incomplete workspace statistics");
        const auto record=std::string_view(result.stdout_bytes).substr(start,end-start);const auto first=record.find('\t'),second=first==std::string_view::npos?first:record.find('\t',first+1);
        if(first==std::string_view::npos||second==std::string_view::npos||second+1>=record.size())throw VcsStatusFailure(503,"Git returned invalid workspace statistics");
        const auto [additions,deletions]=parse_numstat(record);const std::string file(record.substr(second+1));
        if(file.size()>32768||stats.size()>=4096)throw VcsStatusFailure(503,"Workspace changes exceed the status limit");stats.emplace(file,std::pair{additions,deletions});start=end+1;
    }
    return stats;
}
std::string change_kind(std::string_view code){
    if(code=="??")return "added";if(code.find('U')!=std::string_view::npos)return "modified";
    const bool added=code.find('A')!=std::string_view::npos,deleted=code.find('D')!=std::string_view::npos;
    if(added&&!deleted)return "added";if(deleted&&!added)return "deleted";return "modified";
}
std::vector<VcsFileStatus> workspace_vcs_status_impl(WorkspaceTools& workspace){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    const auto listing=workspace.list_files(".");
    if(listing.truncated)throw VcsStatusFailure(503,"Cannot verify the repository marker in a truncated workspace root");
    const auto marker=std::find_if(listing.entries.begin(),listing.entries.end(),[](const WorkspaceEntry& entry){auto name=entry.name;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):static_cast<char>(c);});return name==".git";});
    if(marker==listing.entries.end())return {};
    if(marker->kind!="directory"&&marker->kind!="file")throw VcsStatusFailure(409,"Git metadata must be a regular file or directory");
    const auto executable=utf8(git_executable());
    const auto status=run_git_until(executable,workspace,{"status","--porcelain=v1","--untracked-files=all","--no-renames","-z","--","."},deadline);
    if(status.termination!=ProcessTermination::exited||status.exit_code!=0||status.truncated)throw VcsStatusFailure(503,"Git could not read workspace status completely");
    const auto stats=tracked_stats(executable,workspace,deadline);std::vector<VcsFileStatus> result;std::size_t start=0;
    while(start<status.stdout_bytes.size()){
        const auto end=status.stdout_bytes.find('\0',start);if(end==std::string::npos)throw VcsStatusFailure(503,"Git returned incomplete workspace status");
        const auto record=std::string_view(status.stdout_bytes).substr(start,end-start);if(record.size()<4||record[2]!=' ')throw VcsStatusFailure(503,"Git returned invalid workspace status");
        std::string file(record.substr(3));if(file.empty()||file.size()>32768||result.size()>=4096)throw VcsStatusFailure(503,"Workspace changes exceed the status limit");
        const auto kind=change_kind(record.substr(0,2));auto found=stats.find(file);std::pair<std::uint64_t,std::uint64_t> counts{};
        if(found!=stats.end())counts=found->second;
        else if(kind=="added"){
            const auto untracked=run_git_until(executable,workspace,{"diff","--no-index","--numstat","--no-renames","--no-ext-diff","--no-textconv","--","NUL",file},deadline,8192);
            if(untracked.termination!=ProcessTermination::exited||untracked.truncated||untracked.exit_code>1)throw VcsStatusFailure(503,"Git could not count a new workspace file");
            if(untracked.exit_code==1){const auto first=untracked.stdout_bytes.find('\t'),second=first==std::string::npos?first:untracked.stdout_bytes.find('\t',first+1);if(first!=std::string::npos&&second!=std::string::npos)counts=parse_numstat(untracked.stdout_bytes);}
        }
        result.push_back({std::move(file),counts.first,counts.second,kind});start=end+1;
    }
    std::sort(result.begin(),result.end(),[](const auto& left,const auto& right){return left.file<right.file;});
    return result;
}
bool has_git_marker(WorkspaceTools& workspace){
    const auto listing=workspace.list_files(".");
    if(listing.truncated)throw VcsStatusFailure(503,"Cannot verify the repository marker in a truncated workspace root");
    const auto marker=std::find_if(listing.entries.begin(),listing.entries.end(),[](const WorkspaceEntry& entry){auto name=entry.name;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;});return name==".git";});
    if(marker==listing.entries.end())return false;
    if(marker->kind!="directory"&&marker->kind!="file")throw VcsStatusFailure(409,"Git metadata must be a regular file or directory");
    return true;
}
std::optional<std::string> symbolic_ref(const std::string& executable,WorkspaceTools& workspace,const std::string& name){
    const auto result=run_git(executable,workspace,{"symbolic-ref","--quiet","--short",name},8192);
    if(result.termination!=ProcessTermination::exited||result.truncated)throw VcsStatusFailure(503,"Git could not inspect branch metadata");
    if(result.exit_code==1)return {};
    if(result.exit_code!=0)throw VcsStatusFailure(503,"Git could not inspect branch metadata");
    auto value=result.stdout_bytes;while(!value.empty()&&(value.back()=='\r'||value.back()=='\n'))value.pop_back();
    if(value.empty()||value.size()>1024||value.find_first_of("\r\n")!=std::string::npos||value.find('\0')!=std::string::npos)throw VcsStatusFailure(503,"Git returned invalid branch metadata");
    return value;
}
VcsInfo workspace_vcs_info_impl(WorkspaceTools& workspace){
    VcsInfo info;if(!has_git_marker(workspace))return info;info.repository=true;
    const auto executable=utf8(git_executable());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    const auto head=run_git_until(executable,workspace,{"symbolic-ref","--quiet","--short","HEAD"},deadline,8192);
    if(head.termination!=ProcessTermination::exited||head.truncated)throw VcsStatusFailure(503,"Git could not inspect the current branch");
    if(head.exit_code==0){auto value=head.stdout_bytes;while(!value.empty()&&(value.back()=='\r'||value.back()=='\n'))value.pop_back();if(value.empty()||value.size()>1024||value.find_first_of("\r\n")!=std::string::npos||value.find('\0')!=std::string::npos)throw VcsStatusFailure(503,"Git returned an invalid current branch");info.current_branch=std::move(value);}
    else if(head.exit_code!=1)throw VcsStatusFailure(503,"Git could not inspect the current branch");
    if(const auto remote=symbolic_ref(executable,workspace,"refs/remotes/origin/HEAD")){const auto slash=remote->find('/');if(slash!=std::string::npos&&slash+1<remote->size())info.default_branch=remote->substr(slash+1);}
    const auto branches=run_git_until(executable,workspace,{"for-each-ref","--format=%(refname:short)","refs/heads"},deadline,256*1024);
    if(branches.termination!=ProcessTermination::exited||branches.exit_code!=0||branches.truncated)throw VcsStatusFailure(503,"Git could not list local branches");
    std::size_t start=0;while(start<branches.stdout_bytes.size()){
        const auto end=branches.stdout_bytes.find('\n',start);const auto stop=end==std::string::npos?branches.stdout_bytes.size():end;
        auto name=std::string_view(branches.stdout_bytes).substr(start,stop-start);if(!name.empty()&&name.back()=='\r')name.remove_suffix(1);
        if(name.empty()||name.size()>1024||info.local_branches.size()>=4096||name.find('\0')!=std::string_view::npos)throw VcsStatusFailure(503,"Git returned an invalid local branch list");
        info.local_branches.emplace_back(name);start=end==std::string::npos?branches.stdout_bytes.size():end+1;
    }
    return info;
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

std::vector<VcsFileStatus> workspace_vcs_status(WorkspaceTools& workspace){return workspace_vcs_status_impl(workspace);}
VcsInfo workspace_vcs_info(WorkspaceTools& workspace){return workspace_vcs_info_impl(workspace);}

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
