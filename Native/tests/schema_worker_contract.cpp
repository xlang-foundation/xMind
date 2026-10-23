#include "agentflow/schema_worker.hpp"
#include "agentflow/mcp_requests.hpp"
#include "nlohmann/json.hpp"
#include "xlang3/cpp/runtime.h"
#include <array>
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <future>
#include <iostream>
#include <random>
#include <thread>
using namespace agentflow;using namespace std::chrono_literals;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected worker rejection did not occur");}
struct Directory {
    std::filesystem::path parent=std::filesystem::temp_directory_path(),path=parent/("xmind-schema-budget-"+std::to_string(std::random_device{}()));
    Directory(){require(std::filesystem::create_directory(path),"Budget fixture directory must be newly created");}
    ~Directory(){if(path.parent_path()==parent && path.filename().string().starts_with("xmind-schema-budget-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
void sdk_contract(const std::string& executable){
    McpStdioConfiguration config{executable,std::filesystem::path(executable).parent_path().string(),{"sdk-success"},{}};
    config.process_memory_limit=128*1024*1024;config.process_cpu_limit=2s;config.active_process_limit=1;
    McpStdioProcess process(config);std::array<char,32> secret{};secret.fill('K');
    const auto deadline=std::chrono::steady_clock::now()+2s;process.write(std::string_view(secret.data(),secret.size()),deadline);
    std::string endpoint;while(endpoint.find('\n')==std::string::npos){const auto bytes=process.read(deadline);require(bool(bytes),"SDK schema fixture must publish its endpoint");endpoint+=*bytes;}
    endpoint.pop_back();
    if(!endpoint.empty()&&endpoint.back()=='\r')endpoint.pop_back();
    {
        X::Runtime runtime;auto root=runtime.ImportRemote("xmind",endpoint.c_str());
        const auto call=[](const X::Value& method,std::vector<X::Value> args={}){X::Value result;require(method.Call(args,result),"Actual schema SDK method failed");return result;};
        auto schemas=call(root["Schemas"]);X::Value ignored;
        require(!schemas["Open"].Call(std::vector<X::Value>{X::Value(runtime,std::string(64,'0'))},ignored),"Wrong schema capability must reject before validation");
        std::string capability;for(unsigned i=0;i<32;++i)capability+="4b";
        auto validator=call(schemas["Open"],{X::Value(runtime,capability)});
        const auto identity=call(validator["Identity"]);require(identity.IsDict()&&identity["pid"].ToInt64()==process.status().pid&&identity["version"].ToInt64()==1,"Schema SDK object must belong to the actual child");
        const std::vector<X::Value> args{X::Value(runtime,"{}"),X::Value(runtime,false),X::Value(runtime,"")};
        const auto result=call(validator["Validate"],args);require(result.IsInt64()&&result.ToInt64()==0,"Schema validation must return its typed SDK verdict");
        require(!validator["Validate"].Call(args,ignored),"A schema object must reject duplicate evaluation");
        auto second=call(schemas["Open"],{X::Value(runtime,capability)});require(!second["Validate"].Call(args,ignored),"Another SDK handle must not replay the child evaluation");
    }
    process.shutdown();require(process.status().exit_code==0,"Schema SDK fixture must exit cleanly after parent EOF");
}
void budgets(const std::string& executable,const std::string& mode) {
    McpStdioConfiguration configuration{executable,std::filesystem::path(executable).parent_path().string(),{mode},{}};
    configuration.process_memory_limit=32*1024*1024;configuration.process_cpu_limit=1500ms;configuration.active_process_limit=1;
    McpStdioProcess process(configuration);McpRequestTracker requests(1);std::optional<McpCorrelatedReply> reply;
    McpLineStream stream([&](const auto& value){reply=requests.receive(value);});const auto deadline=std::chrono::steady_clock::now()+5s;
    process.write(requests.prepare("fixture/budget","{}",McpWireEra::legacy).frame,deadline);
    while(!reply){const auto bytes=process.read(deadline);require(bool(bytes),"Native budget fixture exited before replying");stream.feed(*bytes);}
    process.shutdown();require(process.status().exit_code==0,"Native fixture must actually query/allocate under its job");
    const auto value=Json::parse(reply->response.payload_json);const auto flags=value["flags"].get<unsigned>();
    require(value["memory_limit"]==32*1024*1024 && value["cpu_ticks"]==15000000 && value["process_limit"]==1,"Actual child must observe configured native job budgets");
    require((flags&JOB_OBJECT_LIMIT_PROCESS_MEMORY) && (flags&JOB_OBJECT_LIMIT_PROCESS_TIME) && (flags&JOB_OBJECT_LIMIT_ACTIVE_PROCESS),"Native job budget flags must be enforced before child execution");
    if(mode=="allocate")require(value["allocation_refused"]==true && value["committed_fixture_bytes"]>0 && value["committed_fixture_bytes"]<32*1024*1024,"Actual committed allocations must be refused by the native memory budget");
}
}
int main(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="--unified-sdk"){try{
        SchemaWorker unified(SchemaWorkerExecutable{argv[2],std::filesystem::path(argv[2]).parent_path().string(),{"schema-worker"}});
        const std::string schema=R"({"type":"object","properties":{"value":{"type":"string"}},"required":["value"],"additionalProperties":false})";
        unified.evaluate(schema,{},std::chrono::steady_clock::now()+5s);
        unified.evaluate(schema,R"({"value":"\u96ea"})",std::chrono::steady_clock::now()+5s);
        rejects<SchemaArgumentsInvalid>([&]{unified.evaluate(schema,R"({"value":1})",std::chrono::steady_clock::now()+5s);});
        std::cout<<Json{{"schema_valid",true},{"unicode_instance_valid",true},{"invalid_instance_rejected",true},{"transport","xlang3-sdk"}}.dump()<<"\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
    if(argc!=2)return 2;
    try {
        sdk_contract(argv[1]);
        SchemaWorker production;const std::string schema=R"({"type":"object","properties":{"n":{"type":"integer","minimum":1}},"required":["n"],"additionalProperties":false})";
        production.evaluate(schema,{},std::chrono::steady_clock::now()+5s);production.evaluate(schema,R"({"n":1})",std::chrono::steady_clock::now()+5s);
        rejects<SchemaArgumentsInvalid>([&]{production.evaluate(schema,R"({"n":0})",std::chrono::steady_clock::now()+5s);});
        rejects<SchemaArgumentsInvalid>([&]{production.evaluate(schema,R"({"n":1,"n":2})",std::chrono::steady_clock::now()+5s);});
        rejects<SchemaInvalid>([&]{production.evaluate(R"({"properties":{"x":{"minLength":-1}}})",{},std::chrono::steady_clock::now()+5s);});
        rejects<SchemaInvalid>([&]{production.evaluate(R"({"$ref":"https://untrusted.invalid/private-schema"})",{},std::chrono::steady_clock::now()+5s);});
        // Adversarial real schema validation must produce a real rejection or
        // be contained; neither outcome constitutes successful validation.
        const auto started=std::chrono::steady_clock::now();bool refused=false;
        try{production.evaluate(R"({"type":"object","properties":{"s":{"type":"string","pattern":"^(a+)+$"}}})",Json{{"s",std::string(256,'a')+"!"}}.dump(),started+5s);}catch(const SchemaArgumentsInvalid&){refused=true;}catch(const SchemaEvaluationFailure&){refused=true;}
        require(refused && std::chrono::steady_clock::now()-started<4s,"Expensive real regex schema evaluation must not stall the owning backend");
        budgets(argv[1],"limits");budgets(argv[1],"allocate");
        const auto root=std::filesystem::path(argv[1]).parent_path().string();SchemaWorker stalled(SchemaWorkerExecutable{argv[1],root,{"hang"}});
        SchemaWorker malformed(SchemaWorkerExecutable{argv[1],root,{"bad-verdict"}});rejects<SchemaEvaluationFailure>([&]{malformed.evaluate("{}","{}",std::chrono::steady_clock::now()+5s);});
        const auto wall=std::chrono::steady_clock::now();rejects<SchemaEvaluationFailure>([&]{stalled.evaluate("{}","{}",wall+150ms);});require(std::chrono::steady_clock::now()-wall<3s,"Actual nonresponsive child must be retired after its wall deadline");
        Directory directory;const auto marker=(directory.path/"entered.marker").string();SchemaWorker cancellable(SchemaWorkerExecutable{argv[1],root,{"hang",marker}});
        std::stop_source cancel;auto pending=std::async(std::launch::async,[&]{cancellable.evaluate("{}","{}",std::chrono::steady_clock::now()+5s,cancel.get_token());});
        const auto entered=std::chrono::steady_clock::now()+1500ms;while(!std::filesystem::exists(marker) && std::chrono::steady_clock::now()<entered)std::this_thread::sleep_for(5ms);
        require(std::filesystem::exists(marker),"Cancellation fixture must observe actual child evaluation first");const auto stopped=std::chrono::steady_clock::now();cancel.request_stop();rejects<SchemaEvaluationCancelled>([&]{pending.get();});require(std::chrono::steady_clock::now()-stopped<3s,"Actual evaluating child cancellation must retire promptly");
        production.evaluate(schema,R"({"n":2})",std::chrono::steady_clock::now()+5s);
        std::cout<<"Native schema isolation passed actual worker validation, invalid schemas/arguments/no fetch, expensive regex rejection/containment, OS job budget queries, actual committed-memory refusal, stalled child timeout/cancellation and subsequent healthy evaluation. CPU flags are queried; no exact CPU timer scheduling or full schema conformance claim\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
