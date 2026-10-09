#include "agentflow/runtime_generation.hpp"
#include "agentflow/context_records.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace agentflow {
namespace {
using Json=nlohmann::json;
[[noreturn]] void denied(){throw RuntimeGenerationDenied("Native runtime generation verification failed");}
void require(bool value){if(!value)denied();}
bool hex(const std::string& value,std::size_t count){return value.size()==count&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::wstring wide(const std::string& value){require(!value.empty()&&value.size()<=32768&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32;}));const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);require(size>0);std::wstring result(size,L'\0');require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size)==size);return result;}
std::string utf8(const std::wstring& value){const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);require(size>0);std::string result(size,'\0');require(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr)==size);return result;}
std::wstring absolute(const std::string& value){auto input=wide(value);if(input.starts_with(L"\\\\?\\"))input.erase(0,4);require(input.size()>=3&&((input[0]>=L'A'&&input[0]<=L'Z')||(input[0]>=L'a'&&input[0]<=L'z'))&&input[1]==L':'&&(input[2]==L'\\'||input[2]==L'/'));const auto size=GetFullPathNameW(input.c_str(),0,nullptr,nullptr);require(size>0&&size<32768);std::wstring result(size,L'\0');const auto used=GetFullPathNameW(input.c_str(),size,result.data(),nullptr);require(used>0&&used<size);result.resize(used);while(result.size()>3&&result.back()==L'\\')result.pop_back();return L"\\\\?\\"+result;}
bool same(const std::wstring& a,const std::wstring& b){return a.size()==b.size()&&CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;}
std::wstring final_name(HANDLE handle){const auto size=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);require(size>0&&size<32768);std::wstring result(size,L'\0');const auto used=GetFinalPathNameByHandleW(handle,result.data(),size,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);require(used>0&&used<size);result.resize(used);require(result.starts_with(L"\\\\?\\")&&!result.starts_with(L"\\\\?\\UNC\\"));return result;}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;BY_HANDLE_FILE_INFORMATION info{};std::wstring path;bool directory=false;
    Handle(const std::wstring& name,bool dir):directory(dir){value=CreateFileW(name.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(dir?FILE_FLAG_BACKUP_SEMANTICS:FILE_FLAG_SEQUENTIAL_SCAN),nullptr);if(value==INVALID_HANDLE_VALUE)denied();try{require(GetFileInformationByHandle(value,&info)!=0);require(!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)==dir&&(dir||info.nNumberOfLinks==1));path=final_name(value);require(same(path,name));}catch(...){CloseHandle(value);value=INVALID_HANDLE_VALUE;throw;}}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(Handle&& other)noexcept:value(other.value),info(other.info),path(std::move(other.path)),directory(other.directory){other.value=INVALID_HANDLE_VALUE;}
    Handle& operator=(Handle&&)=delete;Handle(const Handle&)=delete;
    std::uint64_t size()const{return (static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow;}
    std::string identity()const{return "windows-runtime-file-v1:"+std::to_string(info.dwVolumeSerialNumber)+":"+std::to_string((static_cast<std::uint64_t>(info.nFileIndexHigh)<<32)|info.nFileIndexLow);}
    void stable(bool contents=true)const{BY_HANDLE_FILE_INFORMATION now{};require(GetFileInformationByHandle(value,&now)!=0&&now.dwVolumeSerialNumber==info.dwVolumeSerialNumber&&now.nFileIndexHigh==info.nFileIndexHigh&&now.nFileIndexLow==info.nFileIndexLow&&(directory||now.nNumberOfLinks==info.nNumberOfLinks)&&(!contents||(now.nFileSizeHigh==info.nFileSizeHigh&&now.nFileSizeLow==info.nFileSizeLow&&now.ftLastWriteTime.dwHighDateTime==info.ftLastWriteTime.dwHighDateTime&&now.ftLastWriteTime.dwLowDateTime==info.ftLastWriteTime.dwLowDateTime))&&same(final_name(value),path));}
    std::string bytes(std::uint64_t limit)const{require(!directory&&size()<=limit);LARGE_INTEGER start{};require(SetFilePointerEx(value,start,nullptr,FILE_BEGIN)!=0);std::string result(static_cast<std::size_t>(size()),'\0');std::size_t offset=0;while(offset<result.size()){DWORD read=0;const auto amount=static_cast<DWORD>(std::min<std::size_t>(1024*1024,result.size()-offset));require(ReadFile(value,result.data()+offset,amount,&read,nullptr)!=0&&read>0&&read<=amount);offset+=read;}stable();return result;}
};

const std::array<std::string,5> native_files{"xmind.exe","xlang3_runtime.dll","xlang3.exe","modules/xlang_json.x3pkg.dll","modules/xlang_sqlite3.x3pkg.dll"};
bool relative(const std::string& value){if(value.empty()||value.size()>1024||value.front()=='/'||value.back()=='/'||value.find_first_of("\\:")!=std::string::npos)return false;wide(value);std::size_t from=0;while(from<value.size()){const auto to=value.find('/',from);auto part=value.substr(from,to==std::string::npos?value.size()-from:to-from);if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')return false;std::transform(part.begin(),part.end(),part.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});if(part==".git"||part==".config"||part==".agentflow"||part=="__pycache__"||part=="site-packages")return false;if(to==std::string::npos)break;from=to+1;}return true;}
struct OrdinalNameLess {bool operator()(const std::wstring& a,const std::wstring& b)const{const auto result=CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE);require(result!=0);return result==CSTR_LESS_THAN;}};
std::string folded(std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
bool allowed(const std::string& name){if(std::find(native_files.begin(),native_files.end(),name)!=native_files.end())return true;if(name.starts_with("stdlib/")&&name.ends_with(".py"))return true;if(name.starts_with("licenses/")){const auto lower=folded(name);for(const auto* extension:{".exe",".dll",".pyd",".pyc",".pyo"})if(lower.ends_with(extension))return false;return true;}return name=="README.txt"||name=="provenance.json";}
std::wstring child(const std::wstring& root,const std::string& name){auto result=wide(name);std::replace(result.begin(),result.end(),L'/',L'\\');return root+L"\\"+result;}
std::set<std::string> inventory(const std::wstring& root,std::vector<Handle>* directories=nullptr,const std::string& prefix={},std::size_t depth=0,std::size_t* visited=nullptr){
    std::size_t local=0;if(!visited)visited=&local;require(depth<=32);
    std::set<std::string> result;WIN32_FIND_DATAW data{};const auto parent=prefix.empty()?root:child(root,prefix);HANDLE search=FindFirstFileW((parent+L"\\*").c_str(),&data);require(search!=INVALID_HANDLE_VALUE);try{for(;;){const std::wstring leaf=data.cFileName;if(leaf!=L"."&&leaf!=L".."){require(++*visited<=40001);const auto name=(prefix.empty()?std::string{}:prefix+"/")+utf8(leaf);require(relative(name)&&!(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT));if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){Handle held(child(root,name),true);auto nested=inventory(root,directories,name,depth+1,visited);require(result.size()+nested.size()<=20001);result.insert(nested.begin(),nested.end());if(directories)directories->push_back(std::move(held));}else{require(!(data.dwFileAttributes&FILE_ATTRIBUTE_DEVICE)&&result.size()<20001);require(result.insert(name).second);}}if(!FindNextFileW(search,&data)){require(GetLastError()==ERROR_NO_MORE_FILES);break;}}FindClose(search);return result;}catch(...){FindClose(search);throw;}}
}
struct VerifiedRuntimeGeneration::Impl {
    RuntimeGenerationBinding binding;Handle root;std::vector<Handle> ancestors,directories,files;std::map<std::string,std::size_t> positions;std::set<std::string> expected;std::string manifest;std::size_t total=0;mutable std::mutex verification;
    Impl(const std::string& directory,const std::string& digest,const std::string& excluded):root(absolute(directory),true){
        require(hex(digest,64));for(std::size_t separator=7;(separator=root.path.find(L'\\',separator))!=std::wstring::npos;++separator)ancestors.emplace_back(root.path.substr(0,separator),true);if(!excluded.empty()){Handle workspace(absolute(excluded),true);const auto& path=workspace.path;require(!same(root.path,path)&&!(root.path.size()>path.size()&&same(root.path.substr(0,path.size()),path)&&root.path[path.size()]==L'\\'));}
        Handle source(child(root.path,"native-runtime-manifest.json"),false);manifest=source.bytes(4*1024*1024);require(context_digest(manifest)==digest);auto input=manifest;if(input.starts_with("\xef\xbb\xbf"))input.erase(0,3);std::vector<std::set<std::string>> keys;const auto j=Json::parse(input,[&](int depth,Json::parse_event_t event,Json& parsed){require(depth<=8);if(event==Json::parse_event_t::object_start)keys.emplace_back();else if(event==Json::parse_event_t::object_end)keys.pop_back();else if(event==Json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(parsed.get<std::string>()).second);return true;});
        require(j.is_object()&&j.at("schemaVersion").is_number_integer()&&j.at("schemaVersion")==1&&j.at("platform")=="win32"&&j.at("arch")=="x64"&&j.at("bridgeEnabled").is_boolean()&&j.at("bridgeEnabled")==false);
        binding={utf8(root.path.substr(4)),root.identity(),digest,j.at("nativeRevision").get<std::string>(),j.at("sdkRevision").get<std::string>(),j.at("sourceManifestSha256").get<std::string>(),{}};require(hex(binding.native_revision,40)&&hex(binding.sdk_revision,40)&&hex(binding.source_manifest_sha256,64));const auto& entries=j.at("files");require(entries.is_object()&&entries.size()>=native_files.size()+4&&entries.size()<=20000);std::set<std::wstring,OrdinalNameLess> names;bool license=false;
        for(auto it=entries.begin();it!=entries.end();++it){require(relative(it.key())&&allowed(it.key())&&it.value().is_string()&&hex(it.value().get<std::string>(),64)&&names.insert(wide(it.key())).second);expected.insert(it.key());license|=it.key().starts_with("licenses/");}
        for(const auto& name:native_files)require(entries.contains(name));for(const auto* name:{"os.py","json/__init__.py","encodings/__init__.py","importlib/__init__.py"})require(entries.contains(std::string("stdlib/")+name));require(license);expected.insert("native-runtime-manifest.json");require(inventory(root.path,&directories)==expected);files.reserve(entries.size()+1);
        for(auto it=entries.begin();it!=entries.end();++it){Handle file(child(root.path,it.key()),false);require(file.size()<=128*1024*1024&&total+file.size()<=256*1024*1024);total+=static_cast<std::size_t>(file.size());const auto hash=context_digest(file.bytes(128*1024*1024));require(hash==it.value().get<std::string>());positions[it.key()]=files.size();if(it.key()=="xmind.exe")binding.server_sha256=hash;files.push_back(std::move(file));}
        positions["native-runtime-manifest.json"]=files.size();files.push_back(std::move(source));revalidate();
    }
    void revalidate()const{std::lock_guard lock(verification);for(const auto& handle:ancestors)handle.stable(false);root.stable();for(const auto& handle:directories)handle.stable();for(const auto& handle:files)handle.stable();require(inventory(root.path)==expected);require(files.at(positions.at("native-runtime-manifest.json")).bytes(4*1024*1024)==manifest);}
    void current()const{revalidate();std::wstring image(32768,L'\0');const auto size=GetModuleFileNameW(nullptr,image.data(),static_cast<DWORD>(image.size()));require(size>0&&size<image.size());image.resize(size);Handle actual(absolute(utf8(image)),false);const auto& server=files.at(positions.at("xmind.exe"));require(actual.identity()==server.identity()&&same(actual.path,server.path)&&context_digest(actual.bytes(128*1024*1024))==binding.server_sha256);}
};
VerifiedRuntimeGeneration::VerifiedRuntimeGeneration(const std::string& root,const std::string& digest,const std::string& excluded){try{impl_=std::make_unique<Impl>(root,digest,excluded);}catch(...){denied();}}
VerifiedRuntimeGeneration::~VerifiedRuntimeGeneration()=default;
VerifiedRuntimeGeneration::VerifiedRuntimeGeneration(VerifiedRuntimeGeneration&&) noexcept=default;
VerifiedRuntimeGeneration& VerifiedRuntimeGeneration::operator=(VerifiedRuntimeGeneration&&) noexcept=default;
const RuntimeGenerationBinding& VerifiedRuntimeGeneration::binding()const{if(!impl_)denied();return impl_->binding;}
void VerifiedRuntimeGeneration::revalidate()const{try{if(!impl_)denied();impl_->revalidate();}catch(...){denied();}}
void VerifiedRuntimeGeneration::require_current_server()const{try{if(!impl_)denied();impl_->current();}catch(...){denied();}}
}
