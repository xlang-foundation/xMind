#include "../src/profile_record_publication.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <future>

namespace fs=std::filesystem;
using namespace std::chrono_literals;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Reader{
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Reader(const fs::path& file):value(CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)){
        require(value!=INVALID_HANDLE_VALUE,"Cannot open deny-delete fixture reader");
    }
    void close(){if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);value=INVALID_HANDLE_VALUE;}}
    ~Reader(){close();}
};
int main(){
    fs::path base;
    try{
        const auto parent=fs::canonical(fs::temp_directory_path());
        base=parent/(L"xmind-profile-publication-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        require(fs::create_directory(base),"Cannot create isolated publication fixture");
        const auto target=base/L"profile.state",staged=base/L"state.tmp";
        std::ofstream(target)<<"old ciphertext";std::ofstream(staged)<<"new ciphertext";
        {
            Reader held(target);
            const auto moved=MoveFileExW(staged.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
            const auto error=GetLastError();
            require(!moved&&(error==ERROR_SHARING_VIOLATION||error==ERROR_ACCESS_DENIED)&&
                    agentflow::detail::profile_record_reader_blocks_delete(target.native()),
                    "Fixture must reproduce a real Windows deny-delete reader conflict");
            auto publishing=std::async(std::launch::async,[&]{agentflow::detail::publish_profile_record(staged.native(),target.native(),2s);});
            require(publishing.wait_for(100ms)==std::future_status::timeout,"Publication cannot bypass the held reader");
            held.close();publishing.get();
        }
        require(read(target)=="new ciphertext"&&!fs::exists(staged),"Atomic publication must use the exact staged bytes");
        {
            const auto race_target=base/L"race-profile.state",race_staged=base/L"race-state.tmp";
            std::ofstream(race_target)<<"race old ciphertext";std::ofstream(race_staged)<<"race new ciphertext";
            Reader held(race_target);std::size_t attempts=0;
            agentflow::detail::publish_profile_record_with(race_staged.native(),race_target.native(),2s,
                [&](const std::wstring& from,const std::wstring& to){
                    ++attempts;const auto moved=MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
                    if(attempts==1){
                        require(!moved&&agentflow::detail::profile_record_reader_blocks_delete(to),"Race fixture must first observe an actual Windows deny-delete reader");
                        held.close();
                        // Windows may report ACCESS_DENIED for this conflict.
                        // Inject that documented error class after releasing
                        // the real reader, before the publisher's access probe.
                        SetLastError(ERROR_ACCESS_DENIED);
                    }
                    return moved;
                });
            require(attempts==2&&read(race_target)=="race new ciphertext"&&!fs::exists(race_staged),"A reader released before the probe must permit one exact atomic replacement retry");
        }
        std::ofstream(staged)<<"next ciphertext";
        {
            Reader held(target);bool denied=false;
            try{agentflow::detail::publish_profile_record(staged.native(),target.native(),50ms);}
            catch(const std::runtime_error& error){const auto message=std::string(error.what());denied=message.find("Win32 32")!=std::string::npos||message.find("Win32 5")!=std::string::npos;}
            require(denied,"Persistent sharing conflict must report its Windows code");
            require(read(target)=="new ciphertext"&&read(staged)=="next ciphertext","Failed publication must preserve both files");
        }
        require(SetFileAttributesW(target.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"Cannot make read-only failure fixture");
        bool denied=false;const auto started=std::chrono::steady_clock::now();
        try{agentflow::detail::publish_profile_record(staged.native(),target.native(),2s);}
        catch(const std::runtime_error& error){denied=std::string(error.what()).find("Win32 5")!=std::string::npos;}
        require(SetFileAttributesW(target.c_str(),FILE_ATTRIBUTE_NORMAL)!=0,"Cannot restore fixture attributes");
        require(denied&&std::chrono::steady_clock::now()-started<1s,"Permission errors must fail without sharing retries");
        require(read(target)=="new ciphertext"&&read(staged)=="next ciphertext","Permission denial must retain staged and current bytes");
        require(fs::canonical(base)==base&&base.parent_path()==parent&&!fs::is_symlink(base),"Cleanup must remain in the isolated fixture");
        fs::remove_all(base);
        std::cout<<"Actual Windows reader conflict, bounded atomic publication and permission denial passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"; fixture retained at "<<base<<'\n';return 1;}
}
