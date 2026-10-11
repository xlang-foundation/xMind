#include "agentflow/xlang_authoring_program.hpp"
#include "xlang3/cpp/runtime.h"
#include "nlohmann/json.hpp"
#include <cstddef>
#include <stdexcept>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t source_limit=256*1024;

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

struct ProgramApi {
    X3Runtime* runtime=nullptr;
    Json agents=Json::array(),graphs=Json::array(),tools=Json::array();
    std::size_t bytes=0;
    std::string Encode(const X::Value& value){
        if(!runtime||value.runtime()!=runtime)throw std::invalid_argument("Authoring value belongs to another xlang3 runtime");
        X::Runtime borrowed(runtime);
        X::Module json(borrowed,"json");
        X::Value encoded;
        if(!json.fn("dumps").Call(std::vector<X::Value>{value},encoded)||!encoded.IsString())
            throw std::invalid_argument("Authoring definitions must be JSON-serializable");
        const auto source=encoded.ToString();
        if(source.empty()||source.size()>source_limit-bytes)throw std::invalid_argument("Authoring program output exceeds limits");
        Json parsed;
        try{parsed=Json::parse(source);}catch(...){throw std::invalid_argument("Authoring definition is not valid JSON");}
        if(!parsed.is_object())throw std::invalid_argument("Authoring definitions must be objects");
        bytes+=source.size();
        return source;
    }
    void Agent(X::Value value){
        if(agents.size()>=64)throw std::invalid_argument("Authoring program accepts at most 64 agents");
        agents.push_back(Json::parse(Encode(value)));
    }
    void Graph(X::Value value){
        if(graphs.size()>=16)throw std::invalid_argument("Authoring program accepts at most 16 graphs");
        graphs.push_back(Json::parse(Encode(value)));
    }
    void Tool(X::Value value){
        if(tools.size()>=32)throw std::invalid_argument("Authoring program accepts at most 32 Python tools");
        tools.push_back(Json::parse(Encode(value)));
    }
    BEGIN_PACKAGE(ProgramApi)
        APISET().AddFunc<1>("agent",&ProgramApi::Agent);
        APISET().AddFunc<1>("graph",&ProgramApi::Graph);
        APISET().AddFunc<1>("tool",&ProgramApi::Tool);
    END_PACKAGE
};
}

XlangAuthoringProgramOutput run_xlang_authoring_program(
    const std::string& modules_root,const std::string& stdlib_root,
    const std::string& code_root,const std::string& module,
    const std::string& function){
    if(modules_root.empty()||stdlib_root.empty()||code_root.empty()||
       modules_root.find('\0')!=std::string::npos||stdlib_root.find('\0')!=std::string::npos||
       code_root.find('\0')!=std::string::npos||!identifier(module,true)||!identifier(function))
        throw std::invalid_argument("Invalid trusted xlang3 authoring program binding");
    X::Runtime runtime;
    runtime.AddImportRoot(modules_root);runtime.AddImportRoot(stdlib_root);runtime.AddImportRoot(code_root);
    ProgramApi api;api.runtime=runtime.get();
    auto public_api=runtime.RegisterPackage("xmind",api);
    X::Module program(runtime,module.c_str());
    const auto configure=program[function];
    if(!configure.IsValid())throw std::runtime_error("Trusted xlang3 authoring module has no configure function");
    X::Value result;
    if(!configure.Call(std::vector<X::Value>{public_api},result))
        throw std::runtime_error("Trusted xlang3 authoring program failed");
    XlangAuthoringProgramOutput output;
    if(!api.agents.empty())output.agents_json=Json{{"agents",std::move(api.agents)}}.dump();
    if(!api.graphs.empty())output.graphs_json=Json{{"graphs",std::move(api.graphs)}}.dump();
    if(!api.tools.empty())output.python_tools_config_json=Json{{"code_root",code_root},{"tools",std::move(api.tools)}}.dump();
    return output;
}
}
