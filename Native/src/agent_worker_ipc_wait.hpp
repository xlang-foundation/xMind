#pragma once
#include <chrono>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace agentflow {
// Per-observer relative wait; never changes the process/system timer period.
// Preserve the existing sleep path when high-resolution timers are unavailable.
class AgentWorkerIpcWait final {
public:
    AgentWorkerIpcWait()=default;
    ~AgentWorkerIpcWait(){if(timer_)CloseHandle(timer_);}
    AgentWorkerIpcWait(const AgentWorkerIpcWait&)=delete;
    AgentWorkerIpcWait& operator=(const AgentWorkerIpcWait&)=delete;
    void pause(){LARGE_INTEGER due{};due.QuadPart=-20000;if(timer_&&SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)){if(WaitForSingleObject(timer_,10)==WAIT_OBJECT_0)return;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
private:HANDLE timer_=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
};
}
