#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace agentflow::detail {
inline bool profile_record_reader_blocks_delete(const std::wstring& file){
    const auto probe=CreateFileW(file.c_str(),DELETE|FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(probe!=INVALID_HANDLE_VALUE){CloseHandle(probe);return false;}
    return GetLastError()==ERROR_SHARING_VIOLATION;
}
// The caller pins and validates both paths and flushes the staged ciphertext.
// Retain that exact file while a transient reader prevents atomic replacement.
template<class AtomicReplace>
inline void publish_profile_record_with(const std::wstring& staged,const std::wstring& target,
                                       std::chrono::milliseconds timeout,AtomicReplace replace){
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    bool retried_unconfirmed_denial=false;
    while(!replace(staged,target)){
        const auto error=GetLastError();
        // MoveFileEx can report ACCESS_DENIED for a deny-delete reader.
        // Retry that case only when a non-mutating access probe confirms sharing.
        const bool sharing=error==ERROR_SHARING_VIOLATION||error==ERROR_LOCK_VIOLATION||
            (error==ERROR_ACCESS_DENIED&&(profile_record_reader_blocks_delete(staged)||
                                        profile_record_reader_blocks_delete(target)));
        // The blocking reader may close before the probes observe it. Permit
        // one retry of that unconfirmed denial, using the same flushed file;
        // persistent permission errors still fail promptly without a fallback.
        const bool unconfirmed_denial=error==ERROR_ACCESS_DENIED&&!sharing&&!retried_unconfirmed_denial;
        if((!sharing&&!unconfirmed_denial)||
           std::chrono::steady_clock::now()>=deadline)
            throw std::runtime_error("Cannot publish local profile record (Win32 "+
                std::to_string(error)+"); stored database was preserved");
        if(unconfirmed_denial)retried_unconfirmed_denial=true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}
inline void publish_profile_record(const std::wstring& staged,const std::wstring& target,
                                   std::chrono::milliseconds timeout=std::chrono::seconds(5)){
    publish_profile_record_with(staged,target,timeout,[](const std::wstring& from,const std::wstring& to){
        return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    });
}
}
