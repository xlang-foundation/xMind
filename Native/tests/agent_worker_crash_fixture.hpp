#pragma once
#include <cstdio>
// Test-only fatal diagnostics. Continue normal Windows failure handling and
// preserve the original exception exit status; never convert a crash to a pass.
namespace worker_crash_fixture {
inline LONG WINAPI report(EXCEPTION_POINTERS* failure){
    if(failure&&failure->ExceptionRecord){const auto& record=*failure->ExceptionRecord;HMODULE module=nullptr;char path[32768]{};const auto address=record.ExceptionAddress;
        if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(address),&module))GetModuleFileNameA(module,path,sizeof(path));
        std::fprintf(stderr,"Native worker fixture fatal exception=0x%08lx thread=%lu module=%s offset=0x%llx\n",record.ExceptionCode,GetCurrentThreadId(),path,static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address)-reinterpret_cast<std::uintptr_t>(module)));std::fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
inline void install(){SetUnhandledExceptionFilter(report);}
}
