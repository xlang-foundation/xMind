#define NOMINMAX
#include <windows.h>
#include "agent_worker_crash_fixture.hpp"
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);worker_crash_fixture::install();RaiseException(0xC0000374,EXCEPTION_NONCONTINUABLE,0,nullptr);return 3;}
