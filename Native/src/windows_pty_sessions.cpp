#include "agentflow/pty_sessions.hpp"
#include "agentflow/process.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace agentflow {
namespace {
constexpr std::size_t output_capacity=1024*1024;
constexpr std::size_t input_limit=64*1024;
constexpr std::size_t max_sessions=64;
constexpr std::size_t max_session_count=8;
using CreatePseudoConsoleFn=HRESULT (WINAPI*)(COORD,HANDLE,HANDLE,DWORD,HPCON*);
using ResizePseudoConsoleFn=HRESULT (WINAPI*)(HPCON,COORD);
using ClosePseudoConsoleFn=void (WINAPI*)(HPCON);
struct PseudoConsoleApi {
    CreatePseudoConsoleFn create=nullptr;ResizePseudoConsoleFn resize=nullptr;ClosePseudoConsoleFn close=nullptr;
    PseudoConsoleApi(){const auto module=GetModuleHandleW(L"kernel32.dll");if(!module)return;create=reinterpret_cast<CreatePseudoConsoleFn>(GetProcAddress(module,"CreatePseudoConsole"));resize=reinterpret_cast<ResizePseudoConsoleFn>(GetProcAddress(module,"ResizePseudoConsole"));close=reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(module,"ClosePseudoConsole"));}
    bool available()const{return create&&resize&&close;}
};
const PseudoConsoleApi& api(){static const PseudoConsoleApi value;return value;}
struct Handle {
    HANDLE value=nullptr;explicit Handle(HANDLE handle=nullptr):value(handle){}
    ~Handle(){reset();}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other)noexcept:value(std::exchange(other.value,nullptr)){}
    Handle& operator=(Handle&& other)noexcept{if(this!=&other){reset();value=std::exchange(other.value,nullptr);}return *this;}
    void reset(HANDLE next=nullptr)noexcept{if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=next;}
};
[[noreturn]] void fail(const char* operation){throw std::runtime_error(std::string(operation)+" (Windows error "+std::to_string(GetLastError())+")");}
std::wstring wide(const std::string& value){if(value.size()>32768||value.find('\0')!=std::string::npos)throw std::invalid_argument("Terminal path or title exceeds limits");if(value.empty())return {};const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);if(!size)throw std::invalid_argument("Terminal path must be UTF-8");std::wstring result(size,L'\0');if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size))throw std::invalid_argument("Terminal path must be UTF-8");return result;}
std::wstring quote(const std::wstring& value){std::wstring result=L"\"";std::size_t slashes=0;for(const auto c:value){if(c==L'\\'){++slashes;continue;}result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result.push_back(c);}result.append(slashes*2,L'\\');result.push_back(L'\"');return result;}
std::string utf8(const std::filesystem::path& path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
bool valid_identity(const std::string& value){return !value.empty()&&value.size()<=96&&value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") == std::string::npos;}
std::string new_identity(){std::array<UCHAR,16> bytes{};if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot create terminal identity");static constexpr char digits[]="0123456789abcdef";std::string value;value.reserve(32);for(const auto byte:bytes){value.push_back(digits[byte>>4]);value.push_back(digits[byte&15]);}return value;}
struct AttributeList {
    std::vector<std::byte> storage;LPPROC_THREAD_ATTRIBUTE_LIST value=nullptr;
    explicit AttributeList(HPCON pseudo){SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);if(!size)fail("Cannot size terminal process attributes");storage.resize(size);value=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());if(!InitializeProcThreadAttributeList(value,1,0,&size)){value=nullptr;fail("Cannot initialize terminal process attributes");}if(!UpdateProcThreadAttribute(value,0,PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,pseudo,sizeof(pseudo),nullptr,nullptr)){DeleteProcThreadAttributeList(value);value=nullptr;fail("Cannot bind terminal pseudoconsole");}}
    ~AttributeList(){if(value)DeleteProcThreadAttributeList(value);}
};
void dimensions(std::uint16_t rows,std::uint16_t columns){if(rows<5||rows>300||columns<20||columns>500)throw std::invalid_argument("Terminal size must be between 20x5 and 500x300");}
}

struct PtySessions::Impl {
    struct Terminal {
        std::string id,session_id,title;std::uint16_t rows=24,columns=80;
        Handle job,process,input;HPCON pseudo=nullptr;
        mutable std::mutex mutex;std::mutex input_mutex;std::mutex pseudo_mutex;std::condition_variable changed;
        std::string output;std::uint64_t base_sequence=0;bool running=true;bool reader_done=false;std::uint32_t exit_code=0;
        std::thread reader,monitor;
        void append(const char* data,std::size_t size){std::lock_guard lock(mutex);if(size>output_capacity){data+=size-output_capacity;size=output_capacity;base_sequence+=output.size()+size-output_capacity;output.clear();}const auto excess=output.size()+size>output_capacity?output.size()+size-output_capacity:0;if(excess){output.erase(0,excess);base_sequence+=excess;}output.append(data,size);changed.notify_all();}
        void finish_reader(){std::lock_guard lock(mutex);reader_done=true;changed.notify_all();}
        void read_loop(Handle pipe){std::array<char,8192> bytes{};for(;;){DWORD count=0;if(!ReadFile(pipe.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)){const auto error=GetLastError();if(error!=ERROR_BROKEN_PIPE&&error!=ERROR_PIPE_NOT_CONNECTED&&error!=ERROR_OPERATION_ABORTED){std::lock_guard lock(mutex);running=false;changed.notify_all();}break;}if(!count)break;append(bytes.data(),count);}finish_reader();}
        void close_console()noexcept{HPCON handle=nullptr;{std::lock_guard lock(pseudo_mutex);handle=std::exchange(pseudo,nullptr);}if(handle&&api().close)api().close(handle);}
        void monitor_loop(){WaitForSingleObject(process.value,INFINITE);DWORD code=1;GetExitCodeProcess(process.value,&code);{std::lock_guard lock(mutex);running=false;exit_code=code;changed.notify_all();}close_console();}
        PtySessionMetadata metadata()const{std::lock_guard lock(mutex);return {id,session_id,title,rows,columns,running,exit_code,base_sequence+output.size()};}
        void stop()noexcept{
            if(job.value&&process.value&&WaitForSingleObject(process.value,0)!=WAIT_OBJECT_0)TerminateJobObject(job.value,1);
            if(process.value)WaitForSingleObject(process.value,5000);
            close_console();input.reset();
            if(monitor.joinable()&&monitor.get_id()!=std::this_thread::get_id())monitor.join();
            if(reader.joinable()&&reader.get_id()!=std::this_thread::get_id())reader.join();
            {std::lock_guard lock(mutex);running=false;changed.notify_all();}
        }
        ~Terminal(){stop();}
    };

    std::string root,shell;std::string root_identity,shell_identity;mutable std::mutex mutex;std::map<std::string,std::shared_ptr<Terminal>> terminals;
    Impl(std::string workspace,std::string executable){
        if(!api().available())throw std::runtime_error("Windows ConPTY is unavailable on this system");
        auto canonical_root=std::filesystem::canonical(std::filesystem::u8path(workspace));auto canonical_shell=std::filesystem::canonical(std::filesystem::u8path(executable));
        if(!std::filesystem::is_directory(canonical_root)||!std::filesystem::is_regular_file(canonical_shell))throw std::invalid_argument("Terminal workspace or shell is unavailable");
        const auto name=canonical_shell.filename().wstring();if(CompareStringOrdinal(name.data(),static_cast<int>(name.size()),L"powershell.exe",13,TRUE)!=CSTR_EQUAL&&CompareStringOrdinal(name.data(),static_cast<int>(name.size()),L"pwsh.exe",8,TRUE)!=CSTR_EQUAL&&CompareStringOrdinal(name.data(),static_cast<int>(name.size()),L"cmd.exe",7,TRUE)!=CSTR_EQUAL)throw std::invalid_argument("Terminal shell must be PowerShell, pwsh or cmd");
        root=utf8(canonical_root);shell=utf8(canonical_shell);root_identity=ForegroundProcess::directory_identity(root);shell_identity=ForegroundProcess::executable_identity(shell);
    }
    std::shared_ptr<Terminal> require(const std::string& id)const{std::lock_guard lock(mutex);const auto found=terminals.find(id);if(found==terminals.end())throw std::out_of_range("Terminal session not found");return found->second;}
    std::shared_ptr<Terminal> launch(const std::string& session,const std::string& title,std::uint16_t rows,std::uint16_t columns){
        if(!valid_identity(session))throw std::invalid_argument("Invalid conversation identity");if(title.size()>128||title.find('\0')!=std::string::npos)throw std::invalid_argument("Terminal title exceeds limits");dimensions(rows,columns);
        if(ForegroundProcess::directory_identity(root)!=root_identity||ForegroundProcess::executable_identity(shell)!=shell_identity)throw std::runtime_error("Terminal workspace or shell identity changed");
        auto terminal=std::make_shared<Terminal>();terminal->id=new_identity();terminal->session_id=session;terminal->title=title.empty()?"Terminal":title;terminal->rows=rows;terminal->columns=columns;
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};Handle input_read,input_write,output_read,output_write;
        HANDLE a=nullptr,b=nullptr;if(!CreatePipe(&a,&b,&security,0))fail("Cannot create terminal input pipe");input_read.reset(a);input_write.reset(b);
        if(!SetHandleInformation(input_write.value,HANDLE_FLAG_INHERIT,0))fail("Cannot isolate terminal input handle");
        a=nullptr;b=nullptr;if(!CreatePipe(&a,&b,&security,0))fail("Cannot create terminal output pipe");output_read.reset(a);output_write.reset(b);
        if(!SetHandleInformation(output_read.value,HANDLE_FLAG_INHERIT,0))fail("Cannot isolate terminal output handle");
        HRESULT result=api().create(COORD{static_cast<SHORT>(columns),static_cast<SHORT>(rows)},input_read.value,output_write.value,0,&terminal->pseudo);if(FAILED(result))throw std::runtime_error("Cannot create Windows pseudoconsole (HRESULT "+std::to_string(static_cast<unsigned long>(result))+")");
        input_read.reset();output_write.reset();AttributeList attributes(terminal->pseudo);STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=nullptr;startup.StartupInfo.hStdOutput=nullptr;startup.StartupInfo.hStdError=nullptr;startup.lpAttributeList=attributes.value;
        const auto shell_wide=wide(shell);const auto base=std::filesystem::u8path(shell).filename().wstring();const bool command=CompareStringOrdinal(base.data(),static_cast<int>(base.size()),L"cmd.exe",7,TRUE)==CSTR_EQUAL;
        std::wstring command_line=quote(shell_wide)+(command?L" /D":L" -NoLogo -NoExit");std::vector<wchar_t> mutable_line(command_line.begin(),command_line.end());mutable_line.push_back(L'\0');
        terminal->job=Handle(CreateJobObjectW(nullptr,nullptr));if(!terminal->job.value)fail("Cannot create terminal process job");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(!SetInformationJobObject(terminal->job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))fail("Cannot bound terminal process lifetime");
        PROCESS_INFORMATION created{};const auto cwd=wide(root);if(!CreateProcessW(nullptr,mutable_line.data(),nullptr,nullptr,FALSE,EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT|CREATE_SUSPENDED|CREATE_NEW_PROCESS_GROUP,nullptr,cwd.c_str(),&startup.StartupInfo,&created))fail("Cannot create terminal shell");
        terminal->process.reset(created.hProcess);Handle thread(created.hThread);
        if(!AssignProcessToJobObject(terminal->job.value,terminal->process.value))fail("Cannot assign terminal process ownership");
        if(ForegroundProcess::directory_identity(root)!=root_identity||ForegroundProcess::executable_identity(shell)!=shell_identity)throw std::runtime_error("Terminal workspace or shell changed during launch");
        terminal->input=std::move(input_write);
        terminal->reader=std::thread([owner=terminal.get(),pipe=std::move(output_read)]()mutable{owner->read_loop(std::move(pipe));});
        if(ResumeThread(thread.value)==static_cast<DWORD>(-1))fail("Cannot resume terminal shell");
        terminal->monitor=std::thread([owner=terminal.get()]{owner->monitor_loop();});
        return terminal;
    }
};

PtySessions::PtySessions(std::string root,std::string shell):impl_(std::make_unique<Impl>(std::move(root),std::move(shell))){}
PtySessions::~PtySessions()=default;
PtySessionMetadata PtySessions::create(const std::string& session,const std::string& title,std::uint16_t rows,std::uint16_t columns){
    std::lock_guard lock(impl_->mutex);if(impl_->terminals.size()>=max_sessions)throw std::runtime_error("Terminal session limit reached");const auto count=std::count_if(impl_->terminals.begin(),impl_->terminals.end(),[&](const auto& entry){return entry.second->session_id==session;});if(static_cast<std::size_t>(count)>=max_session_count)throw std::runtime_error("Conversation terminal limit reached");auto terminal=impl_->launch(session,title,rows,columns);if(impl_->terminals.contains(terminal->id))throw std::runtime_error("Terminal identity collision");impl_->terminals.emplace(terminal->id,terminal);return terminal->metadata();
}
std::vector<PtySessionMetadata> PtySessions::list(const std::string& session)const{if(!valid_identity(session))throw std::invalid_argument("Invalid conversation identity");std::vector<PtySessionMetadata> result;std::lock_guard lock(impl_->mutex);for(const auto& [id,terminal]:impl_->terminals){(void)id;if(terminal->session_id==session)result.push_back(terminal->metadata());}return result;}
PtySessionMetadata PtySessions::get(const std::string& id)const{return impl_->require(id)->metadata();}
PtyOutputChunk PtySessions::read(const std::string& id,std::uint64_t after,std::size_t limit,std::chrono::milliseconds wait){if(limit==0||limit>64*1024||wait.count()<0||wait>std::chrono::seconds(25))throw std::invalid_argument("Terminal output cursor, limit or wait exceeds bounds");const auto terminal=impl_->require(id);std::unique_lock lock(terminal->mutex);const auto initial_end=terminal->base_sequence+terminal->output.size();if(after>initial_end)throw std::invalid_argument("Terminal output cursor is ahead of the stream");terminal->changed.wait_for(lock,wait,[&]{return terminal->base_sequence+terminal->output.size()>after||!terminal->running||terminal->reader_done;});bool truncated=after<terminal->base_sequence;if(truncated)after=terminal->base_sequence;const auto offset=static_cast<std::size_t>(after>=terminal->base_sequence?after-terminal->base_sequence:0);const auto amount=std::min(limit,terminal->output.size()-offset);return {terminal->output.substr(offset,amount),terminal->base_sequence+offset+amount,truncated,terminal->running,terminal->exit_code};}
void PtySessions::write(const std::string& id,const std::string& utf8){if(utf8.empty()||utf8.size()>input_limit||utf8.find('\0')!=std::string::npos||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),static_cast<int>(utf8.size()),nullptr,0))throw std::invalid_argument("Terminal input must contain 1-65536 non-NUL UTF-8 bytes");const auto terminal=impl_->require(id);std::lock_guard serial(terminal->input_mutex);if(!terminal->running||!terminal->input.value)throw std::runtime_error("Terminal session has exited");std::size_t offset=0;while(offset<utf8.size()){DWORD written=0;const auto request=static_cast<DWORD>(std::min<std::size_t>(utf8.size()-offset,32768));if(!WriteFile(terminal->input.value,utf8.data()+offset,request,&written,nullptr)||!written)throw std::runtime_error("Terminal input could not be written");offset+=written;}}
PtySessionMetadata PtySessions::resize(const std::string& id,std::uint16_t rows,std::uint16_t columns){dimensions(rows,columns);const auto terminal=impl_->require(id);std::lock_guard serial(terminal->pseudo_mutex);if(!terminal->pseudo||!terminal->running)throw std::runtime_error("Terminal session has exited");const auto result=api().resize(terminal->pseudo,COORD{static_cast<SHORT>(columns),static_cast<SHORT>(rows)});if(FAILED(result))throw std::runtime_error("Windows pseudoconsole resize failed");{std::lock_guard lock(terminal->mutex);terminal->rows=rows;terminal->columns=columns;}return terminal->metadata();}
void PtySessions::close(const std::string& id){std::shared_ptr<Impl::Terminal> terminal;{std::lock_guard lock(impl_->mutex);const auto found=impl_->terminals.find(id);if(found==impl_->terminals.end())throw std::out_of_range("Terminal session not found");terminal=std::move(found->second);impl_->terminals.erase(found);}terminal->stop();}

}
