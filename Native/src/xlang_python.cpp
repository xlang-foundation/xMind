#include "agentflow/xlang_python.hpp"
#include "xlang3/cpp/runtime.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <stdexcept>

namespace agentflow {
namespace {
using Json=nlohmann::json;
bool identifier(const std::string& value,bool dotted=false){
    if(value.empty()||value.size()>256)return false;
    bool start=true;
    for(const unsigned char c:value){
        if(dotted&&c=='.'){if(start)return false;start=true;continue;}
        if(start){if(!(c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z')))return false;start=false;}
        else if(!(c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')))return false;
    }
    return !start;
}
void cancelled(std::stop_token token){if(token.stop_requested())throw XlangPythonCancelled();}
}

XlangPythonCallable::XlangPythonCallable(std::string modules_root,std::string stdlib_root,
    std::string code_root,std::string module,std::string function)
    :modules_root_(std::move(modules_root)),stdlib_root_(std::move(stdlib_root)),
     code_root_(std::move(code_root)),module_(std::move(module)),function_(std::move(function)){
    if(modules_root_.empty()||stdlib_root_.empty()||code_root_.empty()||
        modules_root_.find('\0')!=std::string::npos||stdlib_root_.find('\0')!=std::string::npos||
        code_root_.find('\0')!=std::string::npos||!identifier(module_,true)||!identifier(function_))
        throw std::invalid_argument("Invalid trusted xlang3 callable binding");
}

std::string XlangPythonCallable::invoke_json(const std::string& arguments_json,std::stop_token cancel)const{
    if(arguments_json.size()>256*1024)throw std::invalid_argument("xlang3 tool arguments exceed limits");
    Json arguments;
    try{arguments=Json::parse(arguments_json);}catch(...){throw std::invalid_argument("xlang3 tool arguments are not valid JSON");}
    if(!arguments.is_object())throw std::invalid_argument("xlang3 tool arguments must be a JSON object");
    cancelled(cancel);
    // Runtime and every xlang value live on the calling agent worker. No
    // interpreter or xlang object crosses threads or outlives this invocation.
    X::Runtime runtime;
    runtime.AddImportRoot(modules_root_);
    runtime.AddImportRoot(stdlib_root_);
    runtime.AddImportRoot(code_root_);
    X::Module json(runtime,"json","xlang_json");
    X::Value encoded;
    const auto input=arguments.dump();
    if(!json.fn("loads").Call(std::vector<X::Value>{X::Value(runtime,input)},encoded))
        throw XlangPythonExecutionError();
    cancelled(cancel);
    auto module=[&](){try{return X::Module(runtime,module_.c_str());}catch(...){throw XlangPythonExecutionError();}}();
    const auto callable=module[function_];
    if(!callable.IsValid())throw XlangPythonExecutionError();
    X::Value result;
    if(!callable.Call(std::vector<X::Value>{encoded},result))
        throw XlangPythonExecutionError();
    cancelled(cancel);
    X::Value output;
    if(!json.fn("dumps").Call(std::vector<X::Value>{result},output)||!output.IsString())
        throw XlangPythonExecutionError();
    auto serialized=output.ToString();
    if(serialized.size()>1024*1024)throw XlangPythonExecutionError();
    // The native side remains the protocol authority; reject non-JSON and
    // non-finite values even if the runtime serializer accepts them.
    try{(void)Json::parse(serialized);}catch(...){throw XlangPythonExecutionError();}
    return serialized;
}

}
