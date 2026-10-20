#include "agentflow/agent_worker_channel.hpp"
#include "nlohmann/json.hpp"
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
[[noreturn]] void protocol(){throw AgentWorkerProtocolError("Agent worker channel rejected its frame");}
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
    if(text.empty()||text.size()>AgentWorkerFrameCodec::max_frame_bytes)protocol();
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
std::string signature(const SecretBytes& key,const std::string& text){
    struct Hash {BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}} hash;
    if(BCryptOpenAlgorithmProvider(&hash.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0||
       BCryptCreateHash(hash.algorithm,&hash.hash,nullptr,0,const_cast<PUCHAR>(key.view().data()),static_cast<ULONG>(key.view().size()),0)<0||
       BCryptHashData(hash.hash,reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),static_cast<ULONG>(text.size()),0)<0)throw std::runtime_error("Agent worker authentication unavailable");
    std::array<std::uint8_t,32> bytes{};if(BCryptFinishHash(hash.hash,bytes.data(),static_cast<ULONG>(bytes.size()),0)<0)throw std::runtime_error("Agent worker authentication unavailable");return hex(bytes);
}
bool equal_tag(const std::string& a,const std::string& b){
    if(a.size()!=64||b.size()!=64)return false;unsigned difference=0;for(std::size_t i=0;i<64;++i)difference|=static_cast<unsigned char>(a[i])^static_cast<unsigned char>(b[i]);return difference==0;
}
std::wstring wide(const std::string& text){
    if(text.empty()||text.size()>32768||text.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid worker image path");
    const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);if(!size)throw std::invalid_argument("Invalid worker image path");
    std::wstring out(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),size);return out;
}
std::wstring quote(const std::wstring& text){
    std::wstring out=L"\"";std::size_t slashes=0;for(wchar_t c:text){if(c==L'\\'){++slashes;continue;}out.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;out.push_back(c);}out.append(slashes*2,L'\\');out.push_back(L'\"');return out;
}
// Cancellation retires the actual pending OVERLAPPED before its storage or
// event is released. Partial reads/writes never cause a frame retry.
struct IoStatistics {std::atomic<std::uint64_t> pending_reads=0,pending_writes=0,cancelled_reads=0,cancelled_writes=0;};
void transfer(HANDLE pipe,void* bytes,std::size_t size,bool writing,Clock::time_point deadline,std::stop_token stop,IoStatistics* statistics=nullptr){
    std::size_t offset=0;while(offset<size){check(deadline,stop);Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event.value)throw std::runtime_error("Cannot create worker I/O event");
        OVERLAPPED overlap{};overlap.hEvent=event.value;DWORD done=0;
        const auto count=static_cast<DWORD>(std::min<std::size_t>(size-offset,65536));auto* data=static_cast<char*>(bytes)+offset;
        const BOOL immediate=writing?WriteFile(pipe,data,count,&done,&overlap):ReadFile(pipe,data,count,&done,&overlap);
        if(!immediate){if(GetLastError()!=ERROR_IO_PENDING)throw AgentWorkerProtocolError("Agent worker channel closed");
            if(statistics)++(writing?statistics->pending_writes:statistics->pending_reads);
            try{for(;;){check(deadline,stop);const auto waited=WaitForSingleObject(event.value,10);if(waited==WAIT_OBJECT_0)break;if(waited!=WAIT_TIMEOUT)throw std::runtime_error("Cannot observe worker I/O");}}
            catch(...){if(statistics)++(writing?statistics->cancelled_writes:statistics->cancelled_reads);CancelIoEx(pipe,&overlap);GetOverlappedResult(pipe,&overlap,&done,TRUE);throw;}
            if(!GetOverlappedResult(pipe,&overlap,&done,FALSE))throw AgentWorkerProtocolError("Agent worker channel closed");
        }
        if(!done||done>count)throw AgentWorkerProtocolError("Agent worker channel closed");offset+=done;
    }
}
void write_packet(HANDLE pipe,const std::string& bytes,Clock::time_point deadline,std::stop_token stop,IoStatistics* statistics=nullptr){
    if(bytes.empty()||bytes.size()>AgentWorkerFrameCodec::max_frame_bytes)protocol();
    std::array<unsigned char,4> header{};const auto size=static_cast<std::uint32_t>(bytes.size());for(unsigned i=0;i<4;++i)header[i]=static_cast<unsigned char>(size>>(8*i));
    transfer(pipe,header.data(),header.size(),true,deadline,stop,statistics);transfer(pipe,const_cast<char*>(bytes.data()),bytes.size(),true,deadline,stop,statistics);
}
std::string read_packet(HANDLE pipe,Clock::time_point deadline,std::stop_token stop,std::size_t limit=AgentWorkerFrameCodec::max_frame_bytes,IoStatistics* statistics=nullptr){
    std::array<unsigned char,4> header{};transfer(pipe,header.data(),header.size(),false,deadline,stop,statistics);std::uint32_t size=0;for(unsigned i=0;i<4;++i)size|=static_cast<std::uint32_t>(header[i])<<(8*i);
    if(!size||size>limit)protocol();std::string bytes(size,'\0');transfer(pipe,bytes.data(),bytes.size(),false,deadline,stop,statistics);return bytes;
}
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
struct AgentWorkerFrameCodec::Impl {
    AgentWorkerScope scope;SecretBytes key;std::string outgoing,incoming;std::uint64_t send=1,receive=1;std::atomic<bool> poisoned=false;
    Impl(AgentWorkerScope binding,SecretBytes secret,AgentWorkerSide side):scope(std::move(binding)),key(std::move(secret)),outgoing(side==AgentWorkerSide::backend?"backend":"worker"),incoming(side==AgentWorkerSide::backend?"worker":"backend"){
        scope_json(scope);if(key.view().size()!=32)throw std::invalid_argument("Worker channel requires a private 256-bit capability");
    }
};
AgentWorkerFrameCodec::AgentWorkerFrameCodec(AgentWorkerScope scope,SecretBytes key,AgentWorkerSide side):impl_(std::make_unique<Impl>(std::move(scope),std::move(key),side)){}
AgentWorkerFrameCodec::~AgentWorkerFrameCodec()=default;
std::string AgentWorkerFrameCodec::encode(const AgentWorkerMessage& message){
    auto& state=*impl_;if(state.poisoned||state.send==std::numeric_limits<std::uint64_t>::max())protocol();operation(message.operation);if(message.payload.size()>max_frame_bytes)protocol();
    Json body={{"version",1},{"sequence",state.send},{"direction",state.outgoing},{"scope",scope_json(state.scope)},{"operation",message.operation},{"payload",message.payload}};
    auto signed_bytes=body.dump();if(signed_bytes.size()+80>max_frame_bytes)protocol();body["mac"]=signature(state.key,signed_bytes);auto frame=body.dump();if(frame.size()>max_frame_bytes)protocol();++state.send;return frame;
}
AgentWorkerMessage AgentWorkerFrameCodec::decode(const std::string& frame){
    auto& state=*impl_;if(state.poisoned)protocol();
    try{auto body=parse(frame);if(!body.is_object()||body.size()!=7||!body.at("version").is_number_integer()||body.at("version")!=1||body.at("direction")!=state.incoming||!body.at("sequence").is_number_unsigned()||body.at("sequence").get<std::uint64_t>()!=state.receive||read_scope(body.at("scope"))!=state.scope)protocol();
        const auto tag=body.at("mac").get<std::string>();body.erase("mac");if(!equal_tag(tag,signature(state.key,body.dump())))protocol();
        AgentWorkerMessage result{body.at("operation").get<std::string>(),body.at("payload").get<std::string>()};operation(result.operation);if(state.receive==std::numeric_limits<std::uint64_t>::max())protocol();++state.receive;return result;
    }catch(...){state.poisoned=true;throw AgentWorkerProtocolError("Agent worker channel rejected its frame");}
}
SecretBytes new_agent_worker_channel_key(){std::array<std::uint8_t,32> bytes{};if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot create worker capability");SecretBytes key(bytes);SecureZeroMemory(bytes.data(),bytes.size());return key;}

struct AgentWorkerChannel::Impl {
    Handle pipe;AgentWorkerScope scope;AgentWorkerFrameCodec codec;std::mutex reader,writer;std::atomic<bool> poisoned=false;IoStatistics statistics;
    Impl(Handle handle,AgentWorkerScope binding,SecretBytes key,AgentWorkerSide side):pipe(std::move(handle)),scope(std::move(binding)),codec(scope,std::move(key),side){}
};
AgentWorkerChannel::AgentWorkerChannel(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
AgentWorkerChannel::~AgentWorkerChannel()=default;
const AgentWorkerScope& AgentWorkerChannel::scope()const noexcept{return impl_->scope;}
AgentWorkerChannelStatistics AgentWorkerChannel::statistics()const noexcept{const auto& s=impl_->statistics;return {s.pending_reads.load(),s.pending_writes.load(),s.cancelled_reads.load(),s.cancelled_writes.load()};}
void AgentWorkerChannel::close()noexcept{impl_->poisoned=true;impl_->pipe=Handle();}
void AgentWorkerChannel::send(const AgentWorkerMessage& message,Deadline deadline,std::stop_token stop){
    std::lock_guard lock(impl_->writer);if(impl_->poisoned)protocol();try{check(deadline,stop);write_packet(impl_->pipe.value,impl_->codec.encode(message),deadline,stop,&impl_->statistics);}catch(...){impl_->poisoned=true;throw;}
}
AgentWorkerMessage AgentWorkerChannel::receive(Deadline deadline,std::stop_token stop){
    std::lock_guard lock(impl_->reader);if(impl_->poisoned)protocol();try{check(deadline,stop);return impl_->codec.decode(read_packet(impl_->pipe.value,deadline,stop,AgentWorkerFrameCodec::max_frame_bytes,&impl_->statistics));}catch(...){impl_->poisoned=true;throw;}
}
std::unique_ptr<AgentWorkerChannel> AgentWorkerChannel::accept_inherited(std::uintptr_t inherited,Deadline deadline,std::stop_token stop){
    Handle pipe(reinterpret_cast<HANDLE>(inherited));if(!pipe.value||pipe.value==INVALID_HANDLE_VALUE||GetFileType(pipe.value)!=FILE_TYPE_PIPE)protocol();
    if(!SetHandleInformation(pipe.value,HANDLE_FLAG_INHERIT,0))throw std::runtime_error("Cannot close worker inheritance");
    auto bootstrap=read_packet(pipe.value,deadline,stop,8192);
    struct Wipe {std::string& bytes;~Wipe(){if(!bytes.empty())SecureZeroMemory(bytes.data(),bytes.size());}} wipe{bootstrap};
    if(bootstrap.size()<=32)protocol();SecretBytes key(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bootstrap.data()),32));
    auto config=parse(bootstrap.substr(32));if(!config.is_object()||config.size()!=2||!config.at("parent_pid").is_number_unsigned())protocol();auto scope=read_scope(config.at("scope"));
    auto channel=std::unique_ptr<AgentWorkerChannel>(new AgentWorkerChannel(std::make_unique<Impl>(std::move(pipe),std::move(scope),std::move(key),AgentWorkerSide::worker)));
    channel->send({"worker.ready",Json{{"pid",GetCurrentProcessId()},{"creation_time",birth(GetCurrentProcess())},{"parent_pid",config.at("parent_pid")}}.dump()},deadline,stop);return channel;
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
    auto state=std::make_unique<Impl>();state->executable=std::move(executable);auto key=new_agent_worker_channel_key();
    auto pipe_nonce=new_agent_worker_channel_key();const auto name=L"\\\\.\\pipe\\xmind-agent-"+wide(hex(pipe_nonce.view()));pipe_nonce.clear();
    Handle server(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,nullptr));
    if(server.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create private worker pipe");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};Handle child(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,&security,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));if(child.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open private worker pipe");
    Handle connected(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!connected.value)throw std::runtime_error("Cannot create worker connection event");OVERLAPPED connection{};connection.hEvent=connected.value;
    if(!ConnectNamedPipe(server.value,&connection)){const auto error=GetLastError();if(error!=ERROR_PIPE_CONNECTED){if(error==ERROR_IO_PENDING){DWORD ignored=0;CancelIoEx(server.value,&connection);GetOverlappedResult(server.value,&connection,&ignored,TRUE);}throw std::runtime_error("Cannot connect private worker pipe");}}
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
    std::string bootstrap(reinterpret_cast<const char*>(key.view().data()),key.view().size());bootstrap+=Json{{"scope",scope_json(scope)},{"parent_pid",GetCurrentProcessId()}}.dump();
    try{write_packet(server.value,bootstrap,deadline,stop);}catch(...){SecureZeroMemory(bootstrap.data(),bootstrap.size());throw;}SecureZeroMemory(bootstrap.data(),bootstrap.size());
    state->channel=std::unique_ptr<AgentWorkerChannel>(new AgentWorkerChannel(std::make_unique<AgentWorkerChannel::Impl>(std::move(server),std::move(scope),std::move(key),AgentWorkerSide::backend)));
    const auto ready=state->channel->receive(deadline,stop);const auto identity=parse(ready.payload);if(ready.operation!="worker.ready"||!identity.is_object()||identity.size()!=3||identity.at("pid")!=state->pid||identity.at("creation_time")!=state->created||identity.at("parent_pid")!=GetCurrentProcessId())protocol();
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
