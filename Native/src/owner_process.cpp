#include "agentflow/owner_process.hpp"
#define NOMINMAX
#include <windows.h>
#include <charconv>
#include <stdexcept>
namespace agentflow {
OwnerProcessObservation observe_owner_exit(std::uint32_t pid,const std::string& birth,std::uint32_t timeout_ms){
    std::uint64_t expected=0;const auto parsed=std::from_chars(birth.data(),birth.data()+birth.size(),expected);
    if(!pid||timeout_ms>60000||birth.empty()||birth.size()>20||birth.front()=='0'||parsed.ec!=std::errc{}||parsed.ptr!=birth.data()+birth.size()||!expected)throw std::invalid_argument("Invalid native owner process precondition");
    const auto process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process){if(GetLastError()==ERROR_INVALID_PARAMETER)return {true,false};throw std::runtime_error("Cannot observe native owner process");}
    struct Close {HANDLE value;~Close(){CloseHandle(value);}} close{process};
    FILETIME created{},ended{},kernel{},user{};
    if(!GetProcessTimes(process,&created,&ended,&kernel,&user))throw std::runtime_error("Cannot verify native owner process identity");
    const auto actual=(static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    if(actual!=expected)return {true,false};
    const auto result=WaitForSingleObject(process,timeout_ms);
    if(result==WAIT_OBJECT_0)return {true,true};
    if(result==WAIT_TIMEOUT)return {false,true};
    throw std::runtime_error("Cannot wait for native owner process exit");
}
}
