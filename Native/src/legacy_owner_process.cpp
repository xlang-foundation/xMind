#include "agentflow/legacy_owner_process.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "agentflow/context_records.hpp"
#include "agentflow/owner_process.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <wbemidl.h>
#include <charconv>
#include <algorithm>
#include <map>
#include <vector>

namespace agentflow {
namespace {
void require(bool yes){if(!yes)throw std::runtime_error("Legacy owner process preflight failed");}
bool hex(const std::string& s,std::size_t size){return s.size()==size&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::wstring wide(const std::string& s){require(!s.empty()&&s.size()<=32768&&s.find('\0')==std::string::npos);const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);require(n>0);std::wstring r(n,L'\0');require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n)==n);return r;}
std::string utf8(const std::wstring& s){const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);require(n>0);std::string r(n,'\0');require(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr)==n);return r;}
struct Handle{HANDLE value=INVALID_HANDLE_VALUE;explicit Handle(HANDLE h):value(h){require(h&&h!=INVALID_HANDLE_VALUE);}~Handle(){CloseHandle(value);}Handle(const Handle&)=delete;};
struct File:Handle{
    BY_HANDLE_FILE_INFORMATION info{};
    File(const std::string& p,bool image,bool directory=false):Handle(CreateFileW(wide(p).c_str(),image?GENERIC_READ:FILE_READ_ATTRIBUTES,image?FILE_SHARE_READ:FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr)){
        require(GetFileInformationByHandle(value,&info)!=0&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory);
    }
    bool same(const File& other)const{return info.dwVolumeSerialNumber==other.info.dwVolumeSerialNumber&&info.nFileIndexHigh==other.info.nFileIndexHigh&&info.nFileIndexLow==other.info.nFileIndexLow;}
    std::string path()const{const auto n=GetFinalPathNameByHandleW(value,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);require(n>0&&n<32768);std::wstring r(n,L'\0');const auto used=GetFinalPathNameByHandleW(value,r.data(),n,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);require(used>0&&used<n);r.resize(used);if(r.starts_with(L"\\\\?\\UNC\\"))r=L"\\\\"+r.substr(8);else if(r.starts_with(L"\\\\?\\"))r.erase(0,4);return utf8(r);}
    std::string digest()const{LARGE_INTEGER size{},start{};require(GetFileSizeEx(value,&size)&&size.QuadPart>0&&size.QuadPart<=128*1024*1024&&SetFilePointerEx(value,start,nullptr,FILE_BEGIN));std::string data(static_cast<std::size_t>(size.QuadPart),'\0');std::size_t offset=0;while(offset<data.size()){DWORD used=0;require(ReadFile(value,data.data()+offset,static_cast<DWORD>(std::min<std::size_t>(65536,data.size()-offset)),&used,nullptr)&&used>0);offset+=used;}return context_digest(data);}
};
std::uint32_t listener(std::uint16_t port){
    require(port>0);DWORD bytes=0;auto result=GetExtendedTcpTable(nullptr,&bytes,FALSE,AF_INET,TCP_TABLE_OWNER_PID_LISTENER,0);require(result==ERROR_INSUFFICIENT_BUFFER&&bytes>=sizeof(DWORD)&&bytes<=8*1024*1024);
    for(int attempt=0;attempt<3;++attempt){std::vector<unsigned char> data(bytes);result=GetExtendedTcpTable(data.data(),&bytes,FALSE,AF_INET,TCP_TABLE_OWNER_PID_LISTENER,0);if(result==ERROR_INSUFFICIENT_BUFFER){require(bytes<=8*1024*1024);continue;}require(result==NO_ERROR);const auto* table=reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(data.data());require(table->dwNumEntries<=(data.size()-sizeof(DWORD))/sizeof(MIB_TCPROW_OWNER_PID));std::uint32_t pid=0;for(DWORD i=0;i<table->dwNumEntries;++i){const auto& row=table->table[i];if(row.dwLocalAddr==htonl(0x7f000001)&&ntohs(static_cast<u_short>(row.dwLocalPort))==port){require(!pid);pid=row.dwOwningPid;}}require(pid!=0);return pid;}
    throw std::runtime_error("Legacy listener changed during inspection");
}
std::string birth(HANDLE process){FILETIME c{},e{},k{},u{};require(GetProcessTimes(process,&c,&e,&k,&u)!=0);return std::to_string((static_cast<std::uint64_t>(c.dwHighDateTime)<<32)|c.dwLowDateTime);}
std::string image_path(HANDLE process){std::wstring path(32768,L'\0');DWORD size=static_cast<DWORD>(path.size());require(QueryFullProcessImageNameW(process,0,path.data(),&size)&&size>0);path.resize(size);return utf8(path);}
template<class T>struct Com{T* value=nullptr;~Com(){if(value)value->Release();}T** out(){return &value;}T* operator->()const{return value;}};
struct Bstr{BSTR value;explicit Bstr(const wchar_t* s):value(SysAllocString(s)){require(value!=nullptr);}~Bstr(){SysFreeString(value);}};
std::wstring command_line(std::uint32_t pid){
    const auto init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);require(SUCCEEDED(init));struct Uninit{~Uninit(){CoUninitialize();}}uninit;
    Com<IWbemLocator> locator;require(SUCCEEDED(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_IWbemLocator,reinterpret_cast<void**>(locator.out()))));
    Bstr name(L"ROOT\\CIMV2");Com<IWbemServices> services;require(SUCCEEDED(locator->ConnectServer(name.value,nullptr,nullptr,nullptr,0,nullptr,nullptr,services.out())));
    require(SUCCEEDED(CoSetProxyBlanket(services.value,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE)));
    Bstr language(L"WQL"),query((L"SELECT CommandLine FROM Win32_Process WHERE ProcessId="+std::to_wstring(pid)).c_str());Com<IEnumWbemClassObject> results;
    require(SUCCEEDED(services->ExecQuery(language.value,query.value,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,results.out())));
    Com<IWbemClassObject> object;ULONG count=0;require(SUCCEEDED(results->Next(3000,1,object.out(),&count))&&count==1);
    VARIANT value;VariantInit(&value);struct Clear{VARIANT* p;~Clear(){VariantClear(p);}}clear{&value};require(SUCCEEDED(object->Get(L"CommandLine",0,&value,nullptr,nullptr))&&value.vt==VT_BSTR&&value.bstrVal);
    const auto length=SysStringLen(value.bstrVal);require(length>0&&length<32768);return std::wstring(value.bstrVal,length);
}
void arguments(std::uint32_t pid,std::uint16_t port,const File& database,const File& workspace){
    const auto raw=command_line(pid);int count=0;auto* argv=CommandLineToArgvW(raw.c_str(),&count);require(argv&&count>=7&&count<=128);struct Free{LPWSTR* p;~Free(){LocalFree(p);}}free{argv};
    std::map<std::wstring,std::wstring> options;for(int i=1;i<count;i+=2){require(i+1<count&&std::wstring(argv[i]).starts_with(L"--")&&options.emplace(argv[i],argv[i+1]).second);}
    require(options.contains(L"--db")&&options.contains(L"--workspace")&&options.contains(L"--port"));
    File db(utf8(options.at(L"--db")),false),ws(utf8(options.at(L"--workspace")),false,true);require(database.same(db)&&workspace.same(ws));
    const auto number=utf8(options.at(L"--port"));std::uint32_t actual=0;const auto parsed=std::from_chars(number.data(),number.data()+number.size(),actual);require(parsed.ec==std::errc{}&&parsed.ptr==number.data()+number.size()&&(actual==0||actual==port));
    require(!options.contains(L"--runtime-manifest-sha256")); // Modern owners use their authenticated retirement protocol.
}
}
struct VerifiedLegacyOwnerProcess::Impl {
    std::uint16_t port;std::uint32_t pid;Handle process;File image,database,workspace;SecretBytes token;
    std::string hash,workspace_id,authority_id;LegacyOwnerProcessObservation observed;
    Impl(std::uint16_t p,const std::string& path,const std::string& digest,const std::string& db,const std::string& ws,const std::string& id,const std::string& authority,const SecretBytes& secret)
      :port(p),pid(listener(p)),process(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)),image(path,true),database(db,false),workspace(ws,false,true),token(secret.view()),hash(digest),workspace_id(id),authority_id(authority){
        require(hex(hash,64)&&hex(authority_id,32)&&workspace_id.starts_with("windows-local-file-v1:")&&workspace_id.size()<=256&&token.view().size()>=32&&token.view().size()<=256);
        for(const auto c:token.view())require(c>=33&&c<=126);
        File actual(image_path(process.value),true);require(image.same(actual)&&image.digest()==hash);arguments(pid,port,database,workspace);
        observed={{pid,birth(process.value),hash},image.path(),database.path()};revalidate();
    }
    void revalidate()const{
        require(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT&&birth(process.value)==observed.source.process_birth&&listener(port)==pid);
        File actual(image_path(process.value),true);require(image.same(actual)&&image.digest()==hash);arguments(pid,port,database,workspace);
        HttpStreamRequest request;request.url="http://127.0.0.1:"+std::to_string(port)+"/v1/workspace";request.deadline=std::chrono::seconds(5);request.idle_timeout=std::chrono::seconds(5);
        const auto value=nlohmann::json::parse(get_json(request,&token));require(value.is_object()&&value.size()==4&&value.at("configured")==true&&value.at("workspace_id")==workspace_id&&value.at("authority_id")==authority_id);
        File ws(value.at("root").get<std::string>(),false,true);require(workspace.same(ws));
        require(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT&&listener(port)==pid&&birth(process.value)==observed.source.process_birth);
    }
};
LegacyOwnerProcessObservation discover_legacy_listener(std::uint16_t port){
    const auto pid=listener(port);Handle process(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));require(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT);
    File image(image_path(process.value),true);const auto created=birth(process.value);const auto hash=image.digest();require(listener(port)==pid&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT&&birth(process.value)==created);
    return {{pid,created,hash},image.path(),{}};
}
VerifiedLegacyOwnerProcess::VerifiedLegacyOwnerProcess(std::uint16_t port,const std::string& image,const std::string& hash,const std::string& db,const std::string& ws,const std::string& id,const std::string& authority,const SecretBytes& token):impl_(std::make_unique<Impl>(port,image,hash,db,ws,id,authority,token)){}
VerifiedLegacyOwnerProcess::~VerifiedLegacyOwnerProcess()=default;
const LegacyOwnerProcessObservation& VerifiedLegacyOwnerProcess::observation()const{return impl_->observed;}
void VerifiedLegacyOwnerProcess::revalidate()const{impl_->revalidate();}
void VerifiedLegacyOwnerProcess::stop_and_prepare(const LegacyOwnerBootstrap& boot,const std::vector<std::string>& roots){
    require(boot.target.workspace_id==impl_->workspace_id);
    File target_workspace(boot.target.workspace_root,false,true);require(target_workspace.same(impl_->workspace));
    const auto bytes=impl_->token.view();std::string auth(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    const auto binding=context_digest("xMind.owner-auth.v1:"+auth);SecureZeroMemory(auth.data(),auth.size());require(binding==boot.target.auth_binding);
    // Validate every ticket/target field before opening a termination handle.
    require(hex(boot.ticket_id,32));encode_backend_owner_target(boot.target);
    XlangSqlite db(impl_->observed.database_path,roots);db.begin();bool stopped=false;
    try{
        require_legacy_database_idle(db);require(!legacy_owner_record(db));const auto snapshot=snapshot_legacy_database(db);
        impl_->revalidate();
        Handle termination(OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,impl_->pid));
        require(birth(termination.value)==impl_->observed.source.process_birth&&WaitForSingleObject(termination.value,0)==WAIT_TIMEOUT);
        File actual(image_path(termination.value),true);require(actual.same(impl_->image)&&actual.digest()==impl_->hash);
        require(TerminateProcess(termination.value,0x584d0001)!=0);stopped=true;
        require(WaitForSingleObject(termination.value,10000)==WAIT_OBJECT_0&&observe_owner_exit(impl_->pid,impl_->observed.source.process_birth,0).exited);
        BackendLease lease(impl_->observed.database_path);
        File canonical(lease.canonical_database_path(),false);require(canonical.same(impl_->database));
        publish_legacy_owner_ticket(db,lease,boot,impl_->observed.source,snapshot);db.commit();
    }catch(...){
        const auto original=std::current_exception();bool rolled_back=false;try{db.rollback();rolled_back=true;}catch(...){}
        if(stopped)throw std::runtime_error("Legacy source was stopped, but migration preparation is unverified. Saved storage was retained; operator recovery is required and no automatic restart occurred.");
        if(!rolled_back)throw std::runtime_error("Legacy source was not stopped, but SQLite rollback failed. Saved storage was retained; operator recovery is required.");
        std::rethrow_exception(original);
    }
}
}
