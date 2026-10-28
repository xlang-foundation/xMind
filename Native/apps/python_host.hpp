#pragma once
#include "xlang3/cpp/runtime.h"
#include <string>
#include <vector>

namespace agentflow {
// Supply the import path and argv for this embedded application, just as a
// language executable supplies them. Runtime/platform identity is unchanged.
inline void configure_python_host(X::Runtime& runtime,const std::string& package,
                                  const std::vector<std::string>& arguments,
                                  const std::string& libraries={}){
    auto paths=runtime.List();
    for(const auto& path:{libraries,package+"/modules",package+"/stdlib"}){
        if(path.empty())continue;
        runtime.AddImportRoot(path);if(!paths.Append(X::Value(runtime,path)))throw std::runtime_error("Cannot initialize embedded import paths");
    }
    auto argv=runtime.List();for(const auto& item:arguments)if(!argv.Append(X::Value(runtime,item)))throw std::runtime_error("Cannot initialize embedded arguments");
    X::Module system(runtime,"sys");
    if(!system.SetAttr("path",paths)||!system.SetAttr("argv",argv)||!system.SetAttr("dont_write_bytecode",X::Value(runtime,true)))throw std::runtime_error("Cannot initialize embedded Python host");
}
}
