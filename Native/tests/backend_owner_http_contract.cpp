#include "agentflow/backend_owner_control.hpp"
#include "agentflow/execution_platform.hpp"
#include "agentflow/http_server.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <thread>
#include <vector>
#include <fstream>
#include <filesystem>

// Real native SQLite/platform/HTTP; deployment metadata and ledger prompts are
// boundary fixtures. Retirement is a durable request only. No provider
// transport, retirement-triggered process shutdown or upgrade is claimed.
int wmain(int argc,wchar_t** argv){
    const char* stage="arguments";
    try{if(argc!=7)return 2;std::vector<std::string> args;for(int i=1;i<argc;++i){const std::wstring in=argv[i];const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,in.data(),static_cast<int>(in.size()),nullptr,0,nullptr,nullptr);if(n<=0)return 2;std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,in.data(),static_cast<int>(in.size()),out.data(),n,nullptr,nullptr);args.push_back(std::move(out));}
        stage="package";agentflow::VerifiedRuntimeGeneration verified(args[0],args[1],args[3]);stage="persistence";agentflow::PersistenceService store(args[2],{args[4],args[5]});stage="platform";agentflow::AgentSettings settings;settings.workspace=args[3];agentflow::ExecutionPlatform runtime(store,std::move(settings));stage="controller";agentflow::BackendOwnerControl control(store,runtime,verified,&runtime);const std::string token(64,'t');stage="http";try{agentflow::HttpServer wrong(store,token,nullptr,nullptr,{},{},{},nullptr,&runtime,nullptr,&control);throw std::runtime_error("Mismatched transport was accepted");}catch(const std::invalid_argument&){}agentflow::HttpServer server(store,token,&runtime,nullptr,{},{},{},nullptr,&runtime,nullptr,&control);stage="bind";const auto port=server.bind(0);std::thread listening([&]{server.listen();});std::cout<<nlohmann::json{{"origin","http://127.0.0.1:"+std::to_string(port)}}.dump()<<std::endl;
        std::string command;while(std::getline(std::cin,command)){if(command=="stop")break;if(command=="busy"){store.create_session("busy-session","synthetic owner ledger").get();store.start_prompt_run("busy-root","busy-session",R"({"content":"synthetic owned ledger prompt"})").get();std::cout<<"{\"busy_ledger\":true}"<<std::endl;}else if(command=="settle"){store.transition("busy-root",agentflow::RunState::queued,agentflow::RunState::cancelled,"{}").get();std::cout<<"{\"settled_ledger\":true}"<<std::endl;}else if(command=="retire-request"){
            const auto paused=control.status();const auto ws=runtime.execution_workspace();const agentflow::WorkspaceAdmission authority{ws.workspace_id,ws.authority_id};const agentflow::BackendOwnerReceipt receipt{paused.generation,paused.receipt_id,paused.revision};
            const auto rejects=[&](auto action){bool rejected=false;try{action();}catch(const agentflow::Conflict&){rejected=true;}if(!rejected)throw std::runtime_error("Expected native control rejection is absent");};
            rejects([&]{control.request_retirement(receipt,{ws.workspace_id,std::string(32,'e')});});auto wrong=receipt;++wrong.revision;rejects([&]{control.request_retirement(wrong,authority);});
            const auto requested=control.request_retirement(receipt,authority);rejects([&]{control.resume(receipt,authority);});rejects([&]{control.resume({requested.generation,requested.receipt_id,requested.revision},authority);});
            std::cout<<nlohmann::json{{"retirement_requested",requested.retirement_requested},{"revision",requested.revision}}.dump()<<std::endl;
        }}
        server.stop();listening.join();return 0;
    }catch(const std::exception& error){if(argc==7){std::ofstream diagnostic(std::filesystem::path(argv[3]).parent_path()/"private-fixture-error.txt");diagnostic<<stage<<'\n'<<error.what()<<'\n';}std::cerr<<"Native owner HTTP fixture failed at "<<stage<<'\n';return 1;}
}
