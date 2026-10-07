// Native resource-budget fixture only. Does not validate a schema and is never
// the production worker. Modes exercise actual Windows job/commit/time limits.
#define NOMINMAX
#include <windows.h>
#include "nlohmann/json.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc<2)return 2;
    using Json=nlohmann::json;std::string line;std::getline(std::cin,line);const auto request=Json::parse(line);
    const std::string mode=argv[1];
    if(mode=="hang"){
        if(argc==3){std::ofstream marker(argv[2]);marker<<"actual native child entered evaluation fixture";marker.close();}
        std::atomic<std::uint64_t> work=0;for(;;)work.fetch_add(1,std::memory_order_relaxed);
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if(!QueryInformationJobObject(nullptr,JobObjectExtendedLimitInformation,&limits,sizeof(limits),nullptr))return 3;
    Json result{{"flags",limits.BasicLimitInformation.LimitFlags},{"memory_limit",limits.ProcessMemoryLimit},{"cpu_ticks",limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart},{"process_limit",limits.BasicLimitInformation.ActiveProcessLimit}};
    if(mode=="allocate"){
        std::vector<void*> allocations;bool refused=false;
        for(int i=0;i<64;++i){const auto value=VirtualAlloc(nullptr,1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!value){refused=true;break;}memset(value,1,1024*1024);allocations.push_back(value);}
        result["allocation_refused"]=refused;result["committed_fixture_bytes"]=allocations.size()*1024*1024;
        for(const auto value:allocations)VirtualFree(value,0,MEM_RELEASE);
    }else if(mode!="limits")return 2;
    std::cout<<Json{{"jsonrpc","2.0"},{"id",request["id"]},{"result",result}}.dump()<<'\n';std::cout.flush();return std::cout?0:2;
}
