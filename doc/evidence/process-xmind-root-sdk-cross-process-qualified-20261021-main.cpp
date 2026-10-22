#define NOMINMAX
#include <windows.h>
#include "xlang3/xlang3.h"
#include "agent_worker_crash_fixture.hpp"
#include <iostream>
#include <string>
#include <vector>
struct Leaf {std::int64_t Read(){return 17;} BEGIN_PACKAGE(Leaf) APISET().AddFunc<0>("Read",&Leaf::Read); END_PACKAGE};
struct Objects {BEGIN_PACKAGE(Objects) APISET().AddClass<0,Leaf>("Leaf"); END_PACKAGE};
Objects* objects=nullptr;
struct Root {X::Value Make(){return objects->__xlang3_package_->CreateInstance("Leaf",std::make_shared<Leaf>());} BEGIN_PACKAGE(Root) APISET().AddFunc<0>("Make",&Root::Make); END_PACKAGE};
X::Value call(const X::Value& method){X::Value value;if(!method.Call(std::vector<X::Value>{},value))throw std::runtime_error("SDK call failed");return value;}
int main(int argc,char** argv){worker_crash_fixture::install();try{
    if(argc==2&&std::string(argv[1])=="server"){
        Objects owned;Root root;X::Runtime runtime;objects=&owned;
        (void)runtime.RegisterPackage("_objects",owned);auto package=runtime.RegisterPackage("xmind",root);runtime.RegisterRemoteObject("xmind",package);const auto port=30000+GetCurrentProcessId()%20000;runtime.LrpcListen(port);std::cout<<"READY "<<port<<" "<<GetCurrentProcessId()<<std::endl;std::string stop;std::getline(std::cin,stop);return 0;
    }
    if(argc==4&&std::string(argv[1])=="client"){
        const auto count=std::stoi(argv[3]);X::Runtime runtime;auto root=runtime.ImportRemote("xmind",argv[2]);const auto make=root["Make"];
        for(int i=0;i<count;++i){auto leaf=call(make);if(call(leaf["Read"]).ToInt64()!=17)throw std::runtime_error("SDK value changed");}
        std::cout<<"completed="<<count<<" pid="<<GetCurrentProcessId()<<std::endl;return 0;
    }
    return 2;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
