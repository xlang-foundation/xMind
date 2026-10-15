#include "agentflow/xlang_python.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <future>

int main(int argc,char** argv){
    if(argc!=5)return 2;
    try{
        agentflow::XlangPythonCallable callable(argv[2],argv[3],argv[4],"agent_worker_fixture","transform",argv[1]);
        const auto value=nlohmann::json::parse(callable.invoke_json_worker(R"({"text":"xMind","count":4})"));
        if(value!=nlohmann::json{{"result","worker:xMind"},{"count",5}})throw std::runtime_error("xlang3 worker returned the wrong result");
        agentflow::XlangPythonCallable reused(argv[2],argv[3],argv[4],"agent_worker_fixture","counter",argv[1]);
        const auto first=nlohmann::json::parse(reused.invoke_json_worker(R"({"value":"first"})"));
        const auto second=nlohmann::json::parse(reused.invoke_json_worker(R"({"value":"second"})"));
        if(first!=nlohmann::json{{"calls",1},{"max_active",1},{"value","first"}}||second!=nlohmann::json{{"calls",2},{"max_active",1},{"value","second"}})throw std::runtime_error("xlang3 tool worker was not reused across invocations");
        auto concurrent_a=std::async(std::launch::async,[&]{return nlohmann::json::parse(reused.invoke_json_worker(R"({"value":"parallel-a","delay_ms":150})"));});
        auto concurrent_b=std::async(std::launch::async,[&]{return nlohmann::json::parse(reused.invoke_json_worker(R"({"value":"parallel-b","delay_ms":150})"));});
        const auto parallel_a=concurrent_a.get(),parallel_b=concurrent_b.get();
        if(parallel_a["calls"]==parallel_b["calls"]||parallel_a["max_active"]!=1||parallel_b["max_active"]!=1)throw std::runtime_error("xlang3 tool worker did not serialize concurrent invocations");
        agentflow::XlangPythonCallable slow(argv[2],argv[3],argv[4],"agent_worker_fixture","slow",argv[1]);
        std::stop_source cancellation;std::jthread timer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(500));cancellation.request_stop();});
        const auto start=std::chrono::steady_clock::now();bool cancelled=false;
        try{(void)slow.invoke_json_worker("{}",cancellation.get_token());}catch(const agentflow::XlangPythonCancelled&){cancelled=true;}
        timer.join();
        if(!cancelled||std::chrono::steady_clock::now()-start>std::chrono::seconds(3))throw std::runtime_error("xlang3 worker cancellation did not retire the child promptly");
        std::cout<<"xlang3 callable ran through the unified xmind process over shared-memory IPC; repeated calls reused one worker and cancellation retired its worker.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
