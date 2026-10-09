#include "agentflow/owner_process.hpp"
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <stdexcept>
using agentflow::observe_owner_exit;
void require(bool yes){if(!yes)throw std::runtime_error("Native process observation contract failed");}
std::string birth(HANDLE process){FILETIME created{},ended{},kernel{},user{};require(GetProcessTimes(process,&created,&ended,&kernel,&user)!=0);return std::to_string((static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime);}
int main(int argc,char**){try{
    if(argc==2){Sleep(150);return 0;}
    const auto self=GetCurrentProcessId();const auto created=birth(GetCurrentProcess());
    const auto live=observe_owner_exit(self,created,0);require(!live.exited&&live.identity_matches);
    const auto different=observe_owner_exit(self,std::to_string(std::stoull(created)+1),0);require(different.exited&&!different.identity_matches);
    for(const auto value:{std::string{},"0"+created,std::string("not-a-time")}){bool rejected=false;try{observe_owner_exit(self,value,0);}catch(const std::invalid_argument&){rejected=true;}require(rejected);}
    bool rejected=false;try{observe_owner_exit(0,created,0);}catch(const std::invalid_argument&){rejected=true;}require(rejected);
    wchar_t image[32768];const auto size=GetModuleFileNameW(nullptr,image,32768);require(size>0&&size<32768);
    std::wstring command=L"\""+std::wstring(image,size)+L"\" --child";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    require(CreateProcessW(image,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=0);
    CloseHandle(child.hThread);const auto childBirth=birth(child.hProcess);
    const auto stopped=observe_owner_exit(child.dwProcessId,childBirth,5000);require(stopped.exited&&stopped.identity_matches);
    DWORD code=1;require(GetExitCodeProcess(child.hProcess,&code)!=0&&code==0);CloseHandle(child.hProcess);
    require(observe_owner_exit(child.dwProcessId,childBirth,0).exited);
    require(!observe_owner_exit(self,created,0).exited);
    std::cout<<"Actual Windows live identity, bounded wait, exact child exit and PID-reuse preconditions passed; no process was signalled\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
