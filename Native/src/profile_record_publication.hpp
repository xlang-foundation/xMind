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
inline void publish_profile_record(const std::wstring& staged,const std::wstring& target,
                                   std::chrono::milliseconds timeout=std::chrono::seconds(5)){
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    while(!MoveFileExW(staged.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        const auto error=GetLastError();
        // MoveFileEx can report ACCESS_DENIED for a deny-delete reader.
        // Retry that case only when a non-mutating access probe confirms sharing.
        const bool sharing=error==ERROR_SHARING_VIOLATION||error==ERROR_LOCK_VIOLATION||
            (error==ERROR_ACCESS_DENIED&&(profile_record_reader_blocks_delete(staged)||
                                        profile_record_reader_blocks_delete(target)));
        if(!sharing||
           std::chrono::steady_clock::now()>=deadline)
            throw std::runtime_error("Cannot publish local profile record (Win32 "+
                std::to_string(error)+"); stored database was preserved");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}
}
