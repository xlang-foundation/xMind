#include "agentflow/agent_worker_channel.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/agent_worker_native_links.hpp"
#include <optional>
#include "nlohmann/json.hpp"
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
#include <set>
#include <utility>
#include <vector>

namespace agentflow {
namespace {
using Json=nlohmann::json;
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
Json scope_json(const AgentWorkerScope& scope){
    identifier(scope.root_id);identifier(scope.run_id);identifier(scope.workspace_id);identifier(scope.generation);
    if(scope.workspace_revision<1||scope.workspace_revision>9007199254740991)protocol();
    return {{"root_id",scope.root_id},{"run_id",scope.run_id},{"workspace_id",scope.workspace_id},{"workspace_revision",scope.workspace_revision},{"generation",scope.generation}};
}
Json parse(const std::string& text){
    if(text.empty()||text.size()>AgentWorkerChannel::max_message_bytes)protocol();
    std::vector<std::set<std::string>> objects;
    return Json::parse(text,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>8)protocol();
        if(event==Json::parse_event_t::object_start)objects.emplace_back();
        if(event==Json::parse_event_t::key){if(objects.empty()||!objects.back().insert(value.get<std::string>()).second)protocol();}
        if(event==Json::parse_event_t::object_end){if(objects.empty())protocol();objects.pop_back();}
        return true;
    });
}
AgentWorkerScope read_scope(const Json& value){
    if(!value.is_object()||value.size()!=5||!value.at("workspace_revision").is_number_integer())protocol();
    AgentWorkerScope scope{value.at("root_id").get<std::string>(),value.at("run_id").get<std::string>(),value.at("workspace_id").get<std::string>(),value.at("generation").get<std::string>(),value.at("workspace_revision").get<std::int64_t>()};
    if(scope_json(scope)!=value)protocol();return scope;
}
void operation(const std::string& value){
    if(value.empty()||value.size()>64)protocol();for(char c:value)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'))protocol();
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
struct IoStatistics {std::atomic<std::uint64_t> pending_reads=0,pending_writes=0,cancelled_reads=0,cancelled_writes=0;};
struct Inbox {
    std::mutex mutex;std::condition_variable changed;bool waiting=false,closed=false;std::optional<AgentWorkerMessage> message;
};
struct WorkerLinkApi {
    std::shared_ptr<Inbox> inbox;
    WorkerLinkApi()=default;
    explicit WorkerLinkApi(std::shared_ptr<Inbox> value):inbox(std::move(value)){}
    std::int64_t Deliver(std::string name,std::string payload){
        operation(name);if(payload.size()>AgentWorkerChannel::max_message_bytes-name.size())protocol();if(!inbox)protocol();
        std::lock_guard lock(inbox->mutex);if(inbox->closed)return -1;if(!inbox->waiting||inbox->message)return 0;
        inbox->message=AgentWorkerMessage{std::move(name),std::move(payload)};inbox->changed.notify_one();return 1;
    }
    void Close(){if(!inbox)protocol();std::lock_guard lock(inbox->mutex);inbox->closed=true;inbox->changed.notify_all();}
    BEGIN_PACKAGE(WorkerLinkApi)
        APISET().AddFunc<2>("Deliver",&WorkerLinkApi::Deliver);
        APISET().AddFunc<0>("Close",&WorkerLinkApi::Close);
    END_PACKAGE
};
struct WorkerRemoteObjects {
    std::mutex mutex;std::map<std::string,std::weak_ptr<Inbox>> inboxes;
    void add(const std::string& token,std::shared_ptr<Inbox> inbox){std::lock_guard lock(mutex);if(!inboxes.emplace(token,inbox).second)protocol();}
    void remove(const std::string& token){std::lock_guard lock(mutex);inboxes.erase(token);}
    std::shared_ptr<Inbox> find(const std::string& token){std::lock_guard lock(mutex);auto found=inboxes.find(token);if(found==inboxes.end())protocol();auto inbox=found->second.lock();if(!inbox)protocol();return inbox;}
    BEGIN_PACKAGE(WorkerRemoteObjects)
        APISET().AddClass<0,WorkerLinkApi>("Link");
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
    AgentWorkerScope scope;std::mutex reader,writer;std::atomic<bool> poisoned=false,closed=false;IoStatistics statistics;
    std::shared_ptr<Inbox> inbox=std::make_shared<Inbox>();std::string local,remote_route;std::uint32_t remote_port=0;Handle peer;
    X::Runtime client;X::Value remote;
    Impl(AgentWorkerScope binding,std::string route):scope(std::move(binding)),local(std::move(route)){ipc_host().objects.add(local,inbox);}
    ~Impl(){ipc_host().objects.remove(local);}
    void attach(std::uint32_t port,std::string route,HANDLE process){
        if(port<1||port>65535||route.size()!=64)protocol();remote_port=port;remote_route=std::move(route);
        HANDLE duplicate=nullptr;if(!DuplicateHandle(GetCurrentProcess(),process,GetCurrentProcess(),&duplicate,SYNCHRONIZE,FALSE,0))throw std::runtime_error("Cannot retain IPC peer identity");peer=Handle(duplicate);
        const auto endpoint="lrpc:"+std::to_string(port);X3LrpcEndpointInfo info{};info.size=sizeof(info);
        if(x3_runtime_probe_remote(client.get(),endpoint.c_str(),1000,&info)!=X3_STATUS_OK||info.pid!=GetProcessId(process))protocol();
        auto root=client.ImportRemote("xmind",endpoint.c_str());X::Value links;
        if(!root["WorkerLinks"].Call(std::vector<X::Value>{},links)||!links["Open"].Call(std::vector<X::Value>{X::Value(client,remote_route)},remote))protocol();
    }
    void live(){if(peer.value&&WaitForSingleObject(peer.value,0)!=WAIT_TIMEOUT)protocol();}
};
AgentWorkerChannel::AgentWorkerChannel(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
AgentWorkerChannel::~AgentWorkerChannel(){close();}
const AgentWorkerScope& AgentWorkerChannel::scope()const noexcept{return impl_->scope;}
AgentWorkerChannelStatistics AgentWorkerChannel::statistics()const noexcept{const auto& s=impl_->statistics;return {s.pending_reads.load(),s.pending_writes.load(),s.cancelled_reads.load(),s.cancelled_writes.load()};}
void AgentWorkerChannel::close()noexcept{
    if(impl_->closed.exchange(true))return;impl_->poisoned=true;
    {std::lock_guard lock(impl_->inbox->mutex);impl_->inbox->closed=true;impl_->inbox->changed.notify_all();}
    try{if(impl_->remote_port){impl_->live();impl_->remote["Close"]();}}catch(...){}
}
void AgentWorkerChannel::send(const AgentWorkerMessage& message,Deadline deadline,std::stop_token stop){
    std::lock_guard lock(impl_->writer);if(impl_->poisoned)protocol();++impl_->statistics.pending_writes;
    try{check(deadline,stop);operation(message.operation);if(message.payload.size()>max_message_bytes-message.operation.size())protocol();const std::vector<X::Value> arguments{X::Value(impl_->client,message.operation),X::Value(impl_->client,message.payload)};
        for(;;){check(deadline,stop);impl_->live();X::Value reply;if(!impl_->remote["Deliver"].Call(arguments,reply)||!reply.IsInt64())protocol();const auto result=reply.ToInt64();if(result==1){check(deadline,stop);return;}if(result!=0)protocol();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    }catch(const AgentWorkerChannelCancelled&){++impl_->statistics.cancelled_writes;impl_->poisoned=true;throw;}
    catch(const AgentWorkerChannelTimeout&){++impl_->statistics.cancelled_writes;impl_->poisoned=true;throw;}
    catch(...){impl_->poisoned=true;throw AgentWorkerProtocolError("Worker shared-memory IPC send failed");}
}
AgentWorkerMessage AgentWorkerChannel::receive(Deadline deadline,std::stop_token stop){
    std::lock_guard reader(impl_->reader);if(impl_->poisoned)protocol();++impl_->statistics.pending_reads;
    auto& inbox=*impl_->inbox;std::unique_lock lock(inbox.mutex);inbox.waiting=true;
    struct Waiting {Inbox& inbox;~Waiting(){inbox.waiting=false;}} waiting{inbox};
    try{for(;;){check(deadline,stop);if(inbox.message){auto message=std::move(*inbox.message);inbox.message.reset();return message;}if(inbox.closed)protocol();impl_->live();inbox.changed.wait_for(lock,std::chrono::milliseconds(5));}}
    catch(const AgentWorkerChannelCancelled&){++impl_->statistics.cancelled_reads;impl_->poisoned=true;throw;}
    catch(const AgentWorkerChannelTimeout&){++impl_->statistics.cancelled_reads;impl_->poisoned=true;throw;}
    catch(...){impl_->poisoned=true;throw;}
}
std::unique_ptr<AgentWorkerChannel> AgentWorkerChannel::accept_inherited(std::uintptr_t inherited,Deadline deadline,std::stop_token stop){
    check(deadline,stop);Handle mapping(reinterpret_cast<HANDLE>(inherited));if(!mapping.value||mapping.value==INVALID_HANDLE_VALUE)protocol();
    if(!SetHandleInformation(mapping.value,HANDLE_FLAG_INHERIT,0))protocol();MappingView view(mapping.value,FILE_MAP_READ);
    std::uint32_t size=0;std::memcpy(&size,view.value,sizeof(size));if(!size||size>8188)protocol();
    const auto* bytes=static_cast<const char*>(view.value)+4;
    const auto config=parse(std::string(bytes,size));if(!config.is_object()||config.size()!=7||config.at("version")!=1)protocol();
    const auto parent_pid=config.at("parent_pid").get<DWORD>();Handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parent_pid));
    if(!parent.value||birth(parent.value)!=config.at("parent_birth").get<std::uint64_t>())protocol();
    auto channel=std::unique_ptr<AgentWorkerChannel>(new AgentWorkerChannel(std::make_unique<Impl>(read_scope(config.at("scope")),config.at("worker_route").get<std::string>())));
    channel->impl_->attach(config.at("parent_port").get<std::uint32_t>(),config.at("parent_route").get<std::string>(),parent.value);
    channel->send({"worker.ready",Json{{"pid",GetCurrentProcessId()},{"creation_time",birth(GetCurrentProcess())},{"parent_pid",parent_pid},{"port",ipc_host().port}}.dump()},deadline,stop);return channel;
}
struct AgentWorkerProcess::Impl {
    Handle process,job;std::shared_ptr<const BoundProcessExecutable> executable;std::unique_ptr<AgentWorkerChannel> channel;std::uint32_t pid=0;std::uint64_t created=0;bool assigned=false;
    ~Impl(){if(assigned&&job.value)TerminateJobObject(job.value,1);if(process.value&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT){if(!assigned)TerminateProcess(process.value,1);WaitForSingleObject(process.value,5000);}}
};
AgentWorkerProcess::AgentWorkerProcess(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
AgentWorkerProcess::~AgentWorkerProcess()=default;
std::unique_ptr<AgentWorkerProcess> AgentWorkerProcess::launch(std::shared_ptr<const BoundProcessExecutable> executable,AgentWorkerScope scope,AgentWorkerChannel::Deadline deadline,std::stop_token stop){
    check(deadline,stop);scope_json(scope);if(!executable||executable->identity().empty())throw std::invalid_argument("Worker requires a retained executable binding");
    // Recheck the retained identity before launch; the capability pins bytes and
    // the directory chain, so no unbound replacement path may be substituted.
    if(ForegroundProcess::executable_identity(executable->path())!=executable->identity())throw ProcessBeforeDispatchError("Worker image binding changed");
    auto state=std::make_unique<Impl>();state->executable=std::move(executable);
    const auto parent_route=new_route(),worker_route=new_route();
    state->channel=std::unique_ptr<AgentWorkerChannel>(new AgentWorkerChannel(std::make_unique<AgentWorkerChannel::Impl>(scope,parent_route)));
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    Handle child(CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,8192,nullptr));if(!child.value)throw std::runtime_error("Cannot create private worker bootstrap");
    {MappingView view(child.value,FILE_MAP_WRITE);std::string bootstrap=Json{{"version",1},{"scope",scope_json(scope)},{"parent_pid",GetCurrentProcessId()},{"parent_birth",birth(GetCurrentProcess())},{"parent_port",ipc_host().port},{"parent_route",parent_route},{"worker_route",worker_route}}.dump();
        if(bootstrap.size()>8188)protocol();const auto size=static_cast<std::uint32_t>(bootstrap.size());std::memcpy(view.value,&size,4);std::memcpy(static_cast<char*>(view.value)+4,bootstrap.data(),bootstrap.size());SecureZeroMemory(bootstrap.data(),bootstrap.size());}
    Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));Handle output(CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));if(input.value==INVALID_HANDLE_VALUE||output.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot isolate worker standard handles");
    state->job=Handle(CreateJobObjectW(nullptr,nullptr));if(!state->job.value)throw std::runtime_error("Cannot create worker ownership job");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(!SetInformationJobObject(state->job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Cannot restrict worker ownership job");
    Attributes attributes({child.value,input.value,output.value});STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=input.value;startup.StartupInfo.hStdOutput=output.value;startup.StartupInfo.hStdError=output.value;startup.lpAttributeList=attributes.list;
    const auto image=wide(state->executable->path());auto command=quote(image)+L" agent-channel-worker --channel-handle "+std::to_wstring(reinterpret_cast<std::uintptr_t>(child.value));auto env=environment();PROCESS_INFORMATION created{};
    check(deadline,stop);if(!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT,env.data(),std::filesystem::path(image).parent_path().c_str(),&startup.StartupInfo,&created))throw ProcessBeforeDispatchError("Cannot create owned worker (Windows error "+std::to_string(GetLastError())+")");
    state->process=Handle(created.hProcess);Handle thread(created.hThread);state->pid=created.dwProcessId;state->created=birth(state->process.value);
    if(!AssignProcessToJobObject(state->job.value,state->process.value)){TerminateProcess(state->process.value,1);throw ProcessBeforeDispatchError("Cannot assign worker ownership");}
    state->assigned=true;
    if(ResumeThread(thread.value)==static_cast<DWORD>(-1))throw ProcessBeforeDispatchError("Cannot start owned worker");
    child=Handle();input=Handle();output=Handle();
    const auto ready=state->channel->receive(deadline,stop);const auto identity=parse(ready.payload);if(ready.operation!="worker.ready"||!identity.is_object()||identity.size()!=4||identity.at("pid")!=state->pid||identity.at("creation_time")!=state->created||identity.at("parent_pid")!=GetCurrentProcessId())protocol();
    state->channel->impl_->attach(identity.at("port").get<std::uint32_t>(),worker_route,state->process.value);
    return std::unique_ptr<AgentWorkerProcess>(new AgentWorkerProcess(std::move(state)));
}
AgentWorkerChannel& AgentWorkerProcess::channel()noexcept{return *impl_->channel;}
std::uint32_t AgentWorkerProcess::pid()const noexcept{return impl_->pid;}
std::uint64_t AgentWorkerProcess::creation_time()const noexcept{return impl_->created;}
const std::string& AgentWorkerProcess::executable_identity()const noexcept{return impl_->executable->identity();}
bool AgentWorkerProcess::alive()const{const auto result=WaitForSingleObject(impl_->process.value,0);if(result==WAIT_FAILED)throw std::runtime_error("Cannot observe owned worker");return result==WAIT_TIMEOUT;}
std::uint32_t AgentWorkerProcess::wait(AgentWorkerChannel::Deadline deadline,std::stop_token stop){
    while(alive()){check(deadline,stop);WaitForSingleObject(impl_->process.value,10);}DWORD code=0;if(!GetExitCodeProcess(impl_->process.value,&code))throw std::runtime_error("Cannot read owned worker exit");for(;;){JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};if(!QueryInformationJobObject(impl_->job.value,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr))throw std::runtime_error("Cannot observe worker descendants");if(!accounting.ActiveProcesses)return code;check(deadline,stop);Sleep(5);}
}
void AgentWorkerProcess::terminate(){if(!TerminateJobObject(impl_->job.value,1))throw std::runtime_error("Cannot retire owned worker job");const auto until=Clock::now()+std::chrono::seconds(5);for(;;){JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};if(!QueryInformationJobObject(impl_->job.value,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr))throw std::runtime_error("Cannot observe owned worker job");if(!accounting.ActiveProcesses)break;if(Clock::now()>=until)throw std::runtime_error("Owned worker retirement is unconfirmed");Sleep(5);}const auto remaining=std::clamp<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(until-Clock::now()).count(),0,5000);if(WaitForSingleObject(impl_->process.value,static_cast<DWORD>(remaining))!=WAIT_OBJECT_0)throw std::runtime_error("Owned worker retirement is unconfirmed");}
}
