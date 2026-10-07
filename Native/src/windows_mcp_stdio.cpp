#include "agentflow/mcp_stdio.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <algorithm>

namespace agentflow {
namespace {
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;
    explicit Handle(HANDLE handle):value(handle) {}
    ~Handle(){reset();}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other) noexcept:value(std::exchange(other.value,nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {if(this!=&other){reset();value=std::exchange(other.value,nullptr);}return *this;}
    void reset() noexcept {if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value);value=nullptr;}
    HANDLE get() const {return value;}
};
void windows_error(const char* action) {throw McpTransportError(std::string(action)+" (Windows error "+std::to_string(GetLastError())+")");}
Handle event() {Handle value(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!value.get()) windows_error("Cannot create MCP I/O event");return value;}
std::wstring wide(const std::string& text) {
    if(text.size()>32768 || text.find('\0')!=std::string::npos) throw std::invalid_argument("MCP launch text exceeds limits");
    if(text.empty()) return {};
    const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(!size) throw std::invalid_argument("MCP launch text must be UTF-8");
    std::wstring result(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),size);return result;
}
std::wstring quote(const std::wstring& value) {
    std::wstring result=L"\"";std::size_t slashes=0;
    for(wchar_t c:value) {if(c==L'\\'){++slashes;continue;}result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result.push_back(c);}
    result.append(slashes*2,L'\\');result.push_back(L'\"');return result;
}
struct CaseLess {bool operator()(const std::wstring& left,const std::wstring& right) const {return CompareStringOrdinal(left.data(),static_cast<int>(left.size()),right.data(),static_cast<int>(right.size()),TRUE)==CSTR_LESS_THAN;}};
std::vector<wchar_t> environment(const McpStdioConfiguration& config) {
    std::map<std::wstring,std::wstring,CaseLess> variables;
    std::array<wchar_t,32768> directory{};const auto size=GetWindowsDirectoryW(directory.data(),static_cast<UINT>(directory.size()));
    if(!size || size>=directory.size()) windows_error("Cannot obtain MCP system environment");variables[L"SystemRoot"]=std::wstring(directory.data(),size);
    const auto temporary=GetTempPathW(static_cast<DWORD>(directory.size()),directory.data());
    if(!temporary || temporary>=directory.size()) windows_error("Cannot obtain MCP temporary directory");
    variables[L"TEMP"]=std::wstring(directory.data(),temporary);variables[L"TMP"]=variables[L"TEMP"];
    if(config.environment.size()>128) throw std::invalid_argument("MCP environment exceeds limits");
    std::map<std::wstring,bool,CaseLess> supplied;
    for(const auto& [key,value]:config.environment) {
        const auto name=wide(key);if(name.empty() || name.size()>256 || name.find(L'=')!=std::wstring::npos) throw std::invalid_argument("Invalid MCP environment name");
        for(wchar_t c:name) if(c<32) throw std::invalid_argument("Invalid MCP environment name");
        if(!supplied.emplace(name,true).second) throw std::invalid_argument("Duplicate MCP environment name");variables[name]=wide(value);
    }
    std::vector<wchar_t> result;
    for(const auto& [key,value]:variables) {result.insert(result.end(),key.begin(),key.end());result.push_back(L'=');result.insert(result.end(),value.begin(),value.end());result.push_back(L'\0');if(result.size()>65535) throw std::invalid_argument("MCP environment block exceeds limits");}
    result.push_back(L'\0');return result;
}
struct PendingIo {
    HANDLE pipe;Handle signal=event();OVERLAPPED overlapped{};bool pending=false;
    explicit PendingIo(HANDLE value):pipe(value){overlapped.hEvent=signal.get();}
    ~PendingIo(){if(pending){CancelIoEx(pipe,&overlapped);DWORD ignored=0;GetOverlappedResult(pipe,&overlapped,&ignored,TRUE);}}
    bool complete(DWORD& bytes) {const auto result=GetOverlappedResult(pipe,&overlapped,&bytes,TRUE);pending=false;return result!=FALSE;}
};
struct Pipe {Handle parent,child;};
struct PipeSecurity {
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    PipeSecurity() {
        HANDLE opened=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&opened))windows_error("Cannot determine MCP pipe owner");Handle token(opened);
        DWORD size=0;GetTokenInformation(token.get(),TokenUser,nullptr,0,&size);
        if(!size || size>65536)throw McpTransportError("Invalid MCP pipe owner information");
        std::vector<std::byte> info(size);if(!GetTokenInformation(token.get(),TokenUser,info.data(),size,&size))windows_error("Cannot read MCP pipe owner");
        LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid,&sid))windows_error("Cannot encode MCP pipe owner");
        std::wstring rule;
        try {rule=std::wstring(L"D:P(A;;GA;;;SY)(A;;GA;;;")+sid+L")";}catch(...){LocalFree(sid);throw;}LocalFree(sid);
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(rule.c_str(),SDDL_REVISION_1,&descriptor,nullptr))windows_error("Cannot restrict MCP pipe access");
    }
    ~PipeSecurity(){if(descriptor)LocalFree(descriptor);}
};
Pipe pipe(bool parent_writes) {
    std::array<UCHAR,16> random{};if(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) throw McpTransportError("Cannot create MCP pipe identity");
    constexpr wchar_t hex[]=L"0123456789abcdef";std::wstring name=L"\\\\.\\pipe\\xmind-mcp-";for(auto byte:random){name.push_back(hex[byte>>4]);name.push_back(hex[byte&15]);}
    PipeSecurity security;SECURITY_ATTRIBUTES restricted{sizeof(SECURITY_ATTRIBUTES),security.descriptor,FALSE};
    Pipe result;result.parent=Handle(CreateNamedPipeW(name.c_str(),(parent_writes?PIPE_ACCESS_OUTBOUND:PIPE_ACCESS_INBOUND)|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&restricted));
    if(result.parent.get()==INVALID_HANDLE_VALUE) windows_error("Cannot create MCP pipe");
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    result.child=Handle(CreateFileW(name.c_str(),parent_writes?GENERIC_READ:GENERIC_WRITE,0,&inheritable,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(result.child.get()==INVALID_HANDLE_VALUE) windows_error("Cannot open MCP child pipe");
    PendingIo connection(result.parent.get());
    if(!ConnectNamedPipe(result.parent.get(),&connection.overlapped)) {
        const auto error=GetLastError();if(error==ERROR_IO_PENDING){connection.pending=true;if(WaitForSingleObject(connection.signal.get(),5000)!=WAIT_OBJECT_0) throw McpTransportError("MCP pipe connection timed out");DWORD ignored=0;if(!connection.complete(ignored)) windows_error("Cannot connect MCP pipe");}
        else if(error!=ERROR_PIPE_CONNECTED) windows_error("Cannot connect MCP pipe");
    }
    return result;
}
struct Attributes {
    std::vector<std::byte> bytes;LPPROC_THREAD_ATTRIBUTE_LIST list=nullptr;
    explicit Attributes(HANDLE* handles) {SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);if(!size) windows_error("Cannot size MCP handle attributes");bytes.resize(size);list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes.data());if(!InitializeProcThreadAttributeList(list,1,0,&size)){list=nullptr;windows_error("Cannot initialize MCP handle attributes");}if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,3*sizeof(HANDLE),nullptr,nullptr)){DeleteProcThreadAttributeList(list);list=nullptr;windows_error("Cannot restrict MCP handle inheritance");}}
    ~Attributes(){if(list)DeleteProcThreadAttributeList(list);}
};
DWORD remaining(McpStdioProcess::Deadline deadline) {
    const auto now=std::chrono::steady_clock::now();if(now>=deadline)return 0;
    const auto millis=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-now).count();return static_cast<DWORD>(std::clamp<std::int64_t>(millis,1,0xfffffffe));
}
}
struct McpStdioProcess::Impl {
    Handle job,process,input,output,errors,wake=event();
    DWORD pid=0;std::size_t limit,buffered=0;bool eof=false,closed=false;
    std::atomic<bool> stopping=false,fault=false;
    std::atomic<std::uint64_t> stderr_bytes=0;
    mutable std::mutex mutex;std::condition_variable ready;std::deque<std::string> chunks;
    // Declared last: failed construction stops/joins pumps before handles close.
    std::jthread output_pump,error_pump;
    explicit Impl(const McpStdioConfiguration& config):limit(config.stdout_buffer_limit) {
        if(limit<8192 || limit>4*1024*1024 || config.arguments.size()>64) throw std::invalid_argument("MCP transport configuration exceeds limits");
        if((config.process_memory_limit && (config.process_memory_limit<16*1024*1024 || config.process_memory_limit>1024*1024*1024)) || config.process_cpu_limit<std::chrono::milliseconds::zero() || config.process_cpu_limit>std::chrono::seconds(60) || config.active_process_limit>64)throw std::invalid_argument("Native child resource budget exceeds limits");
        const auto executable=wide(config.executable),directory=wide(config.working_directory);
        if(!std::filesystem::path(executable).is_absolute() || !std::filesystem::is_regular_file(std::filesystem::path(executable)) || !std::filesystem::path(directory).is_absolute() || !std::filesystem::is_directory(std::filesystem::path(directory))) throw std::invalid_argument("MCP executable and working directory must be explicit existing absolute paths");
        std::wstring command=quote(executable);for(const auto& argument:config.arguments){command.push_back(L' ');command+=quote(wide(argument));if(command.size()>32766)throw std::invalid_argument("MCP command line exceeds limits");}
        auto child_environment=environment(config);auto in=pipe(true),out=pipe(false),err=pipe(false);
        job=Handle(CreateJobObjectW(nullptr,nullptr));if(!job.get())windows_error("Cannot create MCP process job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(config.process_memory_limit){limits.BasicLimitInformation.LimitFlags|=JOB_OBJECT_LIMIT_PROCESS_MEMORY|JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;limits.ProcessMemoryLimit=config.process_memory_limit;}
        if(config.process_cpu_limit.count()){limits.BasicLimitInformation.LimitFlags|=JOB_OBJECT_LIMIT_PROCESS_TIME;limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart=config.process_cpu_limit.count()*10000;}
        if(config.active_process_limit){limits.BasicLimitInformation.LimitFlags|=JOB_OBJECT_LIMIT_ACTIVE_PROCESS;limits.BasicLimitInformation.ActiveProcessLimit=config.active_process_limit;}
        if(!SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits)))windows_error("Cannot configure MCP process ownership");
        HANDLE inherited[]{in.child.get(),out.child.get(),err.child.get()};Attributes attributes(inherited);
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=inherited[0];startup.StartupInfo.hStdOutput=inherited[1];startup.StartupInfo.hStdError=inherited[2];startup.lpAttributeList=attributes.list;
        PROCESS_INFORMATION created{};
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,child_environment.data(),directory.c_str(),&startup.StartupInfo,&created))windows_error("Cannot launch configured MCP process");
        process=Handle(created.hProcess);Handle thread(created.hThread);pid=created.dwProcessId;
        if(!AssignProcessToJobObject(job.get(),process.get())){const auto error=GetLastError();TerminateProcess(process.get(),1);WaitForSingleObject(process.get(),5000);SetLastError(error);windows_error("Cannot assign MCP process ownership");}
        input=std::move(in.parent);output=std::move(out.parent);errors=std::move(err.parent);
        if(ResumeThread(thread.get())==static_cast<DWORD>(-1))windows_error("Cannot resume MCP process");
        in.child.reset();out.child.reset();err.child.reset();
        output_pump=std::jthread([this](std::stop_token token){pump(output.get(),false,token);});
        error_pump=std::jthread([this](std::stop_token token){pump(errors.get(),true,token);});
    }
    void fail() noexcept {fault=true;stopping=true;SetEvent(wake.get());{std::lock_guard lock(mutex);ready.notify_all();}TerminateJobObject(job.get(),1);}
    void pump(HANDLE channel,bool stderr_channel,std::stop_token token) noexcept {
        std::stop_callback cancel(token,[this]{stopping=true;SetEvent(wake.get());std::lock_guard lock(mutex);ready.notify_all();});
        try {
            std::array<char,8192> buffer{};
            while(!stopping.load()) {
                PendingIo io(channel);DWORD count=0;const auto success=ReadFile(channel,buffer.data(),static_cast<DWORD>(buffer.size()),&count,&io.overlapped);
                if(!success) {
                    const auto error=GetLastError();if(error==ERROR_BROKEN_PIPE || error==ERROR_PIPE_NOT_CONNECTED)break;
                    if(error!=ERROR_IO_PENDING){if(!stopping.load())fail();break;}
                    io.pending=true;HANDLE waiting[]{io.signal.get(),wake.get()};const auto waited=WaitForMultipleObjects(2,waiting,FALSE,INFINITE);
                    if(waited!=WAIT_OBJECT_0){if(waited!=WAIT_OBJECT_0+1)fail();break;}
                    if(!io.complete(count)){if(!stopping.load() && GetLastError()!=ERROR_BROKEN_PIPE)fail();break;}
                }
                if(!count)break;
                if(stderr_channel){stderr_bytes.fetch_add(count);continue;}
                bool overflow=false;{std::lock_guard lock(mutex);if(count>limit-buffered)overflow=true;else{chunks.emplace_back(buffer.data(),count);buffered+=count;}}
                if(overflow){fail();break;}ready.notify_all();
            }
        }catch(...){fail();}
        if(!stderr_channel){std::lock_guard lock(mutex);eof=true;ready.notify_all();}
    }
    void shutdown() noexcept {
        if(closed)return;closed=true;input.reset();
        WaitForSingleObject(process.get(),1000);
        // Also owns descendants that kept running after the main peer exited.
        TerminateJobObject(job.get(),1);WaitForSingleObject(process.get(),5000);
        stopping=true;SetEvent(wake.get());ready.notify_all();
        if(output_pump.joinable())output_pump.join();if(error_pump.joinable())error_pump.join();
        job.reset();output.reset();errors.reset();
    }
    ~Impl(){shutdown();}
};
McpStdioProcess::McpStdioProcess(const McpStdioConfiguration& configuration):impl_(std::make_unique<Impl>(configuration)) {}
McpStdioProcess::~McpStdioProcess()=default;
void McpStdioProcess::write(std::string_view frame,Deadline deadline,std::stop_token token) {
    auto& state=*impl_;if(state.closed || state.stopping.load())throw McpTransportError("MCP process transport is closed or faulted");
    if(frame.empty() || frame.size()>1024*1024+1 || frame.find('\n')!=frame.size()-1)throw std::invalid_argument("MCP write requires one bounded newline frame");
    if(token.stop_requested())throw McpTransportCancelled("MCP write cancelled before dispatch");
    if(!remaining(deadline))throw McpTransportTimeout("MCP write deadline expired before dispatch");
    PendingIo io(state.input.get());DWORD written=0;
    if(!WriteFile(state.input.get(),frame.data(),static_cast<DWORD>(frame.size()),&written,&io.overlapped)) {
        const auto error=GetLastError();if(error!=ERROR_IO_PENDING){state.fail();SetLastError(error);windows_error("MCP pipe write failed");}
        io.pending=true;auto cancelled=event();std::stop_callback callback(token,[&]{SetEvent(cancelled.get());});
        HANDLE waiting[]{io.signal.get(),state.wake.get(),cancelled.get()};const auto result=WaitForMultipleObjects(3,waiting,FALSE,remaining(deadline));
        if(result!=WAIT_OBJECT_0){state.fail();if(token.stop_requested())throw McpTransportCancelled("MCP write cancelled; remote outcome is not established");if(result==WAIT_TIMEOUT)throw McpTransportTimeout("MCP write timed out; remote outcome is not established");throw McpTransportError("MCP write interrupted; remote outcome is not established");}
        if(!io.complete(written)){const auto error=GetLastError();state.fail();SetLastError(error);windows_error("MCP pipe write completion failed");}
    }
    if(written!=frame.size()){state.fail();throw McpTransportError("MCP frame write was incomplete; remote outcome is not established");}
}
std::optional<std::string> McpStdioProcess::read(Deadline deadline,std::stop_token token) {
    auto& state=*impl_;std::stop_callback callback(token,[&]{std::lock_guard lock(state.mutex);state.ready.notify_all();});std::unique_lock lock(state.mutex);
    while(state.chunks.empty() && !state.eof && !state.stopping.load() && !token.stop_requested())if(state.ready.wait_until(lock,deadline)==std::cv_status::timeout)break;
    if(token.stop_requested())throw McpTransportCancelled("MCP read cancelled");
    if(!state.chunks.empty()){auto result=std::move(state.chunks.front());state.chunks.pop_front();state.buffered-=result.size();return result;}
    if(state.fault.load())throw McpTransportError("MCP process I/O failed or output exceeded its buffer limit");
    if(state.eof || state.closed)return std::nullopt;
    throw McpTransportTimeout("MCP read deadline expired");
}
McpStdioStatus McpStdioProcess::status() const {
    const auto& state=*impl_;std::optional<std::uint32_t> exit;
    if(WaitForSingleObject(state.process.get(),0)==WAIT_OBJECT_0){DWORD code=0;if(GetExitCodeProcess(state.process.get(),&code))exit=code;}
    std::lock_guard lock(state.mutex);return {state.pid,exit,state.buffered,state.stderr_bytes.load(),state.fault.load(),state.closed};
}
void McpStdioProcess::shutdown() noexcept {impl_->shutdown();}
}
