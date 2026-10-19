// A controlled diagnostic of the actual native adapter. No model, persistence,
// profile authority or agent-worker acceptance is implied by these launches.
#include "agentflow/process.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
using namespace agentflow;using Json=nlohmann::json;
namespace {
std::string utf8(const std::wstring& value){const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(!size)throw std::runtime_error("Diagnostic path conversion failed");std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);return result;}
std::string read(const std::filesystem::path& file){std::ifstream stream(file,std::ios::binary);if(!stream)throw std::runtime_error("Diagnostic marker could not be read");return {std::istreambuf_iterator<char>(stream),{}};}
std::string digest(const std::filesystem::path& file){
    if(std::filesystem::file_size(file)==0)return "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const auto identity=ForegroundProcess::executable_identity(utf8(file.native()));const auto value=identity.substr(identity.rfind(':')+1);if(value.size()!=64)throw std::runtime_error("Diagnostic file digest unavailable");return value;
}
Json copy_core_modules(const std::filesystem::path& home,const std::filesystem::path& target){
    std::filesystem::create_directory(target);Json files=Json::array();std::uintmax_t bytes=0;
    for(const auto* name:{L"Microsoft.PowerShell.Utility",L"Microsoft.PowerShell.Management"}){
        const auto source=home/L"Modules"/name;const auto attributes=GetFileAttributesW(source.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Diagnostic module source must be a regular directory");
        for(const auto& entry:std::filesystem::recursive_directory_iterator(source)){
            const auto info=GetFileAttributesW(entry.path().c_str());if(info==INVALID_FILE_ATTRIBUTES||(info&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Diagnostic module cannot contain reparses");
            const auto relative=entry.path().lexically_relative(home/L"Modules"),destination=target/relative;
            if(entry.is_directory()){std::filesystem::create_directories(destination);continue;}
            if(!entry.is_regular_file()||files.size()>=256||(bytes+=entry.file_size())>16*1024*1024)throw std::runtime_error("Diagnostic module copy exceeds limits");
            const auto before=digest(entry.path());std::filesystem::create_directories(destination.parent_path());std::filesystem::copy_file(entry.path(),destination);
            if(digest(entry.path())!=before||digest(destination)!=before)throw std::runtime_error("Diagnostic module copy changed");
            files.push_back({{"relative_path",utf8(relative.native())},{"bytes",entry.file_size()},{"sha256",before}});
        }
    }
    return files;
}
}
int main(int argc,char** argv){if(argc!=4)return 2;try{
    const std::string scenario=argv[1],control=argv[2];const auto folder=std::filesystem::canonical(std::filesystem::u8path(argv[3]));
    std::wstring system(32768,L'\0');const auto length=GetSystemDirectoryW(system.data(),static_cast<UINT>(system.size()));if(!length||length>=system.size())throw std::runtime_error("Diagnostic system path unavailable");system.resize(length);const auto home=std::filesystem::path(system)/L"WindowsPowerShell"/L"v1.0";
    ProcessConfiguration config;config.executable=utf8((home/L"powershell.exe").native());config.executable_id=ForegroundProcess::executable_identity(config.executable);config.working_directory=utf8(folder.native());config.working_directory_id=ForegroundProcess::directory_identity(config.working_directory);config.workspace_root=config.working_directory;config.workspace_root_id=config.working_directory_id;config.timeout=std::chrono::milliseconds(30000);config.output_limit=65536;
    // The two originals are checked against the agent/graph fixture sources by
    // the driver. Controls never replace their product acceptance contracts.
    std::string command,marker;
    if(scenario=="graph"){command="[Console]::Out.WriteLine('xmind-graph-phase:script_entered'); Start-Sleep -Seconds 6; [Console]::Out.WriteLine('xmind-graph-phase:delay_completed'); [IO.File]::WriteAllText('graph-process-folder.txt',(Get-Location).Path); [Console]::Out.WriteLine('xmind-graph-phase:effect_completed')";marker="graph-process-folder.txt";}
    else if(scenario=="agent"){command="[IO.File]::WriteAllText('agent-process-folder.txt',(Get-Location).Path)";marker="agent-process-folder.txt";}
    else if(scenario=="dotnet-control"){command="[Console]::Out.WriteLine('xmind-graph-phase:script_entered'); [Threading.Thread]::Sleep(6000); [Console]::Out.WriteLine('xmind-graph-phase:delay_completed'); [IO.File]::WriteAllText('graph-process-folder.txt',[IO.Directory]::GetCurrentDirectory()); [Console]::Out.WriteLine('xmind-graph-phase:effect_completed')";marker="graph-process-folder.txt";}
    else if(scenario=="cmdlet-phases"){command="[Console]::Out.WriteLine('xmind-graph-phase:script_entered'); Start-Sleep -Seconds 6; [Console]::Out.WriteLine('xmind-graph-phase:delay_completed'); $location=Get-Location; [Console]::Out.WriteLine('xmind-probe:location_resolved'); [IO.File]::WriteAllText('graph-process-folder.txt',$location.Path); [Console]::Out.WriteLine('xmind-graph-phase:effect_completed')";marker="graph-process-folder.txt";}
    else if(scenario=="module-qualified"){command="[Console]::Out.WriteLine('xmind-graph-phase:script_entered'); Microsoft.PowerShell.Utility\\Start-Sleep -Seconds 6; [Console]::Out.WriteLine('xmind-graph-phase:delay_completed'); [IO.File]::WriteAllText('graph-process-folder.txt',(Microsoft.PowerShell.Management\\Get-Location).Path); [Console]::Out.WriteLine('xmind-graph-phase:effect_completed')";marker="graph-process-folder.txt";}
    else throw std::invalid_argument("Unknown diagnostic scenario");
    Json copied=Json::array();const auto core=folder/L"core-modules";
    if(control=="core-only"||control=="precopy-core"){copied=copy_core_modules(home,core);if(control=="core-only")config.environment.emplace_back("PSModulePath",utf8(core.native()));}
    else if(control=="system-modules")config.environment.emplace_back("PSModulePath",utf8((home/L"Modules").native()));
    else if(control=="private-cache")config.environment.emplace_back("PSModuleAnalysisCachePath",utf8((folder/L"ModuleAnalysisCache").native()));
    else if(control=="cache-off")config.environment.emplace_back("PSModuleAnalysisCachePath","nul");
    // Only this throwaway probe process changes its inherited PATH. Production
    // command profiles still retain the full trusted backend environment.
    else if(control=="minimal-path"){const auto value=system+L";"+home.native();if(!SetEnvironmentVariableW(L"PATH",value.c_str()))throw std::runtime_error("Diagnostic-only parent PATH control failed");}
    else if(control!="default")throw std::invalid_argument("Unknown diagnostic control");
    if(std::filesystem::exists(folder/marker))throw std::invalid_argument("Diagnostic marker must not preexist");
    config.arguments={"-NoLogo","-NoProfile","-NonInteractive","-Command",command};
    Json observed=Json::array();std::mutex observed_mutex;const auto launched=std::chrono::steady_clock::now();
    const auto result=ForegroundProcess::run(config,{},[&](bool error,std::string_view bytes){std::lock_guard lock(observed_mutex);if(observed.size()<128)observed.push_back({{"elapsed_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-launched).count()},{"stderr_channel",error},{"data",std::string(bytes.substr(0,8192))},{"truncated",bytes.size()>8192}});});const bool marker_exists=std::filesystem::is_regular_file(folder/marker);
    const auto marker_content=marker_exists?read(folder/marker):std::string{};
    const auto marker_directory_id=marker_exists?ForegroundProcess::directory_identity(marker_content):std::string{};
    Json report={{"scenario",scenario},{"control",control},{"command",command},{"timeout_ms",30000},{"working_directory",config.working_directory},{"working_directory_id",config.working_directory_id},{"pid",result.pid},{"exit_code",result.exit_code},{"elapsed_ms",result.elapsed_ms},{"termination",result.termination==ProcessTermination::exited?"exited":result.termination==ProcessTermination::timed_out?"timed_out":"cancelled"},{"stdout",result.stdout_bytes},{"stderr",result.stderr_bytes},{"stdout_bytes",result.stdout_count},{"stderr_bytes",result.stderr_count},{"truncated",result.truncated},{"marker_exists",marker_exists},{"marker_content",marker_content},{"marker_directory_id",marker_directory_id},{"scope","Native foreground adapter diagnostic only"}};
    report["output_observations"]=std::move(observed);
    // Observe only after the original first command. This cannot warm its
    // command discovery; subsequent comparison commands are already labelled warm.
    try{
        auto probe=config;probe.timeout=std::chrono::milliseconds(5000);probe.arguments={"-NoLogo","-NoProfile","-NonInteractive","-Command","[Console]::Out.Write([Environment]::GetEnvironmentVariable('PSModulePath'))"};
        const auto actual=ForegroundProcess::run(probe);report["module_path_probe"]={{"success",actual.exit_code==0&&actual.termination==ProcessTermination::exited&&!actual.truncated},{"effective_path",actual.stdout_bytes},{"stderr",actual.stderr_bytes},{"elapsed_ms",actual.elapsed_ms}};
    }catch(...){report["module_path_probe"]={{"success",false}};}
    if(!copied.empty()){
        for(const auto& file:copied){const auto relative=std::filesystem::u8path(file.at("relative_path").get<std::string>());if(digest(home/L"Modules"/relative)!=file.at("sha256").get<std::string>()||digest(core/relative)!=file.at("sha256").get<std::string>())throw std::runtime_error("Diagnostic module inputs changed during command");}
        report["core_module_copy"]={{"root",utf8(core.native())},{"files",copied},{"unchanged",true},{"search_path_overridden",control=="core-only"}};
    }
    std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
