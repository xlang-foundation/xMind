#include "agentflow/vcs_service.hpp"
#include "agentflow/process.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <unordered_set>
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
    // ForegroundProcess supplies the bounded ordinary environment (including
    // PATH) and deliberately rejects overrides of inherited names. Git gets
    // only its isolation controls below; never duplicate host environment keys.
    return {};
}
ProcessResult run_git(const std::string& executable,WorkspaceTools& workspace,std::vector<std::string> arguments,
    std::size_t output_limit=1024*1024,std::chrono::milliseconds timeout=std::chrono::seconds(10)){
    ProcessConfiguration launch;launch.executable=executable;launch.working_directory=workspace.root_path();
    launch.working_directory_id=workspace.directory_identity(".");launch.workspace_root=launch.working_directory;
    launch.workspace_root_id=workspace.identity();launch.arguments={"--no-optional-locks","-c","core.autocrlf=false","-c","core.fsmonitor=false","-c","core.quotepath=false","-c","diff.external=","--no-pager"};
    launch.arguments.insert(launch.arguments.end(),std::make_move_iterator(arguments.begin()),std::make_move_iterator(arguments.end()));
    launch.environment=git_environment();launch.environment.emplace_back("GIT_OPTIONAL_LOCKS","0");launch.environment.emplace_back("GIT_TERMINAL_PROMPT","0");launch.environment.emplace_back("GIT_CONFIG_NOSYSTEM","1");launch.environment.emplace_back("GIT_CONFIG_GLOBAL","NUL");launch.timeout=timeout;launch.output_limit=output_limit;
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
std::string resolve_commit(const std::string& executable,WorkspaceTools& workspace,std::string_view reference,
    std::chrono::steady_clock::time_point deadline){
    if(reference.empty()||reference.size()>1024||reference.find_first_of("\r\n")!=std::string_view::npos||reference.find('\0')!=std::string_view::npos)
        throw VcsStatusFailure(400,"Invalid VCS base reference");
    const std::string expression=std::string(reference)+"^{commit}";
    const auto result=run_git_until(executable,workspace,{"rev-parse","--verify","--quiet","--end-of-options",expression},deadline,8192);
    if(result.termination!=ProcessTermination::exited||result.truncated)throw VcsStatusFailure(503,"Git could not resolve the requested base");
    if(result.exit_code!=0)throw VcsStatusFailure(400,"VCS base does not resolve to a commit");
    auto commit=result.stdout_bytes;while(!commit.empty()&&(commit.back()=='\r'||commit.back()=='\n'))commit.pop_back();
    if(commit.size()<40||commit.size()>64||commit.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)throw VcsStatusFailure(503,"Git returned an invalid commit identity");
    std::transform(commit.begin(),commit.end(),commit.begin(),[](unsigned char c){return c>='A'&&c<='F'?static_cast<char>(c-'A'+'a'):static_cast<char>(c);});return commit;
}
std::optional<VcsReviewBase> workspace_vcs_base_impl(WorkspaceTools& workspace){
    if(!has_git_marker(workspace))return {};
    const auto executable=utf8(git_executable());const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    const auto info=workspace_vcs_info_impl(workspace);if(info.current_branch.empty())return {};
    if(!info.default_branch.empty()&&info.current_branch==info.default_branch){
        const auto remote="refs/remotes/origin/"+info.default_branch;try{return VcsReviewBase{info.default_branch,resolve_commit(executable,workspace,remote,deadline),"default"};}
        catch(const VcsStatusFailure& error){if(error.http_status()!=400)throw;}
    }
    const auto log=run_git_until(executable,workspace,{"reflog","show","--format=%gs%x00%H","refs/heads/"+info.current_branch},deadline,256*1024);
    if(log.termination!=ProcessTermination::exited||log.truncated)throw VcsStatusFailure(503,"Git could not inspect branch creation history");
    if(log.exit_code!=0)return {};
    std::size_t start=0;while(start<log.stdout_bytes.size()){
        const auto end=log.stdout_bytes.find('\n',start),stop=end==std::string::npos?log.stdout_bytes.size():end;auto record=std::string_view(log.stdout_bytes).substr(start,stop-start);if(!record.empty()&&record.back()=='\r')record.remove_suffix(1);
        const auto separator=record.find('\0');if(separator!=std::string_view::npos&&record.size()-separator-1>=40){const auto message=record.substr(0,separator),commit=record.substr(separator+1);const auto created=message.find("branch: Created from ");
            if(created!=std::string_view::npos){constexpr std::string_view prefix="branch: Created from ";const auto name=message.substr(created+prefix.size());if(!name.empty()&&name.size()<=1024)return VcsReviewBase{std::string(name),resolve_commit(executable,workspace,commit,deadline),"reflog"};}}
        start=end==std::string::npos?log.stdout_bytes.size():end+1;
    }
    return {};
}
std::string decode_git_path(std::string_view token){
    if(token.empty()||token.front()!='"')return std::string(token);if(token.size()<2||token.back()!='"')throw VcsStatusFailure(503,"Git returned a malformed quoted path");
    std::string result;for(std::size_t i=1;i+1<token.size();++i){const char c=token[i];if(c!='\\'){result.push_back(c);continue;}if(++i+1>token.size())throw VcsStatusFailure(503,"Git returned a malformed quoted path");const char escaped=token[i];
        if(escaped>='0'&&escaped<='7'){unsigned byte=static_cast<unsigned>(escaped-'0');for(int digit=0;digit<2&&i+1<token.size()-1&&token[i+1]>='0'&&token[i+1]<='7';++digit)byte=byte*8+static_cast<unsigned>(token[++i]-'0');if(byte>255)throw VcsStatusFailure(503,"Git returned an invalid quoted path");result.push_back(static_cast<char>(byte));}
        else switch(escaped){case 'a':result.push_back('\a');break;case 'b':result.push_back('\b');break;case 't':result.push_back('\t');break;case 'n':result.push_back('\n');break;case 'v':result.push_back('\v');break;case 'f':result.push_back('\f');break;case 'r':result.push_back('\r');break;default:result.push_back(escaped);break;}}
    return result;
}
std::string patch_path(std::string_view patch){
    std::size_t start=0;while(start<patch.size()){
        const auto end=patch.find('\n',start),stop=end==std::string_view::npos?patch.size():end;const auto line=patch.substr(start,stop-start);
        if(line.starts_with("+++ ")||line.starts_with("--- ")){auto value=line.substr(4);if(const auto metadata=value.find('\t');metadata!=std::string_view::npos)value=value.substr(0,metadata);const auto token=decode_git_path(value);if(token!="/dev/null"){if(token.starts_with("a/")||token.starts_with("b/"))return token.substr(2);return token;}}
        start=end==std::string_view::npos?patch.size():end+1;
    }
    if(patch.starts_with("diff --git ")){
        auto header=patch.substr(11);const auto end=header.find('\n');if(end!=std::string_view::npos)header=header.substr(0,end);
        std::string_view first,second;if(!header.empty()&&header.front()=='"'){
            if(header.size()<2)throw VcsStatusFailure(503,"Git returned a malformed diff header");std::size_t at=1;bool escaped=false;for(;at<header.size();++at){if(!escaped&&header[at]=='"')break;if(!escaped&&header[at]=='\\')escaped=true;else escaped=false;}if(at>=header.size())throw VcsStatusFailure(503,"Git returned a malformed diff header");first=header.substr(0,at+1);auto rest=header.substr(at+1);while(!rest.empty()&&rest.front()==' ')rest.remove_prefix(1);second=rest;
        }else{const auto separator=header.find(" b/");if(separator==std::string_view::npos)throw VcsStatusFailure(503,"Git returned an invalid diff header");first=header.substr(0,separator);second=header.substr(separator+1);}
        (void)first;auto file=decode_git_path(second);if(file.starts_with("b/"))file.erase(0,2);if(file.empty()||file=="/dev/null")throw VcsStatusFailure(503,"Git returned an invalid diff path");return file;
    }
    throw VcsStatusFailure(503,"Git returned a diff without a file path");
}
std::vector<std::string_view> split_patches(std::string_view patch){
    std::vector<std::size_t> starts;for(std::size_t at=0;at<patch.size();){const auto found=patch.find("diff --git ",at);if(found==std::string_view::npos)break;if(found==0||patch[found-1]=='\n')starts.push_back(found);at=found+10;}
    std::vector<std::string_view> result;for(std::size_t i=0;i<starts.size();++i)result.push_back(patch.substr(starts[i],(i+1<starts.size()?starts[i+1]:patch.size())-starts[i]));return result;
}
std::vector<VcsFileStatus> parse_diff_status(std::string_view output){
    std::vector<VcsFileStatus> result;std::size_t start=0;while(start<output.size()){
        const auto first=output.find('\0',start);if(first==std::string_view::npos)throw VcsStatusFailure(503,"Git returned incomplete diff names");const auto code=output.substr(start,first-start);start=first+1;
        if(code.size()!=1||std::string_view("ADM").find(code.front())==std::string_view::npos||start>=output.size())throw VcsStatusFailure(503,"Git returned invalid diff status");const auto end=output.find('\0',start);if(end==std::string_view::npos)throw VcsStatusFailure(503,"Git returned incomplete diff paths");const auto file=output.substr(start,end-start);start=end+1;
        if(file.empty()||file.size()>32768||result.size()>=4096)throw VcsStatusFailure(503,"Workspace diff exceeds its file limit");result.push_back({std::string(file),0,0,code=="A"?"added":code=="D"?"deleted":"modified"});}
    return result;
}
std::unordered_map<std::string,std::pair<std::uint64_t,std::uint64_t>> parse_diff_stats(std::string_view output){
    std::unordered_map<std::string,std::pair<std::uint64_t,std::uint64_t>> result;std::size_t start=0;while(start<output.size()){
        const auto end=output.find('\0',start);if(end==std::string_view::npos)throw VcsStatusFailure(503,"Git returned incomplete diff statistics");const auto record=output.substr(start,end-start);const auto first=record.find('\t'),second=first==std::string_view::npos?first:record.find('\t',first+1);
        if(first==std::string_view::npos||second==std::string_view::npos)throw VcsStatusFailure(503,"Git returned invalid diff statistics");const auto file=record.substr(second+1);if(file.empty()||file.size()>32768||result.size()>=4096)throw VcsStatusFailure(503,"Workspace diff exceeds its file limit");result.emplace(std::string(file),parse_numstat(record));start=end+1;}
    return result;
}
std::vector<VcsFileDiff> workspace_vcs_diff_impl(WorkspaceTools& workspace,std::string_view mode,std::optional<std::string> base,unsigned context){
    if(mode!="working"&&mode!="branch"&&mode!="committed")throw VcsStatusFailure(400,"VCS diff mode must be working, branch, or committed");
    if(context>1000)throw VcsStatusFailure(400,"VCS diff context must be between 0 and 1000 lines");
    if(!has_git_marker(workspace))return {};
    const auto executable=utf8(git_executable());const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    std::string head;try{head=resolve_commit(executable,workspace,"HEAD",deadline);}catch(const VcsStatusFailure& error){if(error.http_status()!=400||mode!="working")throw;}
    if(mode=="committed"&&head.empty())return {};
    std::string comparison;
    if(mode!="working"){
        std::string ref;if(base)ref=resolve_commit(executable,workspace,*base,deadline);else{const auto inferred=workspace_vcs_base_impl(workspace);if(!inferred)throw VcsStatusFailure(400,"Git cannot infer a review base for this branch; provide an explicit base");ref=inferred->ref;}
        const auto merged=run_git_until(executable,workspace,{"merge-base",ref,head},deadline,8192);if(merged.termination!=ProcessTermination::exited||merged.truncated)throw VcsStatusFailure(503,"Git could not compute the review merge base");if(merged.exit_code==1)return {};if(merged.exit_code!=0)throw VcsStatusFailure(400,"Git base has no common history with the opened branch");comparison=merged.stdout_bytes;while(!comparison.empty()&&(comparison.back()=='\r'||comparison.back()=='\n'))comparison.pop_back();
    }
    std::vector<VcsFileStatus> files;std::unordered_map<std::string,std::pair<std::uint64_t,std::uint64_t>> stats;std::string patch_text;
    if(head.empty()){
        const auto pending=workspace_vcs_status_impl(workspace);std::vector<VcsFileDiff> initial_files;std::size_t total=0;
        for(const auto& item:pending){if(item.status=="deleted")continue;auto patch=run_git_until(executable,workspace,{"diff","--no-index","--binary","--no-ext-diff","--no-textconv","--no-color","--unified="+std::to_string(context),"--","NUL",item.file},deadline,1024*1024);
            if(patch.termination!=ProcessTermination::exited||patch.truncated||patch.exit_code>1)throw VcsStatusFailure(503,"Git could not build the initial workspace diff");if(patch.exit_code!=1||patch.stdout_bytes.empty())continue;total+=patch.stdout_bytes.size();if(total>4*1024*1024)throw VcsStatusFailure(503,"Workspace diff exceeds the response limit");auto file=item;file.status="added";initial_files.push_back({file.file,std::move(patch.stdout_bytes),file.status,file.additions,file.deletions});}
        return initial_files;
    }
    std::vector<std::string> diff_args{"diff","--no-ext-diff","--no-textconv","--no-renames","--no-color","--binary","--unified="+std::to_string(context)};
    if(mode=="committed"){diff_args.push_back(comparison);diff_args.push_back("HEAD");}else diff_args.push_back(mode=="working"?head:comparison);diff_args.push_back("--");diff_args.push_back(".");
    auto patch=run_git_until(executable,workspace,diff_args,deadline,4*1024*1024);if(patch.termination!=ProcessTermination::exited||patch.exit_code!=0||patch.truncated)throw VcsStatusFailure(503,"Git could not build the workspace diff completely");patch_text=std::move(patch.stdout_bytes);
    std::vector<std::string> data_args{"diff","--no-renames","--no-ext-diff","--no-textconv","--name-status","-z"};std::vector<std::string> stat_args{"diff","--no-renames","--no-ext-diff","--no-textconv","--numstat","-z"};
    if(mode=="committed"){data_args.push_back(comparison);data_args.push_back("HEAD");stat_args.push_back(comparison);stat_args.push_back("HEAD");}else{const auto& ref=mode=="working"?head:comparison;data_args.push_back(ref);stat_args.push_back(ref);}data_args.insert(data_args.end(),{"--","."});stat_args.insert(stat_args.end(),{"--","."});
    const auto named=run_git_until(executable,workspace,data_args,deadline,1024*1024),numbered=run_git_until(executable,workspace,stat_args,deadline,1024*1024);
    if(named.termination!=ProcessTermination::exited||named.exit_code!=0||named.truncated||numbered.termination!=ProcessTermination::exited||numbered.exit_code!=0||numbered.truncated)throw VcsStatusFailure(503,"Git could not index workspace diff metadata");
    files=parse_diff_status(named.stdout_bytes);stats=parse_diff_stats(numbered.stdout_bytes);
    std::unordered_map<std::string,std::string> patches;for(const auto chunk:split_patches(patch_text)){const auto file=patch_path(chunk);if(file.size()>32768||patches.size()>=4096)throw VcsStatusFailure(503,"Workspace diff exceeds its file limit");patches.emplace(file,std::string(chunk));}
    std::vector<VcsFileDiff> result;std::unordered_set<std::string> included;std::size_t total=0;
    for(const auto& file:files){const auto found=patches.find(file.file);if(found==patches.end())throw VcsStatusFailure(503,"Git omitted a changed file patch");const auto count=stats.find(file.file);auto text=found->second;total+=text.size();if(total>4*1024*1024)throw VcsStatusFailure(503,"Workspace diff exceeds the response limit");result.push_back({file.file,std::move(text),file.status,count==stats.end()?0:count->second.first,count==stats.end()?0:count->second.second});included.insert(file.file);}
    if(mode!="committed")for(const auto& item:workspace_vcs_status_impl(workspace))if(item.status=="added"&&!included.contains(item.file)){
        const auto untracked=run_git_until(executable,workspace,{"diff","--no-index","--binary","--no-ext-diff","--no-textconv","--no-color","--unified="+std::to_string(context),"--","NUL",item.file},deadline,1024*1024);
        if(untracked.termination!=ProcessTermination::exited||untracked.truncated||untracked.exit_code>1)throw VcsStatusFailure(503,"Git could not build an untracked file patch");if(untracked.exit_code!=1||untracked.stdout_bytes.empty())continue;total+=untracked.stdout_bytes.size();if(total>4*1024*1024)throw VcsStatusFailure(503,"Workspace diff exceeds the response limit");result.push_back({item.file,std::move(untracked.stdout_bytes),"added",item.additions,item.deletions});}
    std::sort(result.begin(),result.end(),[](const auto& left,const auto& right){return left.file<right.file;});return result;
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
std::optional<VcsReviewBase> workspace_vcs_base(WorkspaceTools& workspace){return workspace_vcs_base_impl(workspace);}
std::vector<VcsFileDiff> workspace_vcs_diff(WorkspaceTools& workspace,std::string_view mode,std::optional<std::string> base,unsigned context){return workspace_vcs_diff_impl(workspace,mode,std::move(base),context);}

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
    launch.arguments={"init"};launch.environment=git_environment();launch.environment.emplace_back("GIT_CONFIG_NOSYSTEM","1");launch.environment.emplace_back("GIT_CONFIG_GLOBAL","NUL");launch.timeout=std::chrono::seconds(30);launch.output_limit=8192;
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
