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
#include <limits>
#include <cstddef>
#include <utility>
#include <optional>
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
    std::array<unsigned char,32> secret{};
    std::uint64_t next_sequence=0;
    ~WorkerSession(){retire();}
    bool alive() const { return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT; }
    void stop_process() const { if(process)TerminateProcess(process,1); }
    void retire() {
        if(!process)return;
        if(WaitForSingleObject(process,0)==WAIT_TIMEOUT)TerminateProcess(process,1);
        WaitForSingleObject(process,5000);CloseHandle(process);process=nullptr;port=0;
        SecureZeroMemory(secret.data(),secret.size());next_sequence=0;
    }
};
struct WorkerHandle {
    HANDLE value=nullptr;
    explicit WorkerHandle(HANDLE handle=nullptr):value(handle){}
    ~WorkerHandle(){reset();}
    WorkerHandle(const WorkerHandle&)=delete;
    WorkerHandle& operator=(const WorkerHandle&)=delete;
    void reset(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}
};
struct WorkerAttributes {
    std::vector<std::byte> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST value=nullptr;
    explicit WorkerAttributes(HANDLE* handles) {
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);
        if(!size)throw std::runtime_error("Cannot size worker handle attributes");
        storage.resize(size);value=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(value,1,0,&size)){value=nullptr;throw std::runtime_error("Cannot initialize worker handle attributes");}
        if(!UpdateProcThreadAttribute(value,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,3*sizeof(HANDLE),nullptr,nullptr)){
            DeleteProcThreadAttributeList(value);value=nullptr;throw std::runtime_error("Cannot restrict worker handle inheritance");
        }
    }
    ~WorkerAttributes(){if(value)DeleteProcThreadAttributeList(value);}
};
std::uint32_t random_port() {
    std::uint32_t value=0;
    if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&value),sizeof(value),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)throw std::runtime_error("Cannot create agent worker endpoint");
    return 20000+(value%40000);
}
std::array<unsigned char,32> random_worker_secret() {
    std::array<unsigned char,32> result{};
    if(BCryptGenRandom(nullptr,result.data(),static_cast<ULONG>(result.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)
        throw std::runtime_error("Cannot create xMind worker capability");
    return result;
}
std::string hex(const unsigned char* bytes,std::size_t size) {
    constexpr char digits[]="0123456789abcdef";std::string result(size*2,'0');
    for(std::size_t i=0;i<size;++i){result[i*2]=digits[bytes[i]>>4];result[i*2+1]=digits[bytes[i]&15];}
    return result;
}
std::string worker_signature(const std::array<unsigned char,32>& secret,std::uint64_t sequence,const std::string& arguments) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::vector<unsigned char> object;
    DWORD object_size=0,hash_size=0,got=0;std::array<unsigned char,32> digest{};
    auto close=[&]{if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0 ||
       BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&got,0)<0 ||
       BCryptGetProperty(algorithm,BCRYPT_HASH_LENGTH,reinterpret_cast<PUCHAR>(&hash_size),sizeof(hash_size),&got,0)<0 || hash_size!=digest.size()){
        close();throw std::runtime_error("Cannot initialize xMind worker authentication");
    }
    object.resize(object_size);
    if(BCryptCreateHash(algorithm,&hash,object.data(),object_size,
            const_cast<PUCHAR>(secret.data()),static_cast<ULONG>(secret.size()),0)<0){close();throw std::runtime_error("Cannot create xMind worker authentication tag");}
    const auto message=std::to_string(sequence)+"\n"+arguments;
    if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(message.data())),static_cast<ULONG>(message.size()),0)<0 ||
       BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0){close();throw std::runtime_error("Cannot sign xMind worker request");}
    close();return hex(digest.data(),digest.size());
}
class NativeToolWorker {
    X::Runtime& runtime_;
    std::array<unsigned char,32> secret_;
    std::int64_t sequence_=0;
    std::mutex invocation_;
    std::string module_,function_;
    void authorize(std::int64_t sequence,const std::string& tag,const std::string& message){
        if(sequence_==std::numeric_limits<std::int64_t>::max()||sequence!=sequence_+1||tag.size()!=64)
            throw std::invalid_argument("Invalid private worker authentication");
        const auto expected=worker_signature(secret_,static_cast<std::uint64_t>(sequence),message);
        unsigned difference=0;for(std::size_t i=0;i<64;++i)difference|=static_cast<unsigned char>(tag[i])^static_cast<unsigned char>(expected[i]);
        if(difference)throw std::invalid_argument("Invalid private worker authentication");sequence_=sequence;
    }
public:
    NativeToolWorker(X::Runtime& runtime,const std::string& module,const std::string& function,const std::array<unsigned char,32>& secret)
        :runtime_(runtime),secret_(secret),module_(module),function_(function){
        X::Module application(runtime_,module_.c_str());
        if(!application[function_.c_str()].IsValid())throw std::invalid_argument("Trusted worker callable is unavailable");
    }
    ~NativeToolWorker(){SecureZeroMemory(secret_.data(),secret_.size());}
    std::int64_t Handshake(std::int64_t sequence,std::string tag){
        std::lock_guard lock(invocation_);authorize(sequence,tag,"xMind worker handshake v1");return 1;
    }
    std::string Invoke(std::int64_t sequence,std::string tag,std::string input){
        std::lock_guard lock(invocation_);
        if(input.size()>256*1024||!Json::parse(input).is_object())throw std::invalid_argument("Invalid private worker arguments");
        authorize(sequence,tag,input);X::Value arguments,result,output;
        X::Module json(runtime_,"json"),application(runtime_,module_.c_str());
        if(!json["loads"].Call(std::vector<X::Value>{X::Value(runtime_,input)},arguments)||
           !application[function_.c_str()].Call(std::vector<X::Value>{arguments},result)||
           !json["dumps"].Call(std::vector<X::Value>{result},output)||!output.IsString())
            throw XlangPythonExecutionError();
        auto bytes=output.ToString();validate_json_result(bytes);return bytes;
    }
    BEGIN_PACKAGE(NativeToolWorker)
        APISET().AddFunc<2>("handshake",&NativeToolWorker::Handshake);
        APISET().AddFunc<3>("invoke",&NativeToolWorker::Invoke);
    END_PACKAGE
};

void write_worker_secret(HANDLE pipe,const std::array<unsigned char,32>& secret) {
    std::size_t offset=0;
    while(offset<secret.size()){
        DWORD written=0;
        if(!WriteFile(pipe,secret.data()+offset,static_cast<DWORD>(secret.size()-offset),&written,nullptr)||!written)
            throw std::runtime_error("Cannot deliver private xMind worker capability");
        offset+=written;
    }
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
    for(int attempt=0;attempt<8;++attempt){
        const auto port=random_port();std::wstring command=quote(binary.wstring())+L" worker";
        for(const auto& [key,value]:std::vector<std::pair<std::string,std::string>>{{"--agent-port",std::to_string(port)},{"--modules",modules_root},{"--stdlib",stdlib_root},{"--code-root",code_root},{"--module",module},{"--function",function}})command+=L" "+quote(wide(key))+L" "+quote(wide(value));
        command.push_back(L'\0');
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};HANDLE read_pipe=nullptr,write_pipe=nullptr;
        if(!CreatePipe(&read_pipe,&write_pipe,&security,0))throw std::runtime_error("Cannot create private xMind worker channel");
        WorkerHandle child_input(read_pipe),parent_output(write_pipe);
        if(!SetHandleInformation(parent_output.value,HANDLE_FLAG_INHERIT,0))throw std::runtime_error("Cannot protect xMind worker channel");
        WorkerHandle child_stdout(CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));
        WorkerHandle child_stderr(CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));
        if(child_stdout.value==INVALID_HANDLE_VALUE||child_stderr.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create private worker output handles");
        HANDLE inherited[]{child_input.value,child_stdout.value,child_stderr.value};WorkerAttributes attributes(inherited);
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=inherited[0];startup.StartupInfo.hStdOutput=inherited[1];startup.StartupInfo.hStdError=inherited[2];startup.lpAttributeList=attributes.value;
        PROCESS_INFORMATION created{};auto environment=worker_environment();auto secret=random_worker_secret();
        if(!CreateProcessW(binary.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,environment.data(),binary.parent_path().c_str(),&startup.StartupInfo,&created))throw std::runtime_error("Cannot start xMind agent worker");
        CloseHandle(created.hThread);session.process=created.hProcess;session.port=port;session.secret=secret;session.next_sequence=0;child_input.reset();child_stdout.reset();child_stderr.reset();
        try{write_worker_secret(parent_output.value,secret);parent_output.reset();}
        catch(...){parent_output.reset();SecureZeroMemory(secret.data(),secret.size());session.retire();throw;}
        SecureZeroMemory(secret.data(),secret.size());
        X::Runtime probe;const std::string endpoint="lrpc:"+std::to_string(port);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(std::chrono::steady_clock::now()<deadline){
            if(WaitForSingleObject(session.process,0)==WAIT_OBJECT_0){session.retire();break;}
            X3LrpcEndpointInfo info{};info.size=sizeof(info);
            if(x3_runtime_probe_remote(probe.get(),endpoint.c_str(),100,&info)==X3_STATUS_OK&&info.pid==created.dwProcessId){
                auto remote=probe.ImportRemote("xmind_agent_worker",endpoint.c_str());
                const auto invoke=remote["invoke"];X::Value ignored;
                const std::vector<X::Value> unauthenticated{X::Value(probe,std::int64_t{1}),X::Value(probe,""),X::Value(probe,"{}")};
                if(invoke.Call(unauthenticated,ignored))throw std::runtime_error("xMind worker accepted an unauthenticated request");
                const auto handshake=remote["handshake"];const std::string challenge="xMind worker handshake v1";
                const auto signature=worker_signature(session.secret,1,challenge);
                const std::vector<X::Value> handshake_args{X::Value(probe,std::int64_t{1}),X::Value(probe,signature)};X::Value accepted;
                if(!handshake.Call(handshake_args,accepted)||accepted.ToInt64()!=1)throw std::runtime_error("xMind worker capability handshake failed");
                X::Value replayed;
                if(handshake.Call(handshake_args,replayed))throw std::runtime_error("xMind worker accepted a replayed request");
                session.next_sequence=1;
                return;
            }
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
    std::array<unsigned char,32> secret{};std::size_t offset=0;const auto input=GetStdHandle(STD_INPUT_HANDLE);
    while(offset<secret.size()){
        DWORD count=0;if(input==nullptr||input==INVALID_HANDLE_VALUE||!ReadFile(input,secret.data()+offset,static_cast<DWORD>(secret.size()-offset),&count,nullptr)||!count)
            throw std::runtime_error("xMind worker did not receive its private capability");
        offset+=count;
    }
    // The borrowed C++ service outlives the runtime; it retains no SDK values.
    std::optional<NativeToolWorker> worker;X::Runtime runtime;
    runtime.AddImportRoot(modules);runtime.AddImportRoot(stdlib);runtime.AddImportRoot(code_root);
    worker.emplace(runtime,module_name,function_name,secret);SecureZeroMemory(secret.data(),secret.size());
    auto service=runtime.RegisterPackage("xmind_agent_worker",*worker);
    runtime.RegisterRemoteObject("xmind_agent_worker",service);runtime.LrpcListen(port);
    for(;;)std::this_thread::sleep_for(std::chrono::seconds(1));
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
            if(session->next_sequence>=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())){session->retire();throw std::runtime_error("xMind worker request sequence exhausted");}
            const auto sequence=++session->next_sequence;const auto payload=arguments.dump();const auto signature=worker_signature(session->secret,sequence,payload);
            const std::vector<X::Value> args{X::Value(probe,static_cast<std::int64_t>(sequence)),X::Value(probe,signature),X::Value(probe,payload)};
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
