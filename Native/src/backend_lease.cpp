#include "agentflow/backend_lease.hpp"
#include "agentflow/records.hpp"
#include <algorithm>
#include <filesystem>
#include <system_error>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace agentflow {
namespace {
std::filesystem::path database_path(const std::string& input) {
    if(input.empty() || input==":memory:" || input.find('\0')!=std::string::npos)
        throw std::invalid_argument("Backend requires a file database");
    const auto path=std::filesystem::weakly_canonical(std::filesystem::u8path(input));
    if(std::filesystem::exists(path) && std::filesystem::hard_link_count(path)>1)
        throw std::invalid_argument("Backend database must not have hard-link aliases");
    return path;
}
#if defined(_WIN32)
std::wstring lease_open_path(const std::filesystem::path& database,const std::filesystem::path& lock) {
    const auto original=lock.native();
    // Existing extended/device spellings retain their exact Win32 semantics.
    if(original.starts_with(L"\\\\?\\")||original.starts_with(L"\\\\.\\")||!lock.is_absolute())return original;
    auto value=original;std::replace(value.begin(),value.end(),L'/',L'\\');
    auto source=database.native();std::replace(source.begin(),source.end(),L'/',L'\\');
    const bool drive=source.size()>=3&&((source[0]>=L'A'&&source[0]<=L'Z')||(source[0]>=L'a'&&source[0]<=L'z'))&&source[1]==L':'&&source[2]==L'\\';
    const bool unc=source.starts_with(L"\\\\");
    if(!drive&&!unc)return original;
    // Prefixing must not turn an ordinary device/trimmed component into a new
    // literal filename. Such inputs keep the original API behavior instead.
    for(std::size_t offset=drive?3:2;offset<source.size();) {
        const auto end=source.find(L'\\',offset);const auto part=source.substr(offset,end==std::wstring::npos?source.size()-offset:end-offset);
        if(part.empty()||part==L"."||part==L".."||part.back()==L'.'||part.back()==L' '||part.find_first_of(L"<>:\"|?*")!=std::wstring::npos)return original;
        auto stem=part.substr(0,part.find(L'.'));for(auto& character:stem)if(character>=L'a'&&character<=L'z')character-=L'a'-L'A';
        if(stem==L"CON"||stem==L"PRN"||stem==L"AUX"||stem==L"NUL"||stem==L"CONIN$"||stem==L"CONOUT$"||
           (stem.size()==4&&(stem.starts_with(L"COM")||stem.starts_with(L"LPT"))&&
            ((stem[3]>=L'1'&&stem[3]<=L'9')||stem[3]==0x00b9||stem[3]==0x00b2||stem[3]==0x00b3)))return original;
        if(end==std::wstring::npos)break;offset=end+1;
    }
    return drive?L"\\\\?\\"+value:L"\\\\?\\UNC\\"+value.substr(2);
}
#endif
}
struct BackendLease::Impl {
    std::filesystem::path database;
#if defined(_WIN32)
    HANDLE handle=INVALID_HANDLE_VALUE;
    ~Impl() { if(handle!=INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
    int descriptor=-1;
    ~Impl() { if(descriptor!=-1) close(descriptor); }
#endif
};
BackendLease::BackendLease(const std::string& input) : impl_(std::make_unique<Impl>()) {
    impl_->database=database_path(input);
    auto lock=impl_->database;
    lock+=".backend-lock";
#if defined(_WIN32)
    const auto native_lock=lease_open_path(impl_->database,lock);
    impl_->handle=CreateFileW(native_lock.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(impl_->handle==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();
        if(error==ERROR_SHARING_VIOLATION) throw Conflict("Database already has a backend owner");
        throw std::system_error(static_cast<int>(error),std::system_category(),"Backend lease open failed");
    }
#else
    impl_->descriptor=open(lock.c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(impl_->descriptor==-1) throw std::system_error(errno,std::generic_category(),"Backend lease open failed");
    if(flock(impl_->descriptor,LOCK_EX|LOCK_NB)!=0) {
        const auto error=errno;
        if(error==EWOULDBLOCK || error==EAGAIN) throw Conflict("Database already has a backend owner");
        throw std::system_error(error,std::generic_category(),"Backend lease lock failed");
    }
#endif
}
BackendLease::~BackendLease()=default;
bool BackendLease::covers(const std::string& input) const {
    const auto path=database_path(input);
    if(std::filesystem::exists(path) && std::filesystem::exists(impl_->database))
        return std::filesystem::equivalent(path,impl_->database);
    return path==impl_->database;
}
}
