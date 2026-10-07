#include "agentflow/workspace_tools.hpp"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <deque>
#include <filesystem>
#include <set>

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
    for(const auto& part:path) if(part==L"..") throw ToolAccessDenied("Parent traversal is outside the tool contract");
    const auto normalized=path.lexically_normal().generic_u8string();
    return {reinterpret_cast<const char*>(normalized.data()),normalized.size()};
}
bool valid_text(const std::string& value) {
    return value.find('\0')==std::string::npos && (value.empty() || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0)>0);
}
std::string prefix(const std::string& value,std::size_t limit) {
    if(value.size()<=limit) return value;
    auto size=limit;while(size && (static_cast<unsigned char>(value[size])&0xc0)==0x80) --size;
    return value.substr(0,size);
}
}
struct WorkspaceTools::Impl {
    Handle root;
    std::wstring base;
    explicit Impl(const std::string& path):root(CreateFileW(wide(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr)),base(final_path(root.value)) {
        const auto attrs=GetFileAttributesW(base.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_DIRECTORY)) throw std::invalid_argument("Workspace root must be a directory");
        while(!base.empty() && base.back()==L'\\') base.pop_back();
    }
    void verify(HANDLE handle) const {
        const auto path=final_path(handle);
        if(!equal(path,base) && !(path.size()>base.size() && path[base.size()]==L'\\' && equal(path.substr(0,base.size()),base)))
            throw ToolAccessDenied("Resolved file is outside the workspace");
    }
    std::wstring path(const std::string& relative) const {
        if(relative==".") return base;
        auto suffix=wide(relative);std::replace(suffix.begin(),suffix.end(),L'/',L'\\');
        return base+L"\\"+suffix;
    }
};
WorkspaceTools::WorkspaceTools(const std::string& root):impl_(std::make_unique<Impl>(root)) {}
WorkspaceTools::~WorkspaceTools()=default;
WorkspaceFile WorkspaceTools::read_file(const std::string& input,std::stop_token cancel) const {
    check_cancel(cancel);const auto relative=relative_path(input);
    Handle file(CreateFileW(impl_->path(relative).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    impl_->verify(file.value);
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.value,&info) || GetFileType(file.value)!=FILE_TYPE_DISK || (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) throw ToolFileError("Expected a regular workspace file");
    if(info.nNumberOfLinks>1) throw ToolAccessDenied("Hard-linked files require a separate explicit policy");
    if(info.nFileSizeHigh || info.nFileSizeLow>1024*1024) throw ToolFileError("File exceeds the 1 MiB text limit");
    std::string content;content.reserve(info.nFileSizeLow);std::array<char,8192> buffer{};
    for(;;) {
        check_cancel(cancel);DWORD read=0;
        if(!ReadFile(file.value,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr)) throw ToolFileError("Cannot read workspace file");
        if(!read) break;
        if(read>1024*1024-content.size()) throw ToolFileError("File exceeds the 1 MiB text limit");
        content.append(buffer.data(),read);
    }
    if(!valid_text(content)) throw ToolFileError("File is binary or not UTF-8 text");
    return {relative,std::move(content)};
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
            if(name!=L"." && name!=L"..") {
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
WorkspaceSearch WorkspaceTools::search_files(const std::string& query,std::stop_token cancel) const {
    if(query.empty() || query.size()>4096 || !valid_text(query)) throw std::invalid_argument("Search query must be UTF-8 text of 1-4096 bytes");
    const std::set<std::string> excluded{".git",".venv",".agentflow","node_modules","__pycache__"};
    WorkspaceSearch result;std::deque<std::string> directories{"."};std::size_t entries=0,bytes=0;
    while(!directories.empty()) {
        check_cancel(cancel);auto current=std::move(directories.front());directories.pop_front();
        WorkspaceListing listing;
        try {listing=list_files(current,cancel);} catch(const ToolAccessDenied&) {++result.skipped_entries;continue;} catch(const ToolFileError&) {++result.skipped_entries;continue;}
        result.truncated=result.truncated || listing.truncated;
        for(const auto& item:listing.entries) {
            check_cancel(cancel);if(++entries>10000) {result.truncated=true;return result;}
            if(excluded.contains(item.name)) continue;
            const auto path=current=="."?item.name:current+"/"+item.name;
            if(item.kind=="directory") {directories.push_back(path);continue;}
            if(item.kind!="file") {++result.skipped_entries;continue;}
            WorkspaceFile file;
            try {file=read_file(path,cancel);} catch(const ToolAccessDenied&) {++result.skipped_entries;continue;} catch(const ToolFileError&) {++result.skipped_entries;continue;}
            ++result.scanned_files;
            if(file.content.size()>64*1024*1024-bytes) {result.truncated=true;return result;}bytes+=file.content.size();
            std::size_t start=0;std::int64_t number=1;
            while(start<file.content.size()) {
                check_cancel(cancel);auto end=file.content.find('\n',start);if(end==std::string::npos) end=file.content.size();
                auto text=file.content.substr(start,end-start);if(!text.empty() && text.back()=='\r') text.pop_back();
                if(text.find(query)!=std::string::npos) {
                    if(result.matches.size()==100) {result.truncated=true;return result;}
                    result.matches.push_back({path,prefix(text,4096),number,text.size()>4096});
                }
                start=end+1;++number;
            }
        }
    }
    return result;
}
}
