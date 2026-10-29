#include "agentflow/program_entries.hpp"
#include "agentflow/owner_process.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <iostream>
#include <map>

int run_web(int argc,char** argv){
    try{
        agentflow::LocalProfileOptions options;std::map<std::string,bool> seen;
        for(int i=1;i<argc;++i){
            const std::string key=argv[i];if(!seen.emplace(key,true).second)throw std::invalid_argument("Duplicate browser option");
            if(key=="--upgrade-backend"){options.upgrade_backend=true;continue;}
            if(i+1>=argc||!*argv[i+1])throw std::invalid_argument("Browser option requires a value");
            const std::string value=argv[++i];
            if(key=="--workspace")options.workspace=value;
            else if(key=="--profile-root")options.profile_root=value;
            else if(key=="--assets")options.web_assets=value;
            else if(key=="--packages")options.web_packages=value;
            else if(key=="--config")options.provider_config=value;
            else if(key=="--graphs-config")options.graphs_config=value;
            else if(key=="--mcp-config")options.mcp_config=value;
            else if(key=="--process-config")options.process_config=value;
            else if(key=="--language-server-config")options.language_server_config=value;
            else if(key=="--agent-execution")options.agent_execution=value;
            else throw std::invalid_argument("Unknown browser option");
        }
        if(options.web_assets.empty())throw std::invalid_argument("Use xmind web --workspace DIR --assets DIR [--profile-root DIR]");
        options.web_assets=std::filesystem::absolute(std::filesystem::u8path(options.web_assets)).string();
        if(!std::filesystem::is_directory(std::filesystem::u8path(options.web_assets)))throw std::invalid_argument("Browser assets directory is unavailable");
        if(!options.web_packages.empty())options.web_packages=std::filesystem::absolute(std::filesystem::u8path(options.web_packages)).string();
        const auto connection=agentflow::connect_local_profile(options);
        const auto birth=agentflow::inspect_owner_process_birth(connection.process_id);
        const auto ready=std::filesystem::u8path(connection.directory)/("web-"+std::to_string(connection.process_id)+"-"+birth+".json");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
        for(;;){
            if(std::filesystem::exists(ready)){
                if(std::filesystem::file_size(ready)>16384)throw std::runtime_error("Browser metadata exceeds limits");
                // Windows can expose the renamed entry before its publication
                // handle closes. An unopened stream is not a JSON document.
                std::ifstream input(ready);
                if(input.is_open()){
                    const auto value=nlohmann::json::parse(input);
                    if(value.at("process_id").get<std::uint32_t>()!=connection.process_id||value.at("process_birth").get<std::string>()!=birth)throw std::runtime_error("Browser owner identity differs");
                    std::cout<<nlohmann::json{{"url",value.at("origin").get<std::string>()+"/ui/"},{"process_id",connection.process_id},{"process_birth",birth},{"profile_directory",connection.directory},{"transport","embedded-fastapi/xlang3-ipc"},{"windows_tray",value.value("windows_tray",false)}}.dump()<<std::endl;return 0;
                }
            }
            if(agentflow::observe_owner_exit(connection.process_id,birth,0).exited)throw std::runtime_error("The owned xMind browser host exited; inspect its retained startup log");
            if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Browser readiness is unresolved; reconnect to the same profile without starting a competing owner");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
