#include "agentflow/agent_worker.hpp"
#include "agentflow/xlang_python.hpp"
#include "xlang3/cpp/runtime.h"
#include "xlang3/abi/xapi.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <array>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <exception>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#endif

namespace agentflow {
namespace {
using Json=nlohmann::json;
std::string path_text(const std::filesystem::path& path) {
    const auto value=path.u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}
std::filesystem::path executable_path() {
#if defined(_WIN32)
    std::wstring path(32768,L'\0');
    const auto size=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!size||size>=path.size())throw std::runtime_error("Cannot resolve xmind worker executable");
    path.resize(size);return std::filesystem::path(path);
#else
    throw std::runtime_error("Out-of-process xlang3 workers are supported on Windows only");
#endif
}
std::string required_arg(int argc,char** argv,const char* name) {
    std::string result;
    for(int i=1;i+1<argc;++i)if(std::string_view(argv[i])==name){if(!result.empty())throw std::invalid_argument("Duplicate worker option");result=argv[i+1];}
    if(result.empty()||result.find('\0')!=std::string::npos)throw std::invalid_argument("Missing agent worker option");
    return result;
}
std::uint32_t port_arg(const std::string& value) {
    std::uint32_t port=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),port);
    if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||port<1||port>65535)throw std::invalid_argument("Invalid agent worker port");return port;
}
void validate_json_result(const std::string& value) {
    if(value.empty()||value.size()>1024*1024)throw std::runtime_error("Agent worker result exceeds limit");
    const auto parsed=Json::parse(value);(void)parsed;
}
#if defined(_WIN32)
std::wstring wide(const std::string& value) {
    if(value.empty()||value.size()>32768||value.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid worker launch argument");
    const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!size)throw std::invalid_argument("Worker launch arguments must be UTF-8");
    std::wstring result(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size);return result;
}
std::wstring quote(const std::wstring& value) {
    std::wstring result=L"\"";std::size_t slashes=0;
    for(wchar_t c:value){if(c==L'\\'){++slashes;continue;}result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result.push_back(c);}
    result.append(slashes*2,L'\\');result.push_back(L'\"');return result;
}
struct WorkerSession {
    std::mutex invocation;
    HANDLE process=nullptr;
    std::uint32_t port=0;
    ~WorkerSession(){retire();}
    bool alive() const { return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT; }
    void stop_process() const { if(process)TerminateProcess(process,1); }
    void retire() {
        if(!process)return;
        if(WaitForSingleObject(process,0)==WAIT_TIMEOUT)TerminateProcess(process,1);
        WaitForSingleObject(process,5000);CloseHandle(process);process=nullptr;port=0;
    }
};
std::uint32_t random_port() {
    std::uint32_t value=0;
    if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&value),sizeof(value),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)throw std::runtime_error("Cannot create agent worker endpoint");
    return 20000+(value%40000);
}
std::vector<wchar_t> worker_environment() {
    std::map<std::wstring,std::wstring> variables;
    std::array<wchar_t,32768> path{};
    const auto windows_size=GetWindowsDirectoryW(path.data(),static_cast<UINT>(path.size()));
    if(!windows_size||windows_size>=path.size())throw std::runtime_error("Cannot create private worker environment");
    variables[L"SystemRoot"]=std::wstring(path.data(),windows_size);
    const auto temporary_size=GetTempPathW(static_cast<DWORD>(path.size()),path.data());
    if(!temporary_size||temporary_size>=path.size())throw std::runtime_error("Cannot create private worker environment");
    variables[L"TEMP"]=std::wstring(path.data(),temporary_size);variables[L"TMP"]=variables[L"TEMP"];
    std::vector<wchar_t> result;
    for(const auto& [key,value]:variables){result.insert(result.end(),key.begin(),key.end());result.push_back(L'=');result.insert(result.end(),value.begin(),value.end());result.push_back(L'\0');}
    result.push_back(L'\0');return result;
}
std::string worker_key(const std::string& modules,const std::string& stdlib,const std::string& code_root,
    const std::string& module,const std::string& function,const std::string& executable) {
    std::string key;
    for(const auto* value:{&modules,&stdlib,&code_root,&module,&function,&executable}){
        key+=std::to_string(value->size());key.push_back(':');key+=*value;key.push_back('\0');
    }
    return key;
}
std::shared_ptr<WorkerSession> worker_session(const std::string& key) {
    static std::mutex mutex;
    static std::unordered_map<std::string,std::shared_ptr<WorkerSession>> sessions;
    std::lock_guard lock(mutex);
    if(const auto found=sessions.find(key);found!=sessions.end())return found->second;
    constexpr std::size_t max_sessions=16;
    if(sessions.size()>=max_sessions){
        const auto idle=std::find_if(sessions.begin(),sessions.end(),[](const auto& entry){return entry.second.use_count()==1;});
        if(idle==sessions.end())throw std::runtime_error("xMind agent worker capacity reached");
        idle->second->retire();sessions.erase(idle);
    }
    auto session=std::make_shared<WorkerSession>();sessions.emplace(key,session);return session;
}
void start_worker(WorkerSession& session,const std::filesystem::path& binary,
    const std::string& modules_root,const std::string& stdlib_root,const std::string& code_root,
    const std::string& module,const std::string& function) {
    const auto script=binary.parent_path()/L"worker"/L"agent_worker.py";
    if(!std::filesystem::is_regular_file(script))throw std::runtime_error("The packaged xMind agent worker script is missing");
    for(int attempt=0;attempt<8;++attempt){
        const auto port=random_port();std::wstring command=quote(binary.wstring())+L" worker";
        for(const auto& [key,value]:std::vector<std::pair<std::string,std::string>>{{"--agent-port",std::to_string(port)},{"--modules",modules_root},{"--stdlib",stdlib_root},{"--code-root",code_root},{"--module",module},{"--function",function}})command+=L" "+quote(wide(key))+L" "+quote(wide(value));
        command.push_back(L'\0');STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION created{};auto environment=worker_environment();
        if(!CreateProcessW(binary.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,environment.data(),binary.parent_path().c_str(),&startup,&created))throw std::runtime_error("Cannot start xMind agent worker");
        CloseHandle(created.hThread);session.process=created.hProcess;session.port=port;
        X::Runtime probe;const std::string endpoint="lrpc:"+std::to_string(port);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(std::chrono::steady_clock::now()<deadline){
            if(WaitForSingleObject(session.process,0)==WAIT_OBJECT_0){session.retire();break;}
            X3LrpcEndpointInfo info{};info.size=sizeof(info);
            if(x3_runtime_probe_remote(probe.get(),endpoint.c_str(),100,&info)==X3_STATUS_OK&&info.pid==created.dwProcessId)return;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        session.retire();
    }
    throw std::runtime_error("xMind agent worker did not become ready");
}
#endif
}

int run_agent_worker(int argc,char** argv) {
    if(argc!=13)throw std::invalid_argument("Invalid private agent worker invocation");
    const auto port=port_arg(required_arg(argc,argv,"--agent-port"));
    const auto modules=required_arg(argc,argv,"--modules");
    const auto stdlib=required_arg(argc,argv,"--stdlib");
    const auto code_root=required_arg(argc,argv,"--code-root");
    const auto module_name=required_arg(argc,argv,"--module");const auto function_name=required_arg(argc,argv,"--function");
    X::Runtime runtime;runtime.AddImportRoot(modules);runtime.AddImportRoot(stdlib);runtime.AddImportRoot(code_root);
    const auto script=executable_path().parent_path()/L"worker"/L"agent_worker.py";
    if(!std::filesystem::is_regular_file(script))throw std::runtime_error("The packaged xMind agent worker script is missing");
    runtime.AddImportRoot(path_text(script.parent_path()));
    X::Module worker(runtime,"agent_worker");const auto start=worker["start"];X::Value result;
    const std::vector<X::Value> args{X::Value(runtime,static_cast<std::int64_t>(port)),X::Value(runtime,module_name),X::Value(runtime,function_name)};
    if(!start.Call(args,result))throw std::runtime_error("Cannot start xMind xlang3 agent worker");
    return 0;
}

std::string XlangPythonCallable::invoke_json_worker(const std::string& arguments_json,std::stop_token cancel) const {
    return invoke_xlang_python_worker(modules_root_,stdlib_root_,code_root_,module_,function_,arguments_json,cancel,worker_executable_);
}

std::string invoke_xlang_python_worker(const std::string& modules_root,const std::string& stdlib_root,
    const std::string& code_root,const std::string& module,const std::string& function,
    const std::string& arguments_json,std::stop_token cancel,const std::string& worker_executable) {
    if(arguments_json.size()>256*1024)throw std::invalid_argument("xlang3 tool arguments exceed limits");
    const auto arguments=Json::parse(arguments_json);if(!arguments.is_object())throw std::invalid_argument("xlang3 tool arguments must be an object");
    if(cancel.stop_requested())throw XlangPythonCancelled();
#if defined(_WIN32)
    const auto binary=worker_executable.empty()?executable_path():std::filesystem::u8path(worker_executable);
    auto session=worker_session(worker_key(modules_root,stdlib_root,code_root,module,function,path_text(binary)));
    std::unique_lock invocation(session->invocation);
    if(cancel.stop_requested())throw XlangPythonCancelled();
    if(!session->alive()){session->retire();start_worker(*session,binary,modules_root,stdlib_root,code_root,module,function);}
    std::string serialized;std::exception_ptr failure;
    {
        std::stop_callback stop(cancel,[session]{session->stop_process();});
        try{
            X::Runtime probe;const std::string endpoint="lrpc:"+std::to_string(session->port);
            auto remote=probe.ImportRemote("xmind_agent_worker",endpoint.c_str());const auto invoke=remote["invoke"];X::Value output;
            const std::vector<X::Value> args{X::Value(probe,arguments.dump())};
            if(!invoke.Call(args,output)||!output.IsString())throw std::runtime_error("xMind agent worker returned an invalid result");
            serialized=output.ToString();validate_json_result(serialized);
        }catch(...){failure=std::current_exception();}
    }
    if(cancel.stop_requested()){session->retire();throw XlangPythonCancelled();}
    if(failure){session->retire();std::rethrow_exception(failure);}
    return serialized;
#else
    (void)modules_root;(void)stdlib_root;(void)code_root;(void)module;(void)function;
    throw std::runtime_error("Out-of-process xlang3 workers are supported on Windows only");
#endif
}
}
