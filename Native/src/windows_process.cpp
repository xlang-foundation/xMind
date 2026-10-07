#include "agentflow/process.hpp"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <thread>

namespace agentflow {
namespace {
using Clock=std::chrono::steady_clock;
struct Handle {
    HANDLE value=nullptr;
    explicit Handle(HANDLE h=nullptr):value(h) {}
    ~Handle(){reset();}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other) noexcept:value(std::exchange(other.value,nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {if(this!=&other){reset();value=std::exchange(other.value,nullptr);}return *this;}
    void reset() noexcept {if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}
};
[[noreturn]] void fail(const char* action) {throw ProcessBeforeDispatchError(std::string(action)+" (Windows error "+std::to_string(GetLastError())+")");}
std::wstring wide(const std::string& value) {
    if(value.size()>32768 || value.find('\0')!=std::string::npos)throw std::invalid_argument("Process launch text exceeds limits");
    if(value.empty())return {};
    const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!size)throw std::invalid_argument("Process launch text must be UTF-8");
    std::wstring result(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size);return result;
}
std::wstring quote(const std::wstring& value) {
    std::wstring result=L"\"";std::size_t slashes=0;
    for(wchar_t c:value){if(c==L'\\'){++slashes;continue;}result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result.push_back(c);}
    result.append(slashes*2,L'\\');result.push_back(L'\"');return result;
}
std::string identity(HANDLE handle) {
    FILE_ID_INFO info{};if(!GetFileInformationByHandleEx(handle,FileIdInfo,&info,sizeof(info)))fail("Cannot identify process directory");
    bool supported=false;for(auto b:info.FileId.Identifier)supported|=b!=0;
    if(!supported)throw ProcessBeforeDispatchError("Unsupported process directory identity");
    std::ostringstream out;out<<"windows-local-file-v1:"<<std::hex<<std::setfill('0')<<std::setw(16)<<info.VolumeSerialNumber<<":";
    for(auto b:info.FileId.Identifier)out<<std::setw(2)<<static_cast<unsigned>(b);return out.str();
}
struct DirectoryLease {
    std::vector<Handle> handles;
    explicit DirectoryLease(const std::wstring& directory) {
        const std::filesystem::path path(directory);
        const auto root=path.root_name().native();
        if(!path.is_absolute() || root.size()!=2 || root[1]!=L':' || !((root[0]>=L'A' && root[0]<=L'Z') || (root[0]>=L'a' && root[0]<=L'z')))
            throw std::invalid_argument("Process directory must be an absolute local drive path");
        auto current=path.root_path();open(current);
        for(const auto& component:path.relative_path()) {
            const auto part=component.native();if(part.empty() || part==L".")continue;
            if(part==L".." || part.find_first_of(L":<>\"|?*")!=std::wstring::npos || part.back()==L'.' || part.back()==L' ')
                throw std::invalid_argument("Ambiguous process directory component");
            current/=component;open(current);
        }
    }
    void open(const std::filesystem::path& path) {
        Handle h(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|FILE_TRAVERSE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(h.value==INVALID_HANDLE_VALUE)fail("Cannot retain process directory");
        BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(h.value,&info))fail("Cannot inspect process directory");
        if(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw ProcessBeforeDispatchError("Process directory reparses are unsupported");
        handles.push_back(std::move(h));
    }
    std::string id() const{return identity(handles.back().value);}
};
struct CaseLess {bool operator()(const std::wstring& a,const std::wstring& b) const {return CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE)==CSTR_LESS_THAN;}};
std::vector<wchar_t> environment(const ProcessConfiguration& config) {
    std::map<std::wstring,std::wstring,CaseLess> entries;
    std::array<wchar_t,32768> buffer{};const auto n=GetWindowsDirectoryW(buffer.data(),static_cast<UINT>(buffer.size()));
    if(!n || n>=buffer.size())fail("Cannot obtain process system directory");entries[L"SystemRoot"]=std::wstring(buffer.data(),n);
    const auto t=GetTempPathW(static_cast<DWORD>(buffer.size()),buffer.data());if(!t || t>=buffer.size())fail("Cannot obtain process temporary directory");
    entries[L"TEMP"]=std::wstring(buffer.data(),t);entries[L"TMP"]=entries[L"TEMP"];
    if(config.environment.size()>64)throw std::invalid_argument("Process environment exceeds limits");
    for(const auto& [key,value]:config.environment) {
        const auto name=wide(key);if(name.empty() || name.size()>256 || name.find(L'=')!=std::wstring::npos)throw std::invalid_argument("Invalid process environment name");
        for(auto c:name)if(c<32)throw std::invalid_argument("Invalid process environment name");
        if(!entries.emplace(name,wide(value)).second)throw std::invalid_argument("Duplicate or reserved process environment name");
    }
    std::vector<wchar_t> result;
    for(const auto& [key,value]:entries){result.insert(result.end(),key.begin(),key.end());result.push_back(L'=');result.insert(result.end(),value.begin(),value.end());result.push_back(L'\0');if(result.size()>65534)throw std::invalid_argument("Process environment block exceeds limits");}
    result.push_back(L'\0');return result;
}
struct CapturePipe {
    Handle reader,writer;bool eof=false;
    CapturePipe() {
        SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};HANDLE r=nullptr,w=nullptr;
        if(!CreatePipe(&r,&w,&sa,65536))fail("Cannot create process capture pipe");reader=Handle(r);writer=Handle(w);
        if(!SetHandleInformation(reader.value,HANDLE_FLAG_INHERIT,0))fail("Cannot restrict process capture handle");
    }
};
struct Attributes {
    std::vector<std::byte> storage;LPPROC_THREAD_ATTRIBUTE_LIST list=nullptr;
    explicit Attributes(HANDLE* handles) {
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);if(!size)fail("Cannot size process handle attributes");storage.resize(size);list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(list,1,0,&size)){list=nullptr;fail("Cannot initialize process handle attributes");}
        if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,3*sizeof(HANDLE),nullptr,nullptr)){DeleteProcThreadAttributeList(list);list=nullptr;fail("Cannot restrict process handle inheritance");}
    }
    ~Attributes(){if(list)DeleteProcThreadAttributeList(list);}
};
struct Child {
    Handle job,process,thread;bool dispatched=false,retired=false;
    bool empty() const {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
        if(!QueryInformationJobObject(job.value,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr))fail("Cannot inspect owned process tree");
        return info.ActiveProcesses==0;
    }
    void terminate() {
        if(!TerminateJobObject(job.value,1))fail("Cannot terminate owned process tree");
        const auto until=Clock::now()+std::chrono::seconds(5);
        while(!empty()){if(Clock::now()>=until)throw ProcessEffectUncertain("Owned process tree termination is not established");std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        if(WaitForSingleObject(process.value,0)!=WAIT_OBJECT_0)throw ProcessEffectUncertain("Owned process exit is not established");retired=true;
    }
    ~Child(){if(!retired && process.value){if(job.value)TerminateJobObject(job.value,1);TerminateProcess(process.value,1);WaitForSingleObject(process.value,5000);}}
};
bool drain(CapturePipe& pipe,bool error,ProcessResult& result,std::size_t limit,const ForegroundProcess::OutputObserver& observer) {
    if(pipe.eof)return false;DWORD available=0;
    if(!PeekNamedPipe(pipe.reader.value,nullptr,0,nullptr,&available,nullptr)) {
        if(GetLastError()==ERROR_BROKEN_PIPE){pipe.eof=true;return false;}fail("Cannot inspect process output");
    }
    if(!available)return false;
    std::array<char,8192> bytes{};DWORD read=0;
    // One reader owns this pipe. Read only bytes already reported available;
    // alternate channels each iteration so a full stderr cannot block stdout.
    if(!ReadFile(pipe.reader.value,bytes.data(),static_cast<DWORD>(std::min<std::size_t>(available,bytes.size())),&read,nullptr))fail("Cannot capture process output");
    if(!read)throw ProcessEffectUncertain("Available process output could not be captured");
    auto& count=error?result.stderr_count:result.stdout_count;count+=read;
    const auto retained=result.stdout_bytes.size()+result.stderr_bytes.size();const auto keep=std::min<std::size_t>(read,limit-retained);
    if(keep){auto& output=error?result.stderr_bytes:result.stdout_bytes;output.append(bytes.data(),keep);if(observer)observer(error,std::string_view(bytes.data(),keep));}
    if(keep<read)result.truncated=true;return true;
}
}
std::string ForegroundProcess::directory_identity(const std::string& path){return DirectoryLease(wide(path)).id();}
ProcessResult ForegroundProcess::run(const ProcessConfiguration& config,std::stop_token cancel,OutputObserver observer) {
    const auto started=Clock::now();
    if(config.timeout<std::chrono::milliseconds(1) || config.timeout>std::chrono::minutes(10) || config.output_limit<8192 || config.output_limit>4*1024*1024 || config.arguments.size()>64 || config.working_directory_id.empty())throw std::invalid_argument("Process configuration exceeds limits or lacks directory identity");
    if(cancel.stop_requested())throw ProcessCancelledBeforeDispatch("Process cancelled before dispatch");
    const auto executable=wide(config.executable),directory=wide(config.working_directory);
    if(!std::filesystem::path(executable).is_absolute() || !std::filesystem::is_regular_file(std::filesystem::path(executable)))throw std::invalid_argument("Process executable must be an explicit existing absolute path");
    std::wstring command=quote(executable);for(const auto& arg:config.arguments){command.push_back(L' ');command+=quote(wide(arg));if(command.size()>32766)throw std::invalid_argument("Process command line exceeds limits");}
    DirectoryLease lease(directory);if(lease.id()!=config.working_directory_id)throw ProcessBeforeDispatchError("Process working directory identity changed");
    auto env=environment(config);CapturePipe output,errors;
    SECURITY_ATTRIBUTES inherited{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&inherited,OPEN_EXISTING,0,nullptr));
    if(input.value==INVALID_HANDLE_VALUE)fail("Cannot create process EOF input");
    Child child;child.job=Handle(CreateJobObjectW(nullptr,nullptr));if(!child.job.value)fail("Cannot create process ownership job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(child.job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))fail("Cannot configure process ownership");
    HANDLE handles[]{input.value,output.writer.value,errors.writer.value};Attributes attributes(handles);
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=handles[0];startup.StartupInfo.hStdOutput=handles[1];startup.StartupInfo.hStdError=handles[2];startup.lpAttributeList=attributes.list;
    if(cancel.stop_requested())throw ProcessCancelledBeforeDispatch("Process cancelled before creation");
    PROCESS_INFORMATION created{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,env.data(),directory.c_str(),&startup.StartupInfo,&created))fail("Cannot create suspended process");
    child.process=Handle(created.hProcess);child.thread=Handle(created.hThread);
    if(!AssignProcessToJobObject(child.job.value,child.process.value))fail("Cannot assign process ownership");
    // Reopen the path while all ancestor handles deny rename/delete sharing.
    if(DirectoryLease(directory).id()!=config.working_directory_id)throw ProcessBeforeDispatchError("Process directory changed during suspended launch");
    output.writer.reset();errors.writer.reset();input.reset();
    if(cancel.stop_requested())throw ProcessCancelledBeforeDispatch("Process cancelled while suspended");
    if(Clock::now()>=started+config.timeout)throw ProcessBeforeDispatchError("Process deadline expired before dispatch");
    ProcessResult result{created.dwProcessId,0,ProcessTermination::exited,{},{},0,0,false,0};
    // This is the first point at which user code can execute.
    if(ResumeThread(child.thread.value)==static_cast<DWORD>(-1))fail("Cannot resume owned process");child.dispatched=true;child.thread.reset();
    try {
        for(;;) {
            const bool a=drain(output,false,result,config.output_limit,observer),b=drain(errors,true,result,config.output_limit,observer);
            const auto now=Clock::now();
            if(cancel.stop_requested() || now>=started+config.timeout) {
                result.termination=cancel.stop_requested()?ProcessTermination::cancelled:ProcessTermination::timed_out;child.terminate();break;
            }
            if(child.empty() && output.eof && errors.eof){child.retired=true;break;}
            if(!a && !b)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // A terminated tree may still have buffered pipe bytes. Drain under a
        // separate bounded cleanup deadline; never discard an unknown I/O fault.
        const auto cleanup=Clock::now()+std::chrono::seconds(5);
        while(!output.eof || !errors.eof){const bool a=drain(output,false,result,config.output_limit,observer),b=drain(errors,true,result,config.output_limit,observer);if(Clock::now()>=cleanup)throw ProcessEffectUncertain("Owned process output drain is not established");if(!a && !b)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        DWORD exit=0;if(WaitForSingleObject(child.process.value,5000)!=WAIT_OBJECT_0 || !GetExitCodeProcess(child.process.value,&exit))throw ProcessEffectUncertain("Actual process exit code is unavailable");result.exit_code=exit;
        result.elapsed_ms=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started).count();return result;
    }catch(const ProcessEffectUncertain&){throw;}
    catch(...){throw ProcessEffectUncertain("Process failed after dispatch; side effects are uncertain");}
}
}
