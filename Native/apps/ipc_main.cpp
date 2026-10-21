#include "xlang3/xlang3.h"
#include "nlohmann/json.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace agentflow {
int run_xmind_ipc_client(int argc,char** argv){
    try{
        if(argc<4||std::string(argv[1])!="--endpoint")throw std::invalid_argument("Usage: xmind ipc --endpoint lrpc:PORT host | session create ID TITLE | session open ID | run open ID | run start ID SESSION PROMPT | run cancel ID");
        const std::string endpoint=argv[2];if(!endpoint.starts_with("lrpc:"))throw std::invalid_argument("xMind IPC requires an xlang3 LRPC endpoint");
        X::Runtime runtime;auto root=runtime.ImportRemote("xmind",endpoint.c_str());
        auto call=[&](const X::Value& method,std::vector<X::Value> args){X::Value result;if(!method.Call(args,result))throw std::runtime_error("xMind IPC call failed: "+runtime.LastError());return result;};
        auto text=[&](const char* value){return X::Value(runtime,std::string(value));};
        const std::string command=argv[3];nlohmann::json output;
        if(command=="host"&&argc==4){auto host=call(root["Host"],{});output={{"api_version",call(host["ApiVersion"],{}).ToInt64()},{"transport",call(host["Transport"],{}).ToString()}};}
        else{
            const auto* token=std::getenv("XMIND_AUTH_TOKEN");if(!token)throw std::invalid_argument("Set XMIND_AUTH_TOKEN for authenticated xMind IPC services");
            if(command!="session"&&command!="run")throw std::invalid_argument("Unknown xMind IPC service");auto service=call(root[command=="session"?"Sessions":"Runs"],{text(token)});X::Value result;
            if(command=="session"&&argc==7&&std::string(argv[4])=="create")result=call(service["Create"],{text(argv[5]),text(argv[6])});
            else if(argc==6&&std::string(argv[4])=="open")result=call(service["Open"],{text(argv[5])});
            else if(command=="run"&&argc==8&&std::string(argv[4])=="start")result=call(service["Start"],{text(argv[5]),text(argv[6]),text(argv[7])});
            else if(command=="run"&&argc==6&&std::string(argv[4])=="cancel"){call(service["Cancel"],{text(argv[5])});output={{"cancel_requested",true}};}
            else throw std::invalid_argument("Invalid xMind IPC service method or arguments");
            if(result.IsValid()){if(command=="session")output={{"id",result["id"].ToString()},{"title",result["title"].ToString()},{"parent_id",result["parent_id"].ToString()}};else output={{"id",result["id"].ToString()},{"session_id",result["session_id"].ToString()},{"state",result["state"].ToString()},{"parent_id",result["parent_id"].ToString()},{"node_id",result["node_id"].ToString()}};}
        }
        std::cout<<output.dump()<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
}
