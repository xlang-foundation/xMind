#include "agentflow/workspace_tools.hpp"
#include "agentflow/path_glob.hpp"
#include "agentflow/path_ignore.hpp"
#include "agentflow/search_pattern.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <winternl.h>
#include <algorithm>
#include <array>
#include <deque>
#include <filesystem>
#include <set>
#include <iomanip>
#include <sstream>
#include <functional>

namespace agentflow {
namespace {
void check_cancel(std::stop_token cancel) {if(cancel.stop_requested()) throw ToolCancelled("Workspace operation cancelled");}
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE input):value(input) {if(value==INVALID_HANDLE_VALUE) throw ToolFileError("Workspace file or directory unavailable (code "+std::to_string(GetLastError())+")");}
    ~Handle() {CloseHandle(value);}
    Handle(const Handle&)=delete;
};
std::wstring wide(const std::string& text) {
    if(text.empty() || text.size()>32768 || text.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid workspace path or text");
    const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(!size) throw std::invalid_argument("Invalid UTF-8 workspace input");
    std::wstring result(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),size);return result;
}
std::string utf8(const std::wstring& text) {
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(!size && !text.empty()) throw ToolFileError("Invalid Unicode workspace name");
    std::string result(size,'\0');if(size) WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr);return result;
}
std::wstring final_path(HANDLE handle) {
    const auto size=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!size) throw ToolFileError("Cannot verify workspace handle path");
    std::wstring result(size,L'\0');const auto written=GetFinalPathNameByHandleW(handle,result.data(),size,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!written || written>=size) throw ToolFileError("Workspace handle path changed");result.resize(written);return result;
}
// Final normalized paths use strict casing; case-distinct sibling directories
// on a case-sensitive NTFS parent must never be treated as the authorized root.
bool equal(const std::wstring& a,const std::wstring& b) {return CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),FALSE)==CSTR_EQUAL;}
std::string relative_path(const std::string& input) {
    wide(input);
    if(input.find(':')!=std::string::npos || input.starts_with('/') || input.starts_with('\\')) throw ToolAccessDenied("Use a relative workspace path");
    const auto path=std::filesystem::u8path(input);
    for(const auto& part:path){
        if(part==L"..")throw ToolAccessDenied("Parent traversal is outside the tool contract");
        if(WorkspaceTools::backend_private_component(utf8(part.native())))throw ToolAccessDenied("Backend configuration is outside workspace tool authority");
    }
    const auto normalized=path.lexically_normal().generic_u8string();
    return {reinterpret_cast<const char*>(normalized.data()),normalized.size()};
}
bool valid_text(const std::string& value) {
    return value.find('\0')==std::string::npos && (value.empty() || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0)>0);
}
std::string prefix(std::string_view value,std::size_t limit) {
    if(value.size()<=limit) return std::string(value);
    auto size=limit;while(size && (static_cast<unsigned char>(value[size])&0xc0)==0x80) --size;
    return std::string(value.substr(0,size));
}
std::string file_identity(HANDLE handle) {
    FILE_ID_INFO info{};
    if(!GetFileInformationByHandleEx(handle,FileIdInfo,&info,sizeof(info))) throw ToolFileError("Filesystem does not expose a file identity");
    bool nonzero=false;for(auto byte:info.FileId.Identifier) nonzero|=byte!=0;
    if(!nonzero) throw ToolFileError("Filesystem returned an unsupported file identity");
    std::ostringstream identifier;identifier<<"windows-local-file-v1:"<<std::hex<<std::setfill('0')<<std::setw(16)<<info.VolumeSerialNumber<<":";
    for(auto byte:info.FileId.Identifier) identifier<<std::setw(2)<<static_cast<unsigned>(byte);
    return identifier.str();
}
std::string content_hash(const std::string& content) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<UCHAR,32> hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw ToolFileError("Cannot capture content hash");
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(content.data())),static_cast<ULONG>(content.size()),hash.data(),static_cast<ULONG>(hash.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0) throw ToolFileError("Cannot capture content hash");
    std::ostringstream result;result<<std::hex<<std::setfill('0');for(auto byte:hash) result<<std::setw(2)<<static_cast<unsigned>(byte);return result.str();
}
using NativeCreate=decltype(&NtCreateFile);
using NativeError=ULONG(NTAPI*)(NTSTATUS);
struct NativeFiles {
    NativeCreate create;
    NativeError error;
    NativeFiles(){const auto module=GetModuleHandleW(L"ntdll.dll");create=reinterpret_cast<NativeCreate>(GetProcAddress(module,"NtCreateFile"));error=reinterpret_cast<NativeError>(GetProcAddress(module,"RtlNtStatusToDosError"));if(!create || !error)throw ToolFileError("Handle-relative file operations are unavailable");}
};
// One literal component relative to the already opened parent. Never parses a
// model-supplied absolute NT/DOS namespace and never follows final reparses.
HANDLE relative_file(HANDLE parent,std::wstring name,bool directory,bool creating,bool absent_ok=false,bool read_content=false){
    static const NativeFiles api;
    if(name.empty() || name.size()>32767 || name.find_first_of(L"\\/:")!=std::wstring::npos)throw std::invalid_argument("Invalid relative file component");
    UNICODE_STRING text{};text.Buffer=name.data();text.Length=static_cast<USHORT>(name.size()*sizeof(wchar_t));text.MaximumLength=text.Length;
    OBJECT_ATTRIBUTES attributes{};attributes.Length=sizeof(attributes);attributes.RootDirectory=parent;attributes.ObjectName=&text;attributes.Attributes=OBJ_CASE_INSENSITIVE;
    IO_STATUS_BLOCK outcome{};HANDLE file=nullptr;
    const auto access=creating?(GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE):(FILE_READ_ATTRIBUTES|SYNCHRONIZE|(directory?FILE_LIST_DIRECTORY|FILE_TRAVERSE:0)|(read_content?FILE_READ_DATA:0));
    const auto flags=FILE_SYNCHRONOUS_IO_NONALERT|FILE_OPEN_REPARSE_POINT|(directory?FILE_DIRECTORY_FILE:0)|(creating?FILE_WRITE_THROUGH|(directory?0:FILE_NON_DIRECTORY_FILE):0);
    const auto status=api.create(&file,access,&attributes,&outcome,nullptr,FILE_ATTRIBUTE_NORMAL,creating?0:FILE_SHARE_READ,creating?FILE_CREATE:FILE_OPEN,flags,nullptr,0);
    if(status<0){const auto code=api.error(status);if(file && file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        if(!creating && absent_ok && code==ERROR_FILE_NOT_FOUND)return nullptr;
        if(creating && (code==ERROR_FILE_EXISTS || code==ERROR_ALREADY_EXISTS))throw ToolContentConflict("Creation target already exists");
        if(creating)throw ToolMutationUncertain("File creation outcome was not established");
        throw ToolFileError("Cannot open verified creation parent or inspect target");
    }
    if(!file || file==INVALID_HANDLE_VALUE){if(creating)throw ToolMutationUncertain("Native creation returned no verified handle");throw ToolFileError("Native inspection returned no verified handle");}
    if(creating && outcome.Information!=FILE_CREATED){CloseHandle(file);throw ToolMutationUncertain("Native creation did not report a newly created file");}
    return file;
}
void creation_component(const std::wstring& name){
    if(name.empty() || name==L"." || name==L".." || name.back()==L'.' || name.back()==L' ')throw ToolAccessDenied("Use an unambiguous file component");
    for(const auto character:name)if(character<32 || std::wstring_view(L"<>:\"/\\|?*").find(character)!=std::wstring_view::npos)throw ToolAccessDenied("Invalid creation file component");
    auto stem=name.substr(0,name.find(L'.'));for(auto& character:stem)if(character>=L'a' && character<=L'z')character-=L'a'-L'A';
    if(stem==L"CON" || stem==L"PRN" || stem==L"AUX" || stem==L"NUL" || (stem.size()==4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) && stem[3]>=L'1' && stem[3]<=L'9'))throw ToolAccessDenied("Device names are not creation targets");
}
}
struct WorkspaceTools::Impl {
    Handle root;
    std::wstring base;
    explicit Impl(const std::string& path):root(CreateFileW(wide(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr)),base(final_path(root.value)) {
        const auto attrs=GetFileAttributesW(base.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_DIRECTORY)) throw std::invalid_argument("Workspace root must be a directory");
        while(!base.empty() && base.back()==L'\\') base.pop_back();
        for(const auto& part:std::filesystem::path(base))if(WorkspaceTools::backend_private_component(utf8(part.native())))throw ToolAccessDenied("Backend configuration cannot be a model workspace");
    }
    void verify(HANDLE handle) const {
        const auto path=final_path(handle);
        if(!equal(path,base) && !(path.size()>base.size() && path[base.size()]==L'\\' && equal(path.substr(0,base.size()),base)))
            throw ToolAccessDenied("Resolved file is outside the workspace");
        if(path.size()>base.size())for(const auto& part:std::filesystem::path(path.substr(base.size()+1)))
            if(WorkspaceTools::backend_private_component(utf8(part.native())))throw ToolAccessDenied("Resolved backend configuration is outside workspace tool authority");
    }
    std::wstring path(const std::string& relative) const {
        if(relative==".") return base;
        auto suffix=wide(relative);std::replace(suffix.begin(),suffix.end(),L'/',L'\\');
        return base+L"\\"+suffix;
    }
    std::optional<std::string> ignore_content(HANDLE directory,const std::wstring& leaf,std::stop_token cancel) const {
        check_cancel(cancel);const auto opened=relative_file(directory,leaf,false,false,true,true);if(!opened)return {};
        Handle file(opened);verify(opened);BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(opened,&info)||GetFileType(opened)!=FILE_TYPE_DISK||(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw ToolFileError("Ignore metadata is not a regular file");
        if((info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||info.nNumberOfLinks!=1)throw ToolAccessDenied("Ignore metadata cannot use links");
        if(info.nFileSizeHigh||info.nFileSizeLow>32768)throw ToolFileError("Ignore metadata exceeds the per-file limit");
        std::string content;std::array<char,8192> buffer{};
        for(;;){check_cancel(cancel);DWORD count=0;if(!ReadFile(opened,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr))throw ToolFileError("Cannot read ignore metadata");if(!count)break;if(count>32768-content.size())throw ToolFileError("Ignore metadata exceeds the per-file limit");content.append(buffer.data(),count);}
        if(!valid_text(content))throw ToolFileError("Ignore metadata must be UTF-8 text without NUL");verify(opened);return content;
    }
    void load_ignores(HANDLE directory,const std::string& scope,PathIgnore& rules,std::stop_token cancel) const {
        check_cancel(cancel);const auto marker=relative_file(directory,L".git",false,false,true);
        if(marker){
            Handle git(marker);verify(marker);BY_HANDLE_FILE_INFORMATION info{};
            if(!GetFileInformationByHandle(marker,&info))throw ToolFileError("Cannot inspect repository marker");
            if((info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&info.nNumberOfLinks!=1))throw ToolAccessDenied("Repository marker cannot use links");
            rules.begin_repository(scope);
            if(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){Handle git_directory(relative_file(directory,L".git",true,false));verify_creation_directory(git_directory.value);if(file_identity(git_directory.value)!=file_identity(marker))throw ToolAccessDenied("Repository marker changed");const auto opened=relative_file(git_directory.value,L"info",true,false,true);
                if(opened){Handle directory_info(opened);verify_creation_directory(opened);if(auto content=ignore_content(opened,L"exclude",cancel))rules.add(scope,-1,*content);}
            }
        }
        if(rules.in_repository())if(auto content=ignore_content(directory,L".gitignore",cancel))rules.add(scope,0,*content);
        if(auto content=ignore_content(directory,L".ignore",cancel))rules.add(scope,1,*content);
        if(auto content=ignore_content(directory,L".rgignore",cancel))rules.add(scope,2,*content);
    }
    struct CreationParent {std::vector<std::unique_ptr<Handle>> directories;std::vector<std::string> missing;std::wstring leaf;std::string relative,existing_directory=".";};
    void verify_creation_directory(HANDLE directory) const {verify(directory);BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(directory,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw ToolFileError("Creation parent is not a directory");if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)throw ToolAccessDenied("Creation does not traverse directory links");}
    CreationParent creation_parent(const std::string& input,std::stop_token cancel,bool allow_missing=false) const {
        check_cancel(cancel);CreationParent result;result.relative=relative_path(input);
        std::vector<std::wstring> components;for(const auto& part:std::filesystem::u8path(result.relative)){const auto name=part.wstring();creation_component(name);components.push_back(name);}
        if(components.empty())throw ToolAccessDenied("A creation target must name a file");result.leaf=components.back();
        if(allow_missing&&components.size()>33)throw ToolAccessDenied("Creation supports at most 32 parent directories");
        result.directories.push_back(std::make_unique<Handle>(CreateFileW(base.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)));
        if(file_identity(result.directories.back()->value)!=file_identity(root.value))throw ToolAccessDenied("Creation root identity changed");
        verify_creation_directory(result.directories.back()->value);std::string prefix;
        for(std::size_t i=0;i+1<components.size();++i){check_cancel(cancel);if(!prefix.empty())prefix+='/';prefix+=utf8(components[i]);
            if(!result.missing.empty()){result.missing.push_back(prefix);continue;}
            const auto opened=relative_file(result.directories.back()->value,components[i],true,false,allow_missing);
            if(!opened){result.missing.push_back(prefix);continue;}
            result.directories.push_back(std::make_unique<Handle>(opened));verify_creation_directory(opened);result.existing_directory=prefix;
        }
        return result;
    }
};
WorkspaceTools::WorkspaceTools(const std::string& root):impl_(std::make_unique<Impl>(root)) {}
WorkspaceTools::~WorkspaceTools()=default;
std::string WorkspaceTools::creation_directory(const std::string& path,std::stop_token cancel) const {return impl_->creation_parent(path,cancel,true).existing_directory;}
WorkspaceCreatePlan WorkspaceTools::plan_creation(const std::string& path,const std::string& content,std::stop_token cancel,bool create_parents) const {
    check_cancel(cancel);if(content.size()>1024*1024 || !valid_text(content))throw std::invalid_argument("Creation content must be bounded UTF-8 text");
    const auto workspace=identity();auto parent=impl_->creation_parent(path,cancel,create_parents);const auto directory=parent.directories.back()->value;
    if(parent.missing.empty()){const auto existing=relative_file(directory,parent.leaf,false,false,true);if(existing){Handle owned(existing);throw ToolContentConflict("Creation target already exists");}}
    check_cancel(cancel);if(identity()!=workspace)throw ToolAccessDenied("Workspace changed while planning creation");
    return {parent.relative,workspace,file_identity(directory),content,content_hash(content),std::move(parent.missing)};
}
WorkspaceSnapshot WorkspaceTools::apply_creation(const WorkspaceCreatePlan& plan,std::stop_token cancel) const {
    check_cancel(cancel);if(plan.content.size()>1024*1024 || !valid_text(plan.content) || content_hash(plan.content)!=plan.content_sha256)throw std::invalid_argument("Invalid creation plan content or hash");
    if(identity()!=plan.workspace_id)throw ToolAccessDenied("Creation belongs to another workspace");
    auto parent=impl_->creation_parent(plan.path,cancel,!plan.create_directories.empty());const auto anchor=parent.directories.back()->value;
    if(file_identity(anchor)!=plan.parent_id||parent.missing!=plan.create_directories)throw ToolContentConflict("Creation parent identity or absence changed");
    check_cancel(cancel);
    bool mutated=false;
    try {
        for(const auto& missing:parent.missing){check_cancel(cancel);const auto leaf=std::filesystem::u8path(missing).filename().wstring();
            const auto created=relative_file(parent.directories.back()->value,leaf,true,true);mutated=true;
            parent.directories.push_back(std::make_unique<Handle>(created));impl_->verify_creation_directory(created);
        }
        check_cancel(cancel);const auto directory=parent.directories.back()->value;
        Handle file(relative_file(directory,parent.leaf,false,true));mutated=true; // Atomic create-new; never open/overwrite.
        impl_->verify(file.value);BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(file.value,&info) || GetFileType(file.value)!=FILE_TYPE_DISK || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || info.nNumberOfLinks!=1)throw ToolFileError("Created file identity is not regular and exclusive");
        std::size_t offset=0;while(offset<plan.content.size()){check_cancel(cancel);DWORD count=0;const auto amount=static_cast<DWORD>(std::min<std::size_t>(8192,plan.content.size()-offset));if(!WriteFile(file.value,plan.content.data()+offset,amount,&count,nullptr) || !count || count>amount)throw ToolFileError("Cannot write created file");offset+=count;}
        check_cancel(cancel);if(!FlushFileBuffers(file.value))throw ToolFileError("Cannot finalize created file");
        LARGE_INTEGER zero{};if(!SetFilePointerEx(file.value,zero,nullptr,FILE_BEGIN))throw ToolFileError("Cannot inspect created file");
        std::string actual;std::array<char,8192> buffer{};for(;;){DWORD count=0;if(!ReadFile(file.value,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr))throw ToolFileError("Cannot read created file");if(!count)break;if(count>1024*1024-actual.size())throw ToolFileError("Created file exceeds limits");actual.append(buffer.data(),count);}
        impl_->verify(file.value);const auto hash=content_hash(actual),id=file_identity(file.value);if(actual!=plan.content || hash!=plan.content_sha256 || identity()!=plan.workspace_id || file_identity(anchor)!=plan.parent_id)throw ToolFileError("Created file readback differs from plan");
        return {parent.relative,std::move(actual),plan.workspace_id,id,hash};
    }catch(...){if(mutated)throw ToolMutationUncertain("File or directory creation occurred; its final state requires reconciliation");throw;}
}
std::string WorkspaceTools::identity() const {
    auto current=final_path(impl_->root.value);
    while(!current.empty() && current.back()==L'\\') current.pop_back();
    if(!equal(current,impl_->base)) throw ToolAccessDenied("Workspace root identity changed");
    return file_identity(impl_->root.value);
}
std::string WorkspaceTools::root_path() const {
    const auto before=identity();auto path=final_path(impl_->root.value);
    while(!path.empty()&&path.back()==L'\\')path.pop_back();
    if(!equal(path,impl_->base)||identity()!=before)throw ToolAccessDenied("Workspace root identity changed");
    if(path.starts_with(L"\\\\?\\UNC\\"))path=L"\\\\"+path.substr(8);
    else if(path.starts_with(L"\\\\?\\"))path.erase(0,4);
    if(path.size()==2&&path[1]==L':')path+=L'\\';
    return utf8(path);
}
std::string WorkspaceTools::directory_identity(const std::string& input,std::stop_token cancel) const {
    check_cancel(cancel);const auto relative=relative_path(input);const auto workspace=identity();
    Handle directory(CreateFileW(impl_->path(relative).c_str(),FILE_READ_ATTRIBUTES|FILE_TRAVERSE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    impl_->verify(directory.value);BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(directory.value,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw ToolFileError("Expected a workspace directory");
    if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)throw ToolAccessDenied("Directory links are not traversed");
    check_cancel(cancel);if(identity()!=workspace)throw ToolAccessDenied("Workspace changed during directory admission");return file_identity(directory.value);
}
WorkspaceFile WorkspaceTools::read_file(const std::string& input,std::stop_token cancel) const {
    auto result=read_snapshot(input,false,cancel);return {std::move(result.path),std::move(result.content)};
}
WorkspaceFilePage WorkspaceTools::read_file_page(const std::string& input,std::size_t offset,std::size_t limit,std::stop_token cancel) const {
    constexpr std::size_t scan_limit=64*1024*1024,page_limit=50*1024,line_limit=2000;
    if(!offset||offset>scan_limit||!limit||limit>2000)throw std::invalid_argument("Invalid read page bounds");
    check_cancel(cancel);const auto relative=relative_path(input),workspace=identity();
    Handle file(CreateFileW(impl_->path(relative).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    impl_->verify(file.value);BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.value,&info)||GetFileType(file.value)!=FILE_TYPE_DISK||(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw ToolFileError("Expected a regular workspace file");
    if(info.nNumberOfLinks!=1)throw ToolAccessDenied("Hard-linked files require a separate explicit policy");
    if(info.nFileSizeHigh||info.nFileSizeLow>scan_limit)throw ToolFileError("File exceeds the 64 MiB paged text limit");
    // FILE_SHARE_READ excludes concurrent writers/deletion while this verified
    // handle is used. UTF-8 validation spans OS read buffers and skipped/clipped
    // lines; no byte cut can turn a partial code point into valid output.
    const auto json_cost=[](std::string_view text){std::size_t size=0;for(const unsigned char byte:text)size+=byte<32?6:(byte=='"'||byte=='\\'?2:1);return size;};
    WorkspaceFilePage result;result.path=relative;result.offset=offset;
    std::size_t budget=256+json_cost(relative),line=1,characters=0,scanned=0;
    std::string text,sequence;bool line_present=false,clipped=false,done=false;
    unsigned remaining=0;std::uint32_t code=0,minimum=0;
    const auto finish_line=[&](bool newline){
        if(remaining)throw ToolFileError("File is binary or not UTF-8 text");
        if(line>=offset){
            const auto cost=json_cost(text)+(newline?6:0)+24;
            if(result.lines_read>=limit||budget+cost>page_limit){
                if(!result.lines_read)throw ToolFileError("Read page cannot fit within the output limit");
                result.next_offset=line;return true;
            }
            result.content+=text;if(newline)result.content+='\n';
            ++result.lines_read;budget+=cost;if(clipped)result.truncated_lines.push_back(line);
        }
        ++line;text.clear();characters=0;line_present=false;clipped=false;return false;
    };
    std::array<char,8192> buffer{};
    while(!done){
        check_cancel(cancel);DWORD count=0;
        if(!ReadFile(file.value,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr))throw ToolFileError("Cannot read workspace file");
        if(!count){if(remaining)throw ToolFileError("File is binary or not UTF-8 text");if(line_present)finish_line(false);break;}
        if(count>scan_limit-scanned)throw ToolFileError("File exceeds the 64 MiB paged text limit");scanned+=count;
        for(DWORD i=0;i<count;++i){
            const auto byte=static_cast<unsigned char>(buffer[i]);
            if(!remaining&&byte=='\n'){if(finish_line(true)){done=true;break;}continue;}
            line_present=true;
            if(!remaining){
                sequence.clear();sequence+=static_cast<char>(byte);
                if(byte>0&&byte<0x80){code=byte;minimum=0;}
                else if(byte>=0xc2&&byte<=0xdf){remaining=1;code=byte&0x1f;minimum=0x80;}
                else if(byte>=0xe0&&byte<=0xef){remaining=2;code=byte&0x0f;minimum=0x800;}
                else if(byte>=0xf0&&byte<=0xf4){remaining=3;code=byte&0x07;minimum=0x10000;}
                else throw ToolFileError("File is binary or not UTF-8 text");
            }else{
                if((byte&0xc0)!=0x80)throw ToolFileError("File is binary or not UTF-8 text");
                sequence+=static_cast<char>(byte);code=(code<<6)|(byte&0x3f);--remaining;
            }
            if(!remaining){
                if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))throw ToolFileError("File is binary or not UTF-8 text");
                if(line>=offset){if(characters<line_limit)text+=sequence;else clipped=true;}
                ++characters;
            }
        }
    }
    if(!result.lines_read&&offset!=1)throw ToolFileError("Read offset is beyond the end of the file");
    check_cancel(cancel);impl_->verify(file.value);
    if(identity()!=workspace)throw ToolAccessDenied("Workspace identity changed during read page");
    return result;
}
WorkspaceSnapshot WorkspaceTools::snapshot_file(const std::string& input,std::stop_token cancel) const {
    return read_snapshot(input,true,cancel);
}
std::optional<WorkspaceSnapshot> WorkspaceTools::instruction_file(const std::string& input,std::stop_token cancel) const {
    check_cancel(cancel);const auto workspace=identity();auto parent=impl_->creation_parent(input,cancel);
    const auto opened=relative_file(parent.directories.back()->value,parent.leaf,false,false,true,true);
    if(!opened){check_cancel(cancel);if(identity()!=workspace)throw ToolAccessDenied("Workspace changed during guidance discovery");return std::nullopt;}
    Handle file(opened);impl_->verify(file.value);BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.value,&info) || GetFileType(file.value)!=FILE_TYPE_DISK || (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw ToolFileError("Guidance must be a regular file");
    if((info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || info.nNumberOfLinks!=1)throw ToolAccessDenied("Guidance does not follow linked files");
    if(info.nFileSizeHigh || info.nFileSizeLow>16384)throw ToolFileError("Guidance file exceeds 16 KiB");
    std::string content;std::array<char,4096> buffer{};
    for(;;){check_cancel(cancel);DWORD count=0;if(!ReadFile(file.value,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr))throw ToolFileError("Guidance is unreadable");if(!count)break;if(count>16384-content.size())throw ToolFileError("Guidance file exceeds 16 KiB");content.append(buffer.data(),count);}
    if(!valid_text(content))throw ToolFileError("Guidance must be UTF-8 text without NUL");
    check_cancel(cancel);impl_->verify(file.value);if(identity()!=workspace)throw ToolAccessDenied("Workspace changed during guidance snapshot");
    return WorkspaceSnapshot{parent.relative,content,workspace,file_identity(file.value),content_hash(content)};
}
std::optional<std::string> WorkspaceTools::instruction_directory_identity(const std::string& input,std::stop_token cancel) const {
    check_cancel(cancel);const auto relative=relative_path(input),workspace=identity();
    std::vector<std::unique_ptr<Handle>> directories;directories.push_back(std::make_unique<Handle>(CreateFileW(impl_->base.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)));
    auto verify=[&](HANDLE directory){impl_->verify(directory);BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(directory,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw ToolFileError("Instruction directory is not a directory");if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)throw ToolAccessDenied("Instruction directories do not traverse links");};
    verify(directories.back()->value);if(file_identity(directories.back()->value)!=workspace)throw ToolAccessDenied("Instruction directory root changed");
    for(const auto& component:std::filesystem::u8path(relative)){
        if(component==L".")continue;check_cancel(cancel);if(directories.size()>=33)throw ToolFileError("Instruction directory depth exceeds 32");creation_component(component.wstring());
        const auto opened=relative_file(directories.back()->value,component.wstring(),true,false,true);
        if(!opened){check_cancel(cancel);if(identity()!=workspace)throw ToolAccessDenied("Instruction directory root changed");return std::nullopt;}
        directories.push_back(std::make_unique<Handle>(opened));verify(directories.back()->value);
    }
    check_cancel(cancel);if(identity()!=workspace)throw ToolAccessDenied("Instruction directory root changed");return file_identity(directories.back()->value);
}
std::vector<WorkspaceSnapshot> WorkspaceTools::repository_instructions(const std::string& input,std::stop_token cancel) const {
    check_cancel(cancel);const auto directory=relative_path(input);std::vector<std::string> paths{"AGENTS.md"};std::string prefix;
    if(directory!=".")for(const auto& component:std::filesystem::u8path(directory)){
        const auto bytes=component.generic_u8string();const std::string part(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        if(part==".")continue;if(paths.size()>=33)throw std::invalid_argument("Guidance directory depth exceeds 32");
        if(!prefix.empty())prefix+='/';prefix+=part;paths.push_back(prefix+"/AGENTS.md");
    }
    const auto workspace=identity();std::vector<WorkspaceSnapshot> result;std::size_t bytes=0;
    for(const auto& path:paths)if(auto file=instruction_file(path,cancel)){
        if(file->workspace_id!=workspace)throw ToolAccessDenied("Workspace changed during guidance discovery");
        if(file->content.size()>32768-bytes)throw ToolFileError("Scoped guidance exceeds 32 KiB");bytes+=file->content.size();result.push_back(std::move(*file));
    }
    if(identity()!=workspace)throw ToolAccessDenied("Workspace changed during guidance discovery");return result;
}
WorkspaceEditPlan WorkspaceTools::plan_replacement(const std::string& path,const std::string& old_text,const std::string& new_text,std::size_t expected,std::stop_token cancel) const {
    check_cancel(cancel);
    if(old_text.empty() || old_text.size()>1024*1024 || new_text.size()>1024*1024 || expected==0 || expected>1024 || !valid_text(old_text) || !valid_text(new_text))
        throw std::invalid_argument("Invalid edit replacement or occurrence count");
    if(old_text==new_text) throw ToolContentConflict("Replacement makes no change");
    auto before=snapshot_file(path,cancel);
    std::vector<std::size_t> matches;std::size_t start=0;
    for(;;) {
        check_cancel(cancel);const auto found=before.content.find(old_text,start);if(found==std::string::npos) break;
        matches.push_back(found);if(matches.size()>expected) throw ToolContentConflict("Replacement occurrence count differs");
        start=found+old_text.size();
    }
    if(matches.size()!=expected) throw ToolContentConflict("Replacement occurrence count differs");
    const auto removed=matches.size()*old_text.size(),added=matches.size()*new_text.size();
    const auto size=before.content.size()-removed+added;
    if(size>1024*1024) throw ToolFileError("Planned edit exceeds the 1 MiB text limit");
    std::string after;after.reserve(size);start=0;
    for(const auto found:matches) {
        check_cancel(cancel);after.append(before.content,start,found-start);after+=new_text;start=found+old_text.size();
    }
    after.append(before.content,start,std::string::npos);check_cancel(cancel);
    const auto hash=content_hash(after);
    return {std::move(before),std::move(after),hash,matches.size()};
}
WorkspaceFingerprint WorkspaceTools::fingerprint_file(const std::string& path,std::stop_token cancel) const {
    auto value=read_snapshot(path,true,cancel,false);
    return {std::move(value.path),std::move(value.workspace_id),std::move(value.file_id),std::move(value.content_sha256),value.content.size()};
}
WorkspaceSnapshot WorkspaceTools::apply_plan(const WorkspaceEditPlan& plan,std::stop_token cancel) const {
    check_cancel(cancel);
    if(plan.before.content.size()>1024*1024 || plan.after_content.size()>1024*1024 || !valid_text(plan.before.content) || !valid_text(plan.after_content)) throw std::invalid_argument("Invalid edit plan contents");
    if(plan.before.content==plan.after_content) throw ToolContentConflict("Edit makes no change");
    if(content_hash(plan.before.content)!=plan.before.content_sha256 || content_hash(plan.after_content)!=plan.after_sha256) throw ToolContentConflict("Edit plan hashes differ from contents");
    if(identity()!=plan.before.workspace_id) throw ToolAccessDenied("Edit belongs to another workspace");
    const auto relative=relative_path(plan.before.path);
    Handle file(CreateFileW(impl_->path(relative).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
    impl_->verify(file.value);BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.value,&info) || GetFileType(file.value)!=FILE_TYPE_DISK || (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) throw ToolFileError("Expected a regular edit target");
    if((info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || info.nNumberOfLinks!=1) throw ToolAccessDenied("Linked edit targets require a separate policy");
    if(file_identity(file.value)!=plan.before.file_id) throw ToolContentConflict("Edit target identity changed");
    auto read=[&](std::stop_token token) {
        LARGE_INTEGER zero{};if(!SetFilePointerEx(file.value,zero,nullptr,FILE_BEGIN)) throw ToolFileError("Cannot inspect edit target");
        std::string bytes;std::array<char,8192> buffer{};
        for(;;) {
            check_cancel(token);DWORD count=0;
            if(!ReadFile(file.value,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)) throw ToolFileError("Cannot inspect edit target");
            if(!count) break;if(count>1024*1024-bytes.size()) throw ToolFileError("Edit target exceeds its limit");bytes.append(buffer.data(),count);
        }
        return bytes;
    };
    const auto current=read(cancel);
    if(current!=plan.before.content || content_hash(current)!=plan.before.content_sha256) throw ToolContentConflict("Edit target contents changed");
    LARGE_INTEGER zero{};if(!SetFilePointerEx(file.value,zero,nullptr,FILE_BEGIN)) throw ToolFileError("Cannot prepare edit target");
    bool attempted=false;
    try {
        std::size_t offset=0;
        while(offset<plan.after_content.size()) {
            check_cancel(cancel);impl_->verify(file.value);
            if(identity()!=plan.before.workspace_id) throw ToolAccessDenied("Workspace changed during edit");
            const auto amount=static_cast<DWORD>(std::min<std::size_t>(8192,plan.after_content.size()-offset));DWORD written=0;
            attempted=true;
            if(!WriteFile(file.value,plan.after_content.data()+offset,amount,&written,nullptr) || !written || written>amount) throw ToolFileError("Cannot write edit target");
            offset+=written;
        }
        check_cancel(cancel);impl_->verify(file.value);
        if(identity()!=plan.before.workspace_id) throw ToolAccessDenied("Workspace changed during edit");
        attempted=true;
        if(!SetEndOfFile(file.value) || !FlushFileBuffers(file.value)) throw ToolFileError("Cannot finalize edit target");
        // The effect has occurred. Inspect it even if cancellation arrived;
        // reporting pre-effect cancellation here would hide actual changes.
        const auto actual=read({});impl_->verify(file.value);
        const auto hash=content_hash(actual),id=file_identity(file.value);
        if(actual!=plan.after_content || hash!=plan.after_sha256 || id!=plan.before.file_id || identity()!=plan.before.workspace_id) throw ToolFileError("Edit readback differs from the plan");
        return {relative,actual,plan.before.workspace_id,id,hash};
    } catch(...) {
        if(attempted) throw ToolMutationUncertain("Edit may have changed the file; reconciliation is required");
        throw;
    }
}
WorkspaceSnapshot WorkspaceTools::read_snapshot(const std::string& input,bool capture_version,std::stop_token cancel,bool require_text,std::size_t max_bytes,std::size_t* bytes_read) const {
    check_cancel(cancel);const auto relative=relative_path(input);
    const auto workspace_id=capture_version?identity():std::string{};
    Handle file(CreateFileW(impl_->path(relative).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    impl_->verify(file.value);
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.value,&info) || GetFileType(file.value)!=FILE_TYPE_DISK || (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) throw ToolFileError("Expected a regular workspace file");
    if(info.nNumberOfLinks>1) throw ToolAccessDenied("Hard-linked files require a separate explicit policy");
    if(info.nFileSizeHigh || info.nFileSizeLow>max_bytes) throw ToolFileError("File exceeds the configured text limit");
    std::string content;content.reserve(info.nFileSizeLow);std::array<char,8192> buffer{};
    for(;;) {
        check_cancel(cancel);DWORD read=0;
        if(!ReadFile(file.value,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr)) throw ToolFileError("Cannot read workspace file");
        if(!read) break;
        if(bytes_read)*bytes_read+=read;
        if(read>max_bytes-content.size()) throw ToolFileError("File exceeds the configured text limit");
        content.append(buffer.data(),read);
    }
    if(require_text && !valid_text(content)) throw ToolFileError("File is binary or not UTF-8 text");
    check_cancel(cancel);impl_->verify(file.value);
    if(capture_version) {
        check_cancel(cancel);impl_->verify(file.value);
        const auto id=file_identity(file.value),hash=content_hash(content);
        if(identity()!=workspace_id) throw ToolAccessDenied("Workspace identity changed during snapshot");
        return {relative,std::move(content),workspace_id,id,hash};
    }
    return {relative,std::move(content),{},{},{}};
}
WorkspaceListing WorkspaceTools::list_files(const std::string& input,std::stop_token cancel) const {
    check_cancel(cancel);const auto relative=relative_path(input);
    Handle directory(CreateFileW(impl_->path(relative).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    impl_->verify(directory.value);BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(directory.value,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) throw ToolFileError("Expected a workspace directory");
    if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) throw ToolAccessDenied("Directory links are not traversed");
    // Enumerate the verified directory handle, never reopen a pathname that can
    // be replaced by a junction between validation and enumeration.
    alignas(FILE_ID_BOTH_DIR_INFO) std::array<std::byte,65536> buffer{};WorkspaceListing result;
    bool first=true;
    for(;;) {
        check_cancel(cancel);
        const auto kind=first?FileIdBothDirectoryRestartInfo:FileIdBothDirectoryInfo;first=false;
        if(!GetFileInformationByHandleEx(directory.value,kind,buffer.data(),static_cast<DWORD>(buffer.size()))) {
            if(GetLastError()==ERROR_NO_MORE_FILES) break;throw ToolFileError("Cannot enumerate workspace directory handle");
        }
        std::size_t offset=0;
        for(;;) {
            const auto* entry=reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buffer.data()+offset);
            const auto header=offsetof(FILE_ID_BOTH_DIR_INFO,FileName);
            if(offset+header>buffer.size() || entry->FileNameLength%sizeof(wchar_t) || entry->FileNameLength>buffer.size()-offset-header)
                throw ToolFileError("Invalid directory enumeration record");
            const std::wstring name(entry->FileName,entry->FileNameLength/sizeof(wchar_t));
            if(name!=L"." && name!=L".."&&!backend_private_component(utf8(name))) {
                if(result.entries.size()==1000) {result.truncated=true;break;}
                result.entries.push_back({utf8(name),(entry->FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)?"link":((entry->FileAttributes&FILE_ATTRIBUTE_DIRECTORY)?"directory":"file")});
            }
            if(!entry->NextEntryOffset) break;
            if(entry->NextEntryOffset<header || entry->NextEntryOffset%alignof(FILE_ID_BOTH_DIR_INFO) || entry->NextEntryOffset>buffer.size()-offset-header) throw ToolFileError("Invalid directory enumeration offset");
            offset+=entry->NextEntryOffset;
        }
        if(result.truncated) break;
    }
    std::sort(result.entries.begin(),result.entries.end(),[](const auto& a,const auto& b){return a.name<b.name;});return result;
}
WorkspaceGlob WorkspaceTools::glob_files(const std::string& pattern,const std::string& input,bool hidden,std::size_t limit,std::stop_token cancel,bool respect_ignore) const {
    if(!limit||limit>1000)throw std::invalid_argument("Glob result limit must be 1-1000");
    return discover_files(pattern,input,hidden,limit,cancel,respect_ignore);
}
WorkspaceGlob WorkspaceTools::discover_files(const std::string& pattern,const std::string& input,bool hidden,std::size_t limit,std::stop_token cancel,bool respect_ignore,const std::function<bool(const std::string&)>& visitor) const {
    const bool negative=pattern.starts_with('!');const PathGlob compiled(negative?pattern.substr(1):pattern);check_cancel(cancel);
    auto relative=relative_path(input);while(relative.size()>1&&relative.back()=='/')relative.pop_back();
    const auto workspace=identity();PathIgnore ignores;
    // Retain every search-root ancestor. Unlike pathname recursion this cannot
    // traverse a junction substituted between directory admission and opening.
    std::vector<std::unique_ptr<Handle>> anchors;
    anchors.push_back(std::make_unique<Handle>(CreateFileW(impl_->base.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)));
    if(file_identity(anchors.back()->value)!=workspace)throw ToolAccessDenied("Glob root identity changed");
    impl_->verify_creation_directory(anchors.back()->value);
    std::string ancestor=".";
    if(relative!=".")for(const auto& component:std::filesystem::u8path(relative)){
        if(respect_ignore)try{impl_->load_ignores(anchors.back()->value,ancestor,ignores,cancel);}catch(const IgnoreMetadataBudgetExceeded&){throw ToolFileError("Ancestor ignore metadata exceeds limits");}
        check_cancel(cancel);creation_component(component.native());if(anchors.size()>32)throw ToolAccessDenied("Glob search root exceeds 32 components");
        anchors.push_back(std::make_unique<Handle>(relative_file(anchors.back()->value,component.native(),true,false)));impl_->verify_creation_directory(anchors.back()->value);
        ancestor=ancestor=="."?utf8(component.native()):ancestor+"/"+utf8(component.native());
    }
    WorkspaceGlob result;std::size_t steps=0,output_bytes=256,visited_files=0;bool stopped=false;
    const std::size_t entry_limit=visitor?10000:20000;
    // Enumeration finishes before descending, so one buffer can serve every
    // frame without 64 KiB per recursive level on a Windows thread stack.
    alignas(FILE_ID_BOTH_DIR_INFO) std::array<std::byte,65536> buffer{};
    const auto bounded=[&](const std::string& reason){result.truncated=true;if(std::find(result.limits.begin(),result.limits.end(),reason)==result.limits.end())result.limits.push_back(reason);};
    const auto skipped=[&]{++result.skipped_entries;bounded("skipped_entries");};
    std::function<void(HANDLE,const std::string&,const std::string&,std::size_t)> walk;
    walk=[&](HANDLE directory,const std::string& scope,const std::string& match_path,std::size_t depth){
        check_cancel(cancel);if(stopped)return;
        if(++result.scanned_directories>2000){--result.scanned_directories;bounded("directory_limit");stopped=true;return;}
        impl_->verify_creation_directory(directory);
        const auto saved_ignores=ignores.checkpoint();
        struct Restore {PathIgnore& state;PathIgnore::Checkpoint before;~Restore(){state.restore(before);}} restore{ignores,saved_ignores};
        if(respect_ignore)try{impl_->load_ignores(directory,scope,ignores,cancel);}catch(const IgnoreMetadataBudgetExceeded&){bounded("ignore_metadata_limit");stopped=true;return;}
        std::vector<WorkspaceEntry> entries;bool first=true;
        for(;;){
            check_cancel(cancel);const auto kind=first?FileIdBothDirectoryRestartInfo:FileIdBothDirectoryInfo;first=false;
            if(!GetFileInformationByHandleEx(directory,kind,buffer.data(),static_cast<DWORD>(buffer.size()))){if(GetLastError()==ERROR_NO_MORE_FILES)break;throw ToolFileError("Cannot enumerate glob directory");}
            std::size_t position=0;
            for(;;){
                const auto header=offsetof(FILE_ID_BOTH_DIR_INFO,FileName);if(position+header>buffer.size())throw ToolFileError("Invalid glob directory record");
                const auto* entry=reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buffer.data()+position);
                if(entry->FileNameLength%sizeof(wchar_t)||entry->FileNameLength>buffer.size()-position-header)throw ToolFileError("Invalid glob directory name");
                const std::wstring wide_name(entry->FileName,entry->FileNameLength/sizeof(wchar_t));
                if(wide_name!=L"."&&wide_name!=L".."){
                    if(result.scanned_entries==entry_limit){bounded("entry_limit");break;}++result.scanned_entries;
                    const auto name=utf8(wide_name);auto lower=name;for(auto& byte:lower)if(byte>='A'&&byte<='Z')byte+=('a'-'A');while(!lower.empty()&&(lower.back()=='.'||lower.back()==' '))lower.pop_back();
                    if(!backend_private_component(name)&&lower!=".git"&&(hidden||(!name.starts_with('.')&&!(entry->FileAttributes&FILE_ATTRIBUTE_HIDDEN))))entries.push_back({name,(entry->FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)?"link":((entry->FileAttributes&FILE_ATTRIBUTE_DIRECTORY)?"directory":"file")});
                }
                if(!entry->NextEntryOffset)break;
                if(entry->NextEntryOffset<header||entry->NextEntryOffset%alignof(FILE_ID_BOTH_DIR_INFO)||entry->NextEntryOffset>buffer.size()-position-header)throw ToolFileError("Invalid glob directory offset");position+=entry->NextEntryOffset;
            }
            if(result.scanned_entries==entry_limit){bounded("entry_limit");break;}
        }
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.name<b.name;});
        for(const auto& entry:entries){
            check_cancel(cancel);if(stopped)break;
            const auto path=scope=="."?entry.name:scope+"/"+entry.name,matching=match_path.empty()?entry.name:match_path+"/"+entry.name;
            if(entry.kind=="link"){skipped();continue;}
            try{
                Handle opened(relative_file(directory,wide(entry.name),entry.kind=="directory",false));impl_->verify(opened.value);
                BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(opened.value,&info))throw ToolFileError("Cannot inspect glob entry");
                if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){skipped();continue;}
                const bool directory_entry=entry.kind=="directory",matched=compiled.matches(matching,steps,cancel);
                if(directory_entry){if(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw ToolFileError("Discovery entry changed type");}
                else if(GetFileType(opened.value)!=FILE_TYPE_DISK||(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||info.nNumberOfLinks!=1){skipped();continue;}
                if(negative&&matched){++result.ignored_entries;continue;}
                if(respect_ignore&&(negative||!matched)&&ignores.ignored(path,directory_entry,steps,cancel)){++result.ignored_entries;continue;}
                if(entry.kind=="directory"){
                    if(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw ToolFileError("Glob entry changed type");
                    if(depth==32){bounded("depth_limit");continue;}walk(opened.value,path,matching,depth+1);
                }else{
                    if(!negative&&!matched)continue;
                    if(visitor){if(visited_files==limit){bounded("file_limit");stopped=true;break;}++visited_files;if(!visitor(path)){bounded("consumer_limit");stopped=true;break;}continue;}
                    if(result.paths.size()==limit){bounded("result_limit");stopped=true;break;}
                    std::size_t cost=4;for(const unsigned char byte:path)cost+=byte<32?6:(byte=='"'||byte=='\\'?2:1);
                    if(output_bytes+cost>50*1024){bounded("output_limit");stopped=true;break;}output_bytes+=cost;result.paths.push_back(path);
                }
            }catch(const GlobMatchBudgetExceeded&){bounded("match_budget");stopped=true;}
            catch(const ToolAccessDenied&){skipped();}catch(const ToolFileError&){skipped();}
        }
        impl_->verify_creation_directory(directory);
    };
    walk(anchors.back()->value,relative,"",0);check_cancel(cancel);
    if(identity()!=workspace)throw ToolAccessDenied("Workspace identity changed during glob");
    result.ignore_files=ignores.source_files();std::sort(result.paths.begin(),result.paths.end());return result;
}
WorkspaceSearch WorkspaceTools::search_files(const std::string& query,std::stop_token cancel) const {
    return search_files(query,WorkspaceSearchOptions{},cancel);
}
WorkspaceSearch WorkspaceTools::search_files(const std::string& query,const WorkspaceSearchOptions& options,std::stop_token cancel) const {
    if(query.empty() || query.size()>4096 || !valid_text(query)) throw std::invalid_argument("Search query must be UTF-8 text of 1-4096 bytes");
    if(!options.limit||options.limit>1000)throw std::invalid_argument("Search result limit must be 1-1000");
    check_cancel(cancel);const SearchPattern pattern(query,options.regex,options.case_sensitive);
    if(!options.include.empty())PathGlob validate(options.include.starts_with('!')?options.include.substr(1):options.include);
    WorkspaceSearch result;std::size_t bytes=0,output_bytes=256;
    const auto bounded=[&](const std::string& reason){result.truncated=true;if(std::find(result.limits.begin(),result.limits.end(),reason)==result.limits.end())result.limits.push_back(reason);};
    const auto json_cost=[](std::string_view text){std::size_t size=0;for(const unsigned char byte:text)size+=byte<32?6:(byte=='"'||byte=='\\'?2:1);return size;};
    const auto visit=[&](const std::string& path){
            if(bytes>=64*1024*1024){bounded("byte_limit");return false;}
            WorkspaceFile file;
            try {auto read=read_snapshot(path,false,cancel,true,64*1024*1024-bytes,&bytes);file={std::move(read.path),std::move(read.content)};}
            catch(const ToolAccessDenied&) {++result.skipped_entries;bounded("skipped_files");return true;} catch(const ToolFileError&) {++result.skipped_entries;bounded("skipped_files");return true;}
            ++result.scanned_files;
            std::size_t start=0;std::int64_t number=1;
            while(start<file.content.size()) {
                check_cancel(cancel);auto end=file.content.find('\n',start);if(end==std::string::npos) end=file.content.size();
                auto text=std::string_view(file.content).substr(start,end-start);if(!text.empty() && text.back()=='\r') text.remove_suffix(1);
                if(pattern.matches(text)) {
                    if(result.matches.size()==options.limit) {bounded("result_limit");return false;}
                    auto preview=prefix(text,4096);const auto cost=json_cost(path)+json_cost(preview)+96;
                    if(output_bytes+cost>50*1024){bounded("output_limit");return false;}output_bytes+=cost;
                    result.matches.push_back({path,std::move(preview),number,text.size()>4096});
                }
                start=end+1;++number;
            }
        return true;
    };
    const auto supplied=relative_path(options.path),workspace=identity();auto relative=supplied;
    while(relative.size()>1&&relative.back()=='/')relative.pop_back();
    // Keep verified non-link parents alive for direct file targets too. The
    // final target handle then prevents its leaf from being renamed/replaced
    // while the content reader opens the same authorized object.
    std::optional<Impl::CreationParent> anchor;
    if(relative!=".")anchor.emplace(impl_->creation_parent(relative,cancel));
    Handle target(CreateFileW(impl_->path(supplied).c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));impl_->verify(target.value);BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(target.value,&info))throw ToolFileError("Cannot inspect search target");
    if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)throw ToolAccessDenied("Search target cannot be a link");
    if(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
        const auto found=discover_files(options.include.empty()?"!**/.git/**":options.include,relative,options.hidden,10000,cancel,options.respect_ignore,visit);
        result.truncated=result.truncated||found.truncated;result.skipped_entries+=found.skipped_entries;result.ignored_entries=found.ignored_entries;result.ignore_files=found.ignore_files;
        for(const auto& reason:found.limits)bounded(reason);
    }else{
        if(supplied.ends_with('/'))throw ToolFileError("A directory search path must name a directory");
        if(GetFileType(target.value)!=FILE_TYPE_DISK||info.nNumberOfLinks!=1)throw ToolAccessDenied("Search requires a regular single-link file");visit(relative);
    }
    check_cancel(cancel);impl_->verify(target.value);if(identity()!=workspace)throw ToolAccessDenied("Workspace identity changed during search");
    return result;
}
}
