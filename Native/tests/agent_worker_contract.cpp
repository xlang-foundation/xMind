#include "agentflow/xlang_python.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>

int main(int argc,char** argv){
    if(argc!=5)return 2;
    try{
        agentflow::XlangPythonCallable callable(argv[2],argv[3],argv[4],"agent_worker_fixture","transform",argv[1]);
        const auto value=nlohmann::json::parse(callable.invoke_json_worker(R"({"text":"xMind","count":4})"));
        if(value!=nlohmann::json{{"result","worker:xMind"},{"count",5}})throw std::runtime_error("xlang3 worker returned the wrong result");
        agentflow::XlangPythonCallable slow(argv[2],argv[3],argv[4],"agent_worker_fixture","slow",argv[1]);
        std::stop_source cancellation;std::jthread timer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(500));cancellation.request_stop();});
        const auto start=std::chrono::steady_clock::now();bool cancelled=false;
        try{(void)slow.invoke_json_worker("{}",cancellation.get_token());}catch(const agentflow::XlangPythonCancelled&){cancelled=true;}
        timer.join();
        if(!cancelled||std::chrono::steady_clock::now()-start>std::chrono::seconds(3))throw std::runtime_error("xlang3 worker cancellation did not retire the child promptly");
        std::cout<<"xlang3 callable ran through the unified xmind process over shared-memory IPC.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
