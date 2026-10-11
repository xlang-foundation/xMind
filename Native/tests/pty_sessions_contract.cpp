#include "agentflow/pty_sessions.hpp"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::string system_shell(){wchar_t buffer[32768]{};const auto count=GetSystemDirectoryW(buffer,static_cast<UINT>(std::size(buffer)));if(!count||count>=std::size(buffer))throw std::runtime_error("Cannot locate the Windows system directory");return (std::filesystem::path(std::wstring(buffer,count))/L"cmd.exe").string();}
}

int main(int argc,char** argv){
    if(argc!=2)return 2;
    const char* stage="workspace";std::size_t observed_bytes=0;bool marker_seen=false;
    try{
        const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));std::filesystem::create_directories(root);
        stage="launch";
        agentflow::PtySessions sessions(root.string(),system_shell());
        const auto first=sessions.create("session-one","Contract terminal",30,100);require(first.running&&first.rows==30&&first.columns==100,"Actual ConPTY must launch in the opened workspace at the requested size");
        stage="list-and-resize";
        const auto listed=sessions.list("session-one");require(listed.size()==1&&listed.front().id==first.id,"Terminal must be discoverable for its owning conversation");require(sessions.list("other-session").empty(),"Terminal listing must stay scoped to its conversation");
        sessions.resize(first.id,35,110);require(sessions.get(first.id).rows==35&&sessions.get(first.id).columns==110,"Actual ConPTY resize must update the terminal dimensions");
        stage="interactive-readiness";
        std::uint64_t cursor=0;std::string observed;const auto ready_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(observed.size()<64&&std::chrono::steady_clock::now()<ready_deadline){auto chunk=sessions.read(first.id,cursor,32768,std::chrono::milliseconds(250));require(!chunk.truncated,"Fresh terminal output must not be truncated");cursor=chunk.next_sequence;observed+=chunk.data;}
        observed_bytes=observed.size();require(observed.size()>=64,"Actual shell must produce startup output before input");
        stage="interactive-output";
        sessions.write(first.id,"echo XMIND_TERMINAL_OUTPUT_OK\r\n");
        const auto output_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(observed.find("XMIND_TERMINAL_OUTPUT_OK")==std::string::npos&&std::chrono::steady_clock::now()<output_deadline){auto chunk=sessions.read(first.id,cursor,32768,std::chrono::milliseconds(250));require(!chunk.truncated,"Fresh terminal output must not be truncated");cursor=chunk.next_sequence;observed+=chunk.data;}
        marker_seen=observed.find("XMIND_TERMINAL_OUTPUT_OK")!=std::string::npos;observed_bytes=observed.size();require(marker_seen,"Actual interactive shell input must produce replayable pseudoconsole output");
        stage="process-exit";
        sessions.write(first.id,"exit\r");const auto exit_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);while(sessions.get(first.id).running&&std::chrono::steady_clock::now()<exit_deadline)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto exited=sessions.get(first.id);require(!exited.running,"Terminal must report the actual shell exit");
        stage="cleanup";sessions.close(first.id);require(sessions.list("session-one").empty(),"Closing a terminal must retire it from the local owner");
        bool bad_size=false;try{sessions.create("session-one","bad",4,80);}catch(const std::invalid_argument&){bad_size=true;}require(bad_size,"Terminal dimensions outside the bounded native range must be rejected");
        bool bad_input=false;try{sessions.write("missing",std::string("\0",1));}catch(const std::invalid_argument&){bad_input=true;}require(bad_input,"NUL-containing shell input must be rejected before dispatch");
        std::cout<<"Native persistent PTY contract passed actual Windows ConPTY creation, conversation scoping, bounded resize/input, live output replay, process exit observation and cleanup. Shell I/O was synthetic; no user workspace command was run.\n";return 0;
    }catch(const std::exception&){std::cerr<<"Native persistent PTY contract failed at "<<stage<<"; captured="<<observed_bytes<<" bytes; expected synthetic marker="<<(marker_seen?"yes":"no")<<". No shell output or workspace contents are included.\n";return 1;}
}
