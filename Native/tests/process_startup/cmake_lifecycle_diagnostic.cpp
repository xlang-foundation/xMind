// Fresh, isolated reproduction of configure/build lifecycle through the real
// foreground adapter. Never execute against a user project or replay its runs.
#include "agentflow/process.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace agentflow;
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};
}
void write(const std::filesystem::path& path,const char* text) {
    std::ofstream stream(path,std::ios::binary);stream<<text;
    if(!stream)throw std::runtime_error("Cannot write diagnostic input");
}
}
int main(int argc,char** argv) {
    if(argc!=3&&argc!=4)return 2;
    try {
        const std::string control=argc==4?argv[3]:"default";
        if(control!="default"&&control!="foreground-build-servers"&&control!="release-trycompile")throw std::runtime_error("Unknown diagnostic control");
        const auto cmake=std::filesystem::canonical(std::filesystem::u8path(argv[1]));
        const auto root=std::filesystem::canonical(std::filesystem::u8path(argv[2]));
        if(!std::filesystem::is_directory(root)||!std::filesystem::is_empty(root))
            throw std::runtime_error("Diagnostic requires an existing empty directory");
        const auto source=root/"source",build=root/"build";
        std::filesystem::create_directory(source);
        write(source/"CMakeLists.txt","cmake_minimum_required(VERSION 3.24)\nproject(xmind_lifecycle_probe LANGUAGES CXX)\nadd_executable(probe main.cpp)\n");
        write(source/"main.cpp","int main(){return 0;}\n");
        ProcessConfiguration config;
        config.executable=utf8(cmake);config.executable_id=ForegroundProcess::executable_identity(config.executable);
        config.working_directory=utf8(root);config.working_directory_id=ForegroundProcess::directory_identity(config.working_directory);
        config.workspace_root=config.working_directory;config.workspace_root_id=config.working_directory_id;
        config.timeout=std::chrono::milliseconds(120000);config.output_limit=1024*1024;
        // Diagnostic-only control. Preserve full debug information in objects
        // instead of a compiler PDB server; do not keep reusable MSBuild nodes.
        // No global environment or user process profile is changed.
        if(control=="foreground-build-servers")config.environment={{"_CL_","/Z7"},{"MSBUILDDISABLENODEREUSE","1"}};
        if(control=="release-trycompile")config.environment={{"MSBUILDDISABLENODEREUSE","1"}};
        nlohmann::json report={{"scope","Fresh native foreground CMake lifecycle diagnostic; no agent/model/IPC acceptance"},
            {"control",control},{"environment",config.environment},
            {"working_directory",config.working_directory},{"executable",config.executable},
            {"executable_identity",config.executable_id},{"timeout_ms",config.timeout.count()},{"steps",nlohmann::json::array()}};
        bool passed=true;
        for(const auto* phase:{"configure","build"}) {
            config.arguments=std::string_view(phase)=="configure"?
                std::vector<std::string>{"-S",utf8(source),"-B",utf8(build),"-G","Visual Studio 18 2026","-A","x64"}:
                std::vector<std::string>{"--build",utf8(build),"--config","Release","--parallel","2"};
            if(control=="release-trycompile"&&std::string_view(phase)=="configure")config.arguments.push_back("-DCMAKE_TRY_COMPILE_CONFIGURATION=Release");
            std::cerr<<"Starting "<<phase<<'\n'<<std::flush;
            const auto result=ForegroundProcess::run(config);
            report["steps"].push_back({{"phase",phase},{"arguments",config.arguments},{"pid",result.pid},
                {"termination",result.termination==ProcessTermination::exited?"exited":result.termination==ProcessTermination::timed_out?"timed_out":"cancelled"},
                {"exit_code",result.exit_code},{"elapsed_ms",result.elapsed_ms},{"stdout",result.stdout_bytes},
                {"stderr",result.stderr_bytes},{"stdout_bytes",result.stdout_count},{"stderr_bytes",result.stderr_count},{"truncated",result.truncated}});
            if(result.termination!=ProcessTermination::exited||result.exit_code!=0){passed=false;break;}
        }
        const bool artifact=std::filesystem::is_regular_file(build/"Release"/"probe.exe");
        passed=passed&&artifact;report["passed"]=passed;report["build_artifact_exists"]=artifact;
        std::cout<<report.dump(2)<<'\n';return passed?0:1;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
