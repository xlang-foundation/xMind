#pragma once
#include <cstdio>
#include <atomic>
#include <dbghelp.h>
#include <condition_variable>
#include <mutex>
#include <thread>
// Test-only fatal diagnostics. Continue normal Windows failure handling and
// preserve the original exception exit status; never convert a crash to a pass.
namespace worker_crash_fixture {
// Dumps are opt-in, confined to the explicitly selected test evidence directory.
// Neither fault handling nor the slow-run observation changes an exit or budget.
inline void dump(EXCEPTION_POINTERS* failure,const wchar_t* reason){
    wchar_t directory[32768]{},path[32768]{};const auto length=GetEnvironmentVariableW(L"XMIND_WORKER_FIXTURE_DUMP_DIR",directory,32768);if(!length||length>=32000)return;
    // DbgHelp is not safe for simultaneous writers or recursive heap faults.
    static std::atomic_flag writing=ATOMIC_FLAG_INIT;if(writing.test_and_set())return;
    struct WriterGuard {std::atomic_flag& flag;~WriterGuard(){flag.clear();}} guard{writing};
    if(swprintf_s(path,L"%s\\fixture-%lu-%llu-%s.dmp",directory,GetCurrentProcessId(),static_cast<unsigned long long>(GetTickCount64()),reason)<0)return;
    const auto library=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!library)return;
    using Writer=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
    const auto writer=reinterpret_cast<Writer>(GetProcAddress(library,"MiniDumpWriteDump"));if(!writer)return;
    const auto file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return;
    const bool heap=failure&&failure->ExceptionRecord&&failure->ExceptionRecord->ExceptionCode==0xC0000374;
    const auto flags=static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo|(heap?(MiniDumpWithFullMemory|MiniDumpWithFullMemoryInfo|MiniDumpWithUnloadedModules):0));
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),failure,FALSE};const auto written=writer(GetCurrentProcess(),GetCurrentProcessId(),file,flags,failure?&info:nullptr,nullptr,nullptr);CloseHandle(file);
    std::fprintf(stderr,"Native worker fixture diagnostic dump pid=%lu reason=%ls written=%d full_memory=%d\n",GetCurrentProcessId(),reason,written,heap?1:0);std::fflush(stderr);
}
inline LONG WINAPI report(EXCEPTION_POINTERS* failure){
    if(failure&&failure->ExceptionRecord){const auto& record=*failure->ExceptionRecord;HMODULE module=nullptr;char path[32768]{};const auto address=record.ExceptionAddress;
        if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(address),&module))GetModuleFileNameA(module,path,sizeof(path));
        std::fprintf(stderr,"Native worker fixture fatal exception=0x%08lx thread=%lu module=%s offset=0x%llx\n",record.ExceptionCode,GetCurrentThreadId(),path,static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address)-reinterpret_cast<std::uintptr_t>(module)));std::fflush(stderr);
    }
    dump(failure,L"fatal");
    return EXCEPTION_CONTINUE_SEARCH;
}
inline LONG CALLBACK heap_failure(EXCEPTION_POINTERS* failure){if(failure&&failure->ExceptionRecord&&failure->ExceptionRecord->ExceptionCode==0xC0000374)report(failure);return EXCEPTION_CONTINUE_SEARCH;}
inline void install(){SetUnhandledExceptionFilter(report);
    // Observe heap corruption without handling it or changing Windows termination.
    if(GetEnvironmentVariableW(L"XMIND_WORKER_FIXTURE_DUMP_DIR",nullptr,0))AddVectoredExceptionHandler(1,heap_failure);
    static std::jthread observer([](std::stop_token stop){if(!GetEnvironmentVariableW(L"XMIND_WORKER_FIXTURE_DUMP_DIR",nullptr,0))return;std::mutex mutex;std::condition_variable_any wake;std::unique_lock lock(mutex);wake.wait_for(lock,stop,std::chrono::seconds(50),[]{return false;});if(!stop.stop_requested())dump(nullptr,L"slow");});
}
}
