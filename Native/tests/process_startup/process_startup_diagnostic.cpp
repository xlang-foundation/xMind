// A controlled diagnostic of the actual native adapter. No model, persistence,
// profile authority or agent-worker acceptance is implied by these launches.
#include "agentflow/process.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace agentflow;using Json=nlohmann::json;
namespace {
std::string utf8(const std::wstring& value){const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(!size)throw std::runtime_error("Diagnostic path conversion failed");std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);return result;}
std::string read(const std::filesystem::path& file){std::ifstream stream(file,std::ios::binary);if(!stream)throw std::runtime_error("Diagnostic marker could not be read");return {std::istreambuf_iterator<char>(stream),{}};}
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
    else throw std::invalid_argument("Unknown diagnostic scenario");
    if(control=="system-modules")config.environment.emplace_back("PSModulePath",utf8((home/L"Modules").native()));
    else if(control=="private-cache")config.environment.emplace_back("PSModuleAnalysisCachePath",utf8((folder/L"ModuleAnalysisCache").native()));
    else if(control=="cache-off")config.environment.emplace_back("PSModuleAnalysisCachePath","nul");
    else if(control!="default")throw std::invalid_argument("Unknown diagnostic control");
    if(std::filesystem::exists(folder/marker))throw std::invalid_argument("Diagnostic marker must not preexist");
    config.arguments={"-NoLogo","-NoProfile","-NonInteractive","-Command",command};
    const auto result=ForegroundProcess::run(config);const bool marker_exists=std::filesystem::is_regular_file(folder/marker);
    const auto marker_content=marker_exists?read(folder/marker):std::string{};
    const auto marker_directory_id=marker_exists?ForegroundProcess::directory_identity(marker_content):std::string{};
    Json report={{"scenario",scenario},{"control",control},{"command",command},{"timeout_ms",30000},{"working_directory",config.working_directory},{"working_directory_id",config.working_directory_id},{"pid",result.pid},{"exit_code",result.exit_code},{"elapsed_ms",result.elapsed_ms},{"termination",result.termination==ProcessTermination::exited?"exited":result.termination==ProcessTermination::timed_out?"timed_out":"cancelled"},{"stdout",result.stdout_bytes},{"stderr",result.stderr_bytes},{"stdout_bytes",result.stdout_count},{"stderr_bytes",result.stderr_count},{"truncated",result.truncated},{"marker_exists",marker_exists},{"marker_content",marker_content},{"marker_directory_id",marker_directory_id},{"scope","Native foreground adapter diagnostic only"}};
    std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
