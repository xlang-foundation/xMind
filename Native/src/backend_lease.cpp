#include "agentflow/backend_lease.hpp"
#include "agentflow/records.hpp"
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
    impl_->handle=CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
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
