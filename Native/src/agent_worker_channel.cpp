#include "agentflow/agent_worker_channel.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/agent_worker_native_links.hpp"
#include <optional>
#include <span>
#include "xlang3/cpp/runtime.h"
#include <condition_variable>
#include <cstring>
#include <map>
#include <thread>
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace agentflow {
namespace {
using Clock=std::chrono::steady_clock;
struct Handle {
    HANDLE value=nullptr;
    explicit Handle(HANDLE handle=nullptr):value(handle){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other)noexcept:value(std::exchange(other.value,nullptr)){}
    Handle& operator=(Handle&& other)noexcept{if(this!=&other){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=std::exchange(other.value,nullptr);}return *this;}
};
[[noreturn]] void protocol(){throw AgentWorkerProtocolError("Agent worker link rejected its message");}
void check(Clock::time_point deadline,std::stop_token stop){
    if(stop.stop_requested())throw AgentWorkerChannelCancelled("Agent worker channel cancelled");
    if(Clock::now()>=deadline)throw AgentWorkerChannelTimeout("Agent worker channel deadline elapsed");
}
void identifier(const std::string& value){
    if(value.empty()||value.size()>256)protocol();for(unsigned char c:value)if(c<33||c>126)protocol();
}
void validate_scope(const AgentWorkerScope& scope){
    identifier(scope.root_id);identifier(scope.run_id);identifier(scope.workspace_id);identifier(scope.generation);
    if(scope.workspace_revision<1||scope.workspace_revision>9007199254740991)protocol();
}
std::string hex(std::span<const std::uint8_t> value){
    constexpr char digits[]="0123456789abcdef";std::string out(value.size()*2,'0');for(std::size_t i=0;i<value.size();++i){out[i*2]=digits[value[i]>>4];out[i*2+1]=digits[value[i]&15];}return out;
}
std::wstring wide(const std::string& text){
    if(text.empty()||text.size()>32768||text.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid worker image path");
    const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);if(!size)throw std::invalid_argument("Invalid worker image path");
    std::wstring out(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),size);return out;
}
std::wstring quote(const std::wstring& text){
    std::wstring out=L"\"";std::size_t slashes=0;for(wchar_t c:text){if(c==L'\\'){++slashes;continue;}out.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;out.push_back(c);}out.append(slashes*2,L'\\');out.push_back(L'\"');return out;
}
// Native C++ remote objects use the SDK's shared-memory LRPC transport.
// A rendezvous accepts one message only while its owning reader is waiting.
// Tool execution never runs in an IPC callback, so callbacks stay bounded.
struct WorkerProcessReady {std::uint32_t pid=0,parent_pid=0,port=0;std::uint64_t created=0;};
struct Inbox {
    AgentWorkerScope scope;
    std::mutex mutex;std::condition_variable changed;bool closed=false;
    std::optional<AgentWorkerStartup> startup;
    std::optional<AgentWorkerServiceAccess> execution;
    bool startup_received=false,execution_received=false;
    std::optional<WorkerProcessReady> process_ready;bool process_ready_received=false;
};
struct WorkerScopeApi {
    AgentWorkerScope scope;
    std::string RootId(){return scope.root_id;}
    std::string RunId(){return scope.run_id;}
    std::string WorkspaceId(){return scope.workspace_id;}
    std::string Generation(){return scope.generation;}
    std::int64_t WorkspaceRevision(){return scope.workspace_revision;}
    BEGIN_PACKAGE(WorkerScopeApi)
        APISET().AddFunc<0>("RootId",&WorkerScopeApi::RootId);
        APISET().AddFunc<0>("RunId",&WorkerScopeApi::RunId);
        APISET().AddFunc<0>("WorkspaceId",&WorkerScopeApi::WorkspaceId);
        APISET().AddFunc<0>("Generation",&WorkerScopeApi::Generation);
        APISET().AddFunc<0>("WorkspaceRevision",&WorkerScopeApi::WorkspaceRevision);
    END_PACKAGE
};
void validate_service_access(const AgentWorkerServiceAccess& access){
    if(!access.endpoint.starts_with("lrpc:")||access.endpoint.size()>32||!access.pid||access.ticket.size()!=64||access.ticket.find_first_not_of("0123456789abcdef")!=std::string::npos)protocol();
}
template<class T,class Live>
T await_startup_value(Inbox& inbox,std::optional<T> Inbox::*slot,bool Inbox::*consumed,Clock::time_point deadline,std::stop_token stop,Live live){
    std::unique_lock lock(inbox.mutex);if(inbox.*consumed)protocol();
    for(;;){check(deadline,stop);if(inbox.closed)protocol();
        if(inbox.*slot){auto value=std::move(*(inbox.*slot));(inbox.*slot).reset();inbox.*consumed=true;return value;}
        live();inbox.changed.wait_for(lock,std::chrono::milliseconds(5));
    }
}
struct WorkerLinkApi {
    std::shared_ptr<Inbox> inbox;
    WorkerLinkApi()=default;
    explicit WorkerLinkApi(std::shared_ptr<Inbox> value):inbox(std::move(value)){}
    X::Value Scope();
    void Close(){if(!inbox)protocol();std::lock_guard lock(inbox->mutex);inbox->closed=true;inbox->changed.notify_all();}
    void ConfigureModel(bool coding,std::string endpoint,std::string ticket,std::int64_t pid){
        if(!inbox||pid<1||pid>UINT32_MAX)protocol();
        AgentWorkerStartup startup{coding,{std::move(endpoint),std::move(ticket),static_cast<std::uint32_t>(pid)}};validate_service_access(startup.model);
        std::lock_guard lock(inbox->mutex);if(inbox->closed||inbox->startup||inbox->startup_received)protocol();
        inbox->startup=std::move(startup);inbox->changed.notify_all();
    }
    void PublishExecution(std::string endpoint,std::string ticket,std::int64_t pid){
        if(!inbox||pid<1||pid>UINT32_MAX)protocol();
        AgentWorkerServiceAccess access{std::move(endpoint),std::move(ticket),static_cast<std::uint32_t>(pid)};validate_service_access(access);
        std::lock_guard lock(inbox->mutex);if(inbox->closed||inbox->execution||inbox->execution_received)protocol();
        inbox->execution=std::move(access);inbox->changed.notify_all();
    }
    void AnnounceProcess(std::int64_t pid,std::int64_t created,std::int64_t parent_pid,std::int64_t port){
        if(!inbox||pid<1||pid>UINT32_MAX||created<1||parent_pid<1||parent_pid>UINT32_MAX||port<1||port>65535)protocol();
        std::lock_guard lock(inbox->mutex);if(inbox->closed||inbox->process_ready||inbox->process_ready_received)protocol();
        inbox->process_ready=WorkerProcessReady{static_cast<std::uint32_t>(pid),static_cast<std::uint32_t>(parent_pid),static_cast<std::uint32_t>(port),static_cast<std::uint64_t>(created)};inbox->changed.notify_all();
    }
    BEGIN_PACKAGE(WorkerLinkApi)
        APISET().AddFunc<0>("Scope",&WorkerLinkApi::Scope);
        APISET().AddFunc<0>("Close",&WorkerLinkApi::Close);
        APISET().AddFunc<4>("ConfigureModel",&WorkerLinkApi::ConfigureModel);
        APISET().AddFunc<3>("PublishExecution",&WorkerLinkApi::PublishExecution);
        APISET().AddFunc<4>("AnnounceProcess",&WorkerLinkApi::AnnounceProcess);
    END_PACKAGE
};
struct WorkerRemoteObjects {
    std::mutex mutex;std::map<std::string,std::weak_ptr<Inbox>> inboxes;
    void add(const std::string& token,std::shared_ptr<Inbox> inbox){std::lock_guard lock(mutex);if(!inboxes.emplace(token,inbox).second)protocol();}
    void remove(const std::string& token){std::lock_guard lock(mutex);inboxes.erase(token);}
    std::shared_ptr<Inbox> find(const std::string& token){std::lock_guard lock(mutex);auto found=inboxes.find(token);if(found==inboxes.end())protocol();auto inbox=found->second.lock();if(!inbox)protocol();return inbox;}
    BEGIN_PACKAGE(WorkerRemoteObjects)
        APISET().AddClass<0,WorkerLinkApi>("Link");
        APISET().AddClass<0,WorkerScopeApi>("Scope");
    END_PACKAGE
};
struct WorkerIpcHost {
    std::shared_ptr<WorkerRemoteObjects> owner=std::make_shared<WorkerRemoteObjects>();WorkerRemoteObjects& objects=*owner;X::Runtime& runtime=xmind_ipc_runtime();std::uint32_t port=0;
    WorkerIpcHost(){
        retain_xmind_ipc_service(owner);
        (void)runtime.RegisterPackage("_xmind_worker_link_objects",objects);
        ensure_xmind_ipc_api();port=xmind_ipc_port();
    }
};
WorkerIpcHost& ipc_host(){static WorkerIpcHost host;return host;}
X::Value WorkerLinkApi::Scope(){
    if(!inbox)protocol();auto captured=std::make_shared<WorkerScopeApi>();
    {std::lock_guard lock(inbox->mutex);if(inbox->closed)protocol();captured->scope=inbox->scope;}
    validate_scope(captured->scope);
    return ipc_host().objects.__xlang3_package_->CreateInstance("Scope",std::move(captured));
}
// The inherited OS record carries only identity and rendezvous capabilities.
// Application scope is obtained from the registered SDK Scope subobject.
struct WorkerBootstrap {
    std::uint32_t version=2,size=0,parent_pid=0,parent_port=0;
    std::uint64_t parent_birth=0;
    std::array<char,64> parent_route{},worker_route{};
};
static_assert(sizeof(WorkerBootstrap)<=8192);
std::string new_route(){std::array<std::uint8_t,32> bytes{};if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot create worker capability");return hex(bytes);}
struct MappingView {
    void* value=nullptr;explicit MappingView(HANDLE handle,DWORD access):value(MapViewOfFile(handle,access,0,0,8192)){if(!value)throw std::runtime_error("Cannot map private worker bootstrap");}
    ~MappingView(){UnmapViewOfFile(value);}
};
std::uint64_t birth(HANDLE process){FILETIME created{},exit{},kernel{},user{};if(!GetProcessTimes(process,&created,&exit,&kernel,&user))throw std::runtime_error("Cannot identify worker creation");return (static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;}
std::vector<wchar_t> environment(){
    std::array<wchar_t,32768> text{};const auto size=GetWindowsDirectoryW(text.data(),static_cast<UINT>(text.size()));if(!size||size>=text.size())throw std::runtime_error("Cannot obtain worker system directory");
    std::vector<wchar_t> block;auto add=[&](const std::wstring& key,const std::wstring& value){block.insert(block.end(),key.begin(),key.end());block.push_back(L'=');block.insert(block.end(),value.begin(),value.end());block.push_back(L'\0');};
    add(L"SystemRoot",std::wstring(text.data(),size));add(L"WINDIR",std::wstring(text.data(),size));block.push_back(L'\0');return block;
}
struct Attributes {
    std::array<HANDLE,3> handles;
    std::vector<std::byte> bytes;LPPROC_THREAD_ATTRIBUTE_LIST list=nullptr;
    explicit Attributes(std::array<HANDLE,3> values):handles(values){SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);if(!size)throw std::runtime_error("Cannot size worker inheritance");bytes.resize(size);list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes.data());
        if(!InitializeProcThreadAttributeList(list,1,0,&size)){list=nullptr;throw std::runtime_error("Cannot create worker inheritance");}
        if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles.data(),sizeof(handles),nullptr,nullptr)){DeleteProcThreadAttributeList(list);list=nullptr;throw std::runtime_error("Cannot restrict worker inheritance");}}
    ~Attributes(){if(list)DeleteProcThreadAttributeList(list);}
};
}
X::Value NativeWorkerLinksApi::Open(std::string ticket){auto& host=ipc_host();auto inbox=host.objects.find(ticket);{std::lock_guard lock(inbox->mutex);if(inbox->closed)protocol();}return host.objects.__xlang3_package_->CreateInstance("Link",std::make_shared<WorkerLinkApi>(std::move(inbox)));}

struct AgentWorkerChannel::Impl {
    AgentWorkerScope scope;std::mutex reader,writer;std::atomic<bool> poisoned=false,closed=false;
    std::shared_ptr<Inbox> inbox=std::make_shared<Inbox>();std::string local,remote_route;std::uint32_t remote_port=0,peer_pid=0;std::uint64_t peer_created=0;Handle peer;
    X::Runtime client;X::Value remote;
    Impl(AgentWorkerScope binding,std::string route):scope(std::move(binding)),local(std::move(route)){inbox->scope=scope;ipc_host().objects.add(local,inbox);}
    ~Impl(){ipc_host().objects.remove(local);}
    void attach(std::uint32_t port,std::string route,HANDLE process){
        if(port<1||port>65535||route.size()!=64)protocol();remote_port=port;remote_route=std::move(route);
        HANDLE duplicate=nullptr;if(!DuplicateHandle(GetCurrentProcess(),process,GetCurrentProcess(),&duplicate,SYNCHRONIZE,FALSE,0))throw std::runtime_error("Cannot retain IPC peer identity");peer=Handle(duplicate);
        const auto endpoint="lrpc:"+std::to_string(port);X3LrpcEndpointInfo info{};info.size=sizeof(info);
        if(x3_runtime_probe_remote(client.get(),endpoint.c_str(),1000,&info)!=X3_STATUS_OK||info.pid!=GetProcessId(process))protocol();
        peer_pid=info.pid;peer_created=birth(process);
        auto root=client.ImportRemote("xmind",endpoint.c_str());X::Value links;
        if(!root["WorkerLinks"].Call(std::vector<X::Value>{},links)||!links["Open"].Call(std::vector<X::Value>{X::Value(client,remote_route)},remote))protocol();
    }
    void live(){if(peer.value&&WaitForSingleObject(peer.value,0)!=WAIT_TIMEOUT)protocol();}
};
AgentWorkerChannel::AgentWorkerChannel(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
AgentWorkerChannel::~AgentWorkerChannel(){close();}
const AgentWorkerScope& AgentWorkerChannel::scope()const noexcept{return impl_->scope;}
AgentWorkerPeer AgentWorkerChannel::peer()const{return {"lrpc:"+std::to_string(impl_->remote_port),impl_->peer_pid,impl_->peer_created};}
bool AgentWorkerChannel::peer_closed()const noexcept{
    if(impl_->closed||impl_->poisoned||(impl_->peer.value&&WaitForSingleObject(impl_->peer.value,0)!=WAIT_TIMEOUT))return true;
    std::lock_guard lock(impl_->inbox->mutex);return impl_->inbox->closed;
}
void AgentWorkerChannel::close()noexcept{
    if(impl_->closed.exchange(true))return;impl_->poisoned=true;
    {std::lock_guard lock(impl_->inbox->mutex);impl_->inbox->closed=true;impl_->inbox->changed.notify_all();}
    try{if(impl_->remote_port){impl_->live();impl_->remote["Close"]();}}catch(...){}
}
void AgentWorkerChannel::configure_model(const AgentWorkerStartup& startup,Deadline deadline,std::stop_token stop){
    validate_service_access(startup.model);std::lock_guard writer(impl_->writer);if(impl_->poisoned)protocol();check(deadline,stop);impl_->live();
    try{X::Value result;if(!impl_->remote["ConfigureModel"].Call(std::vector<X::Value>{X::Value(startup.coding),X::Value(impl_->client,startup.model.endpoint),X::Value(impl_->client,startup.model.ticket),X::Value(static_cast<std::int64_t>(startup.model.pid))},result))protocol();check(deadline,stop);}
    catch(...){impl_->poisoned=true;throw;}
}
AgentWorkerStartup AgentWorkerChannel::receive_model(Deadline deadline,std::stop_token stop){
    std::lock_guard reader(impl_->reader);if(impl_->poisoned)protocol();
    try{return await_startup_value(*impl_->inbox,&Inbox::startup,&Inbox::startup_received,deadline,stop,[&]{impl_->live();});}
    catch(...){impl_->poisoned=true;throw;}
}
void AgentWorkerChannel::publish_execution(const AgentWorkerServiceAccess& access,Deadline deadline,std::stop_token stop){
    validate_service_access(access);std::lock_guard writer(impl_->writer);if(impl_->poisoned)protocol();check(deadline,stop);impl_->live();
    try{X::Value result;if(!impl_->remote["PublishExecution"].Call(std::vector<X::Value>{X::Value(impl_->client,access.endpoint),X::Value(impl_->client,access.ticket),X::Value(static_cast<std::int64_t>(access.pid))},result))protocol();check(deadline,stop);}
    catch(...){impl_->poisoned=true;throw;}
}
AgentWorkerServiceAccess AgentWorkerChannel::receive_execution(Deadline deadline,std::stop_token stop){
    std::lock_guard reader(impl_->reader);if(impl_->poisoned)protocol();
    try{return await_startup_value(*impl_->inbox,&Inbox::execution,&Inbox::execution_received,deadline,stop,[&]{impl_->live();});}
    catch(...){impl_->poisoned=true;throw;}
}
void AgentWorkerChannel::wait_peer_closed(Deadline deadline,std::stop_token stop){
    std::lock_guard reader(impl_->reader);auto& inbox=*impl_->inbox;std::unique_lock lock(inbox.mutex);
    for(;;){check(deadline,stop);if(inbox.closed)return;impl_->live();inbox.changed.wait_for(lock,std::chrono::milliseconds(5));}
}
std::unique_ptr<AgentWorkerChannel> AgentWorkerChannel::accept_inherited(std::uintptr_t inherited,Deadline deadline,std::stop_token stop){
    check(deadline,stop);Handle mapping(reinterpret_cast<HANDLE>(inherited));if(!mapping.value||mapping.value==INVALID_HANDLE_VALUE)protocol();
    if(!SetHandleInformation(mapping.value,HANDLE_FLAG_INHERIT,0))protocol();MappingView view(mapping.value,FILE_MAP_READ);
    WorkerBootstrap config;std::memcpy(&config,view.value,sizeof(config));
    if(config.version!=2||config.size!=sizeof(config)||!config.parent_birth)protocol();
    const auto route=[](const auto& bytes){std::string value(bytes.data(),bytes.size());if(value.find_first_not_of("0123456789abcdef")!=std::string::npos)protocol();return value;};
    const auto parent_pid=config.parent_pid;Handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parent_pid));
    if(!parent.value||birth(parent.value)!=config.parent_birth)protocol();
    auto channel=std::unique_ptr<AgentWorkerChannel>(new AgentWorkerChannel(std::make_unique<Impl>(AgentWorkerScope{},route(config.worker_route))));
    channel->impl_->attach(config.parent_port,route(config.parent_route),parent.value);
    X::Value captured;if(!channel->impl_->remote["Scope"].Call(std::vector<X::Value>{},captured))protocol();
    const auto value=[&](const char* name){check(deadline,stop);X::Value result;if(!captured[name].Call(std::vector<X::Value>{},result))protocol();return result;};
    const auto text=[&](const char* name){const auto result=value(name);if(!result.IsString())protocol();return result.ToString();};
    AgentWorkerScope scope{text("RootId"),text("RunId"),text("WorkspaceId"),text("Generation"),0};
    const auto revision=value("WorkspaceRevision");if(!revision.IsInt64())protocol();scope.workspace_revision=revision.ToInt64();validate_scope(scope);
    channel->impl_->scope=scope;{std::lock_guard lock(channel->impl_->inbox->mutex);channel->impl_->inbox->scope=std::move(scope);}
    X::Value acknowledged;if(!channel->impl_->remote["AnnounceProcess"].Call(std::vector<X::Value>{X::Value(static_cast<std::int64_t>(GetCurrentProcessId())),X::Value(static_cast<std::int64_t>(birth(GetCurrentProcess()))),X::Value(static_cast<std::int64_t>(parent_pid)),X::Value(static_cast<std::int64_t>(ipc_host().port))},acknowledged))protocol();check(deadline,stop);return channel;
}
struct AgentWorkerProcess::Impl {
    Handle process,job;std::shared_ptr<const BoundProcessExecutable> executable;std::unique_ptr<AgentWorkerChannel> channel;AgentWorkerScope scope;std::string worker_route;std::uint32_t pid=0;std::uint64_t created=0;bool assigned=false,start_attempted=false;
    ~Impl(){if(assigned&&job.value)TerminateJobObject(job.value,1);if(process.value&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT){if(!assigned)TerminateProcess(process.value,1);WaitForSingleObject(process.value,5000);}}
};
AgentWorkerProcess::AgentWorkerProcess(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
AgentWorkerProcess::~AgentWorkerProcess()=default;
std::unique_ptr<AgentWorkerProcess> AgentWorkerProcess::launch(std::shared_ptr<const BoundProcessExecutable> executable,AgentWorkerScope scope,AgentWorkerChannel::Deadline deadline,std::stop_token stop){
    check(deadline,stop);auto worker=prepare(std::move(executable),std::move(scope));worker->start(deadline,stop);return worker;
}
std::unique_ptr<AgentWorkerProcess> AgentWorkerProcess::prepare(std::shared_ptr<const BoundProcessExecutable> executable,AgentWorkerScope scope){
    validate_scope(scope);if(!executable||executable->identity().empty())throw std::invalid_argument("Worker requires a retained executable binding");
    auto state=std::make_unique<Impl>();state->executable=std::move(executable);state->scope=std::move(scope);state->worker_route=new_route();
    state->channel=std::unique_ptr<AgentWorkerChannel>(new AgentWorkerChannel(std::make_unique<AgentWorkerChannel::Impl>(state->scope,new_route())));
    return std::unique_ptr<AgentWorkerProcess>(new AgentWorkerProcess(std::move(state)));
}
void AgentWorkerProcess::start(AgentWorkerChannel::Deadline deadline,std::stop_token stop){
    auto* state=impl_.get();if(state->start_attempted)throw AgentWorkerProtocolError("Worker startup cannot be repeated");state->start_attempted=true;check(deadline,stop);
    // Recheck the retained identity before launch; the capability pins bytes and
    // the directory chain, so no unbound replacement path may be substituted.
    if(ForegroundProcess::executable_identity(state->executable->path())!=state->executable->identity())throw ProcessBeforeDispatchError("Worker image binding changed");
    const auto& parent_route=state->channel->impl_->local;const auto& worker_route=state->worker_route;
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    Handle child(CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,8192,nullptr));if(!child.value)throw std::runtime_error("Cannot create private worker bootstrap");
    {MappingView view(child.value,FILE_MAP_WRITE);WorkerBootstrap bootstrap;bootstrap.size=sizeof(bootstrap);bootstrap.parent_pid=GetCurrentProcessId();bootstrap.parent_birth=birth(GetCurrentProcess());bootstrap.parent_port=ipc_host().port;
        std::copy(parent_route.begin(),parent_route.end(),bootstrap.parent_route.begin());std::copy(worker_route.begin(),worker_route.end(),bootstrap.worker_route.begin());
        std::memcpy(view.value,&bootstrap,sizeof(bootstrap));SecureZeroMemory(&bootstrap,sizeof(bootstrap));}
    Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));Handle output(CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));if(input.value==INVALID_HANDLE_VALUE||output.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot isolate worker standard handles");
    state->job=Handle(CreateJobObjectW(nullptr,nullptr));if(!state->job.value)throw std::runtime_error("Cannot create worker ownership job");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(!SetInformationJobObject(state->job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Cannot restrict worker ownership job");
    Attributes attributes({child.value,input.value,output.value});STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=input.value;startup.StartupInfo.hStdOutput=output.value;startup.StartupInfo.hStdError=output.value;startup.lpAttributeList=attributes.list;
    const auto image=wide(state->executable->path());auto command=quote(image)+L" agent-channel-worker --channel-handle "+std::to_wstring(reinterpret_cast<std::uintptr_t>(child.value));auto env=environment();PROCESS_INFORMATION created{};
    // Preserve the retained canonical image/argv while allowing the Windows
    // loader to resolve the adjacent SDK DLL at long managed-runtime paths.
    auto launch_image=image;std::replace(launch_image.begin(),launch_image.end(),L'/',L'\\');launch_image.insert(0,L"\\\\?\\");
    check(deadline,stop);if(!CreateProcessW(launch_image.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT,env.data(),std::filesystem::path(image).parent_path().c_str(),&startup.StartupInfo,&created))throw ProcessBeforeDispatchError("Cannot create owned worker (Windows error "+std::to_string(GetLastError())+")");
    state->process=Handle(created.hProcess);Handle thread(created.hThread);state->pid=created.dwProcessId;state->created=birth(state->process.value);
    if(!AssignProcessToJobObject(state->job.value,state->process.value)){TerminateProcess(state->process.value,1);throw ProcessBeforeDispatchError("Cannot assign worker ownership");}
    state->assigned=true;
    if(ResumeThread(thread.value)==static_cast<DWORD>(-1))throw ProcessBeforeDispatchError("Cannot start owned worker");
    child=Handle();input=Handle();output=Handle();
    const auto identity=await_startup_value(*state->channel->impl_->inbox,&Inbox::process_ready,&Inbox::process_ready_received,deadline,stop,[&]{if(WaitForSingleObject(state->process.value,0)!=WAIT_TIMEOUT)protocol();});
    if(identity.pid!=state->pid||identity.created!=state->created||identity.parent_pid!=GetCurrentProcessId())protocol();
    state->channel->impl_->attach(identity.port,worker_route,state->process.value);
}
AgentWorkerChannel& AgentWorkerProcess::channel()noexcept{return *impl_->channel;}
std::uint32_t AgentWorkerProcess::pid()const noexcept{return impl_->pid;}
std::uint64_t AgentWorkerProcess::creation_time()const noexcept{return impl_->created;}
const std::string& AgentWorkerProcess::executable_identity()const noexcept{return impl_->executable->identity();}
bool AgentWorkerProcess::alive()const{if(!impl_->process.value)return false;const auto result=WaitForSingleObject(impl_->process.value,0);if(result==WAIT_FAILED)throw std::runtime_error("Cannot observe owned worker");return result==WAIT_TIMEOUT;}
std::uint32_t AgentWorkerProcess::wait(AgentWorkerChannel::Deadline deadline,std::stop_token stop){
    if(!impl_->process.value)return 0;
    while(alive()){check(deadline,stop);WaitForSingleObject(impl_->process.value,10);}DWORD code=0;if(!GetExitCodeProcess(impl_->process.value,&code))throw std::runtime_error("Cannot read owned worker exit");for(;;){JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};if(!QueryInformationJobObject(impl_->job.value,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr))throw std::runtime_error("Cannot observe worker descendants");if(!accounting.ActiveProcesses)return code;check(deadline,stop);Sleep(5);}
}
void AgentWorkerProcess::terminate(){
    impl_->start_attempted=true;
    if(!impl_->process.value)return;
    if(impl_->assigned){
        if(!TerminateJobObject(impl_->job.value,1))throw std::runtime_error("Cannot retire owned worker job");
    }else if(alive()&&!TerminateProcess(impl_->process.value,1))throw std::runtime_error("Cannot retire unassigned owned worker");
    const auto until=Clock::now()+std::chrono::seconds(5);
    if(impl_->assigned)for(;;){
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if(!QueryInformationJobObject(impl_->job.value,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr))throw std::runtime_error("Cannot observe owned worker job");
        if(!accounting.ActiveProcesses)break;
        if(Clock::now()>=until)throw std::runtime_error("Owned worker retirement is unconfirmed");
        Sleep(5);
    }
    const auto remaining=std::clamp<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(until-Clock::now()).count(),0,5000);
    if(WaitForSingleObject(impl_->process.value,static_cast<DWORD>(remaining))!=WAIT_OBJECT_0)throw std::runtime_error("Owned worker retirement is unconfirmed");
}
}
