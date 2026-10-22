#define NOMINMAX
#include <windows.h>
#include "xlang3/xlang3.h"
#include "agent_worker_crash_fixture.hpp"
#include <atomic>
#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
struct Leaf {
    std::int64_t Read(){return 17;}
    BEGIN_PACKAGE(Leaf) APISET().AddFunc<0>("Read",&Leaf::Read); END_PACKAGE
};
struct Objects {BEGIN_PACKAGE(Objects) APISET().AddClass<0,Leaf>("Leaf"); END_PACKAGE};
Objects* objects=nullptr;
struct Root {
    X::Value Make(){return objects->__xlang3_package_->CreateInstance("Leaf",std::make_shared<Leaf>());}
    BEGIN_PACKAGE(Root) APISET().AddFunc<0>("Make",&Root::Make); END_PACKAGE
};
X::Value call(const X::Value& method){X::Value value;if(!method.Call(std::vector<X::Value>{},value))throw std::runtime_error("SDK call failed");return value;}
int main(int argc,char** argv){worker_crash_fixture::install();try{
    const int iterations=argc==2?std::stoi(argv[1]):1000;X::Runtime server;Objects owned;objects=&owned;Root root;
    (void)server.RegisterPackage("_objects",owned);auto package=server.RegisterPackage("xmind",root);server.RegisterRemoteObject("xmind",package);
    const auto port=30000+GetCurrentProcessId()%20000;server.LrpcListen(port);const std::string endpoint="lrpc:"+std::to_string(port);std::atomic<unsigned> completed=0,failed=0;std::barrier ready(8);std::vector<std::jthread> threads;
    for(int n=0;n<8;++n)threads.emplace_back([&]{try{X::Runtime client;auto remote=client.ImportRemote("xmind",endpoint.c_str());const auto make=remote["Make"];ready.arrive_and_wait();for(int i=0;i<iterations;++i){auto leaf=call(make);if(call(leaf["Read"]).ToInt64()!=17)throw std::runtime_error("SDK value changed");++completed;}}catch(const std::exception& e){++failed;std::cerr<<e.what()<<std::endl;}});
    for(auto& thread:threads)thread.join();std::cout<<"completed="<<completed<<" failed="<<failed<<std::endl;return failed||completed!=8*iterations?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
