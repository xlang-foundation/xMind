#include "agentflow/runtime_generation.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

// Actual Windows identity/handle verification. The Node peer supplies synthetic
// package metadata and inert fixture files; no xMind agent or SDK compatibility
// result is inferred from those hashes. Self verification uses this loaded C++
// process image copied into its owned fixture generation.
int wmain(int argc,wchar_t** argv){
    try {
        if(argc<4||argc>5)return 2;std::vector<std::string> arguments;
        for(int i=1;i<argc;++i){const std::wstring input=argv[i];if(input.empty()||input.size()>32768)return 2;const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),nullptr,0,nullptr,nullptr);if(count<=0)return 2;std::string value(count,'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),value.data(),count,nullptr,nullptr)!=count)return 2;arguments.push_back(std::move(value));}
        const auto mode=arguments[2];auto verified=mode=="managed"||mode=="managedhold"?agentflow::VerifiedRuntimeGeneration::managed_profile_copy(arguments[0],arguments[1],arguments.size()==4?arguments[3]:std::string{}):agentflow::VerifiedRuntimeGeneration(arguments[0],arguments[1],arguments.size()==4?arguments[3]:std::string{});const auto& binding=verified.binding();if(mode=="self"||mode=="current")verified.require_current_server();else if(mode=="loaded"||mode=="managed"||mode=="managedhold")verified.require_loaded_server_image();else if(mode!="verify"&&mode!="hold")return 2;
        std::cout<<nlohmann::json{{"verified",true},{"root",binding.root},{"root_identity",binding.root_identity},{"manifest_sha256",binding.manifest_sha256},{"self_verified",mode=="self"}}.dump()<<std::endl;
        if(mode=="hold"||mode=="managedhold"){std::string command;while(std::getline(std::cin,command)){if(command=="quit")break;if(command!="revalidate")return 2;verified.revalidate();std::cout<<"{\"revalidated\":true}"<<std::endl;}}
        return 0;
    }catch(...){std::cout<<"{\"verified\":false,\"detail\":\"Native generation rejected\"}"<<std::endl;return 3;}
}
