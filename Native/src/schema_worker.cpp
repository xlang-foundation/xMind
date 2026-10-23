#include "agentflow/schema_worker.hpp"
#include "agentflow/schema_worker_service.hpp"
#include "xlang3/cpp/runtime.h"
#include "xlang3/abi/xapi.h"
#include <array>
#include <atomic>
#include <charconv>
#include <exception>
#include <thread>
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <algorithm>

namespace agentflow {
namespace {
SchemaWorkerExecutable adjacent_worker() {
    std::wstring path(32768,L'\0');const auto count=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!count || count>=path.size())throw SchemaEvaluationFailure("Cannot locate the native schema worker");path.resize(count);
    const auto directory=std::filesystem::path(path).parent_path();
    const auto encode=[](const std::filesystem::path& value){const auto bytes=value.u8string();return std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());};
    // The unified product launches its own bounded schema mode. Transitional
    // launchers/tests retain their adjacent helper until package migration.
    const auto image=std::filesystem::path(path);
    const auto leaf=image.filename().wstring();
    if(CompareStringOrdinal(leaf.data(),static_cast<int>(leaf.size()),L"xmind.exe",9,TRUE)==CSTR_EQUAL)
        return {encode(image),encode(directory),{"schema-worker"}};
    return {encode(directory/"xmind_schema_worker.exe"),encode(directory),{}};
}
}
SchemaWorker::SchemaWorker(std::optional<SchemaWorkerExecutable> executable):executable_(executable?std::move(*executable):adjacent_worker()) {}
void SchemaWorker::evaluate(const std::string& schema,std::optional<std::string> instance,McpStdioProcess::Deadline requested,std::stop_token cancel) const {
    if(schema.empty()||schema.size()>256*1024)throw SchemaInvalid("MCP schema exceeds byte limits");
    if(instance&&(instance->empty()||instance->size()>65536))throw SchemaArgumentsInvalid("MCP arguments exceed byte limits");
    if(cancel.stop_requested())throw SchemaEvaluationCancelled("Native schema evaluation cancelled before launch");
    const auto deadline=std::min(requested,std::chrono::steady_clock::now()+std::chrono::seconds(2));
    if(std::chrono::steady_clock::now()>=deadline)throw SchemaEvaluationFailure("Native schema evaluation deadline expired");
    try {
        McpStdioConfiguration configuration{executable_.executable,executable_.working_directory,executable_.arguments,{}};
        configuration.stdout_buffer_limit=8192;configuration.process_memory_limit=128*1024*1024;configuration.process_cpu_limit=std::chrono::seconds(2);configuration.active_process_limit=1;
        McpStdioProcess process(configuration);const auto pid=process.status().pid;
        std::array<unsigned char,32> secret{};
        if(BCryptGenRandom(nullptr,secret.data(),static_cast<ULONG>(secret.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw SchemaEvaluationFailure("Schema capability entropy unavailable");
        std::string capability;static constexpr char digits[]="0123456789abcdef";for(auto byte:secret){capability+=digits[byte>>4];capability+=digits[byte&15];}
        process.write(std::string_view(reinterpret_cast<const char*>(secret.data()),secret.size()),deadline,cancel);SecureZeroMemory(secret.data(),secret.size());
        std::string endpoint;
        while(endpoint.find('\n')==std::string::npos){const auto bytes=process.read(deadline,cancel);if(!bytes||endpoint.size()+bytes->size()>32)throw SchemaEvaluationFailure("Invalid schema endpoint bootstrap");endpoint+=*bytes;}
        if(!endpoint.ends_with("\n")||!endpoint.starts_with("lrpc:"))throw SchemaEvaluationFailure("Invalid schema endpoint bootstrap");endpoint.pop_back();
        if(!endpoint.empty()&&endpoint.back()=='\r')endpoint.pop_back();
        std::uint32_t port=0;const auto parsed=std::from_chars(endpoint.data()+5,endpoint.data()+endpoint.size(),port);if(parsed.ec!=std::errc{}||parsed.ptr!=endpoint.data()+endpoint.size()||!port||port>65535)throw SchemaEvaluationFailure("Invalid schema endpoint port");
        std::atomic<bool> done=false;std::exception_ptr failure;std::int64_t verdict=-1;
        std::thread invocation([&]{try{
            X::Runtime runtime;X3LrpcEndpointInfo info{};info.size=sizeof(info);
            if(x3_runtime_probe_remote(runtime.get(),endpoint.c_str(),1000,&info)!=X3_STATUS_OK||info.pid!=pid)throw SchemaEvaluationFailure("Schema endpoint PID differs");
            const auto call=[](const X::Value& method,std::vector<X::Value> args={}){X::Value value;if(!method.Call(args,value))throw SchemaEvaluationFailure("Schema SDK method failed; no replay authorized");return value;};
            auto root=runtime.ImportRemote("xmind",endpoint.c_str());auto services=call(root["Schemas"]);
            auto validator=call(services["Open"],{X::Value(runtime,capability)});
            const auto identity=call(validator["Identity"]);
            if(!identity.IsDict()||identity.Size()!=2||!identity["pid"].IsInt64()||identity["pid"].ToInt64()!=pid||!identity["version"].IsInt64()||identity["version"].ToInt64()!=1)throw SchemaEvaluationFailure("Schema validator identity differs");
            const auto value=call(validator["Validate"],{X::Value(runtime,schema),X::Value(runtime,instance.has_value()),X::Value(runtime,instance.value_or(""))});
            if(!value.IsInt64())throw SchemaEvaluationFailure("Invalid typed schema verdict");verdict=value.ToInt64();
        }catch(...){failure=std::current_exception();}done.store(true,std::memory_order_release);});
        try{
            while(!done.load(std::memory_order_acquire)){
                if(cancel.stop_requested())throw SchemaEvaluationCancelled("Native schema evaluation cancelled");
                if(std::chrono::steady_clock::now()>=deadline||process.status().exit_code)throw SchemaEvaluationFailure("Native schema worker failed or exceeded its budget");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            invocation.join();
            if(std::chrono::steady_clock::now()>=deadline)throw SchemaEvaluationFailure("Native schema evaluation deadline expired");
            process.shutdown();
        }catch(...){process.shutdown();if(invocation.joinable())invocation.join();throw;}
        SecureZeroMemory(capability.data(),capability.size());
        if(failure)std::rethrow_exception(failure);
        if(process.status().exit_code!=0)throw SchemaEvaluationFailure("Native schema worker did not exit cleanly");
        if(verdict==static_cast<std::int64_t>(SchemaWorkerVerdict::schema_invalid))throw SchemaInvalid("Invalid JSON Schema 2020-12 document");
        if(verdict==static_cast<std::int64_t>(SchemaWorkerVerdict::arguments_invalid)&&instance)throw SchemaArgumentsInvalid("Arguments do not match the MCP schema");
        if(verdict!=static_cast<std::int64_t>(SchemaWorkerVerdict::valid))throw SchemaEvaluationFailure("Invalid native schema outcome kind");
    }catch(const SchemaInvalid&){throw;}catch(const SchemaArgumentsInvalid&){throw;}catch(const SchemaEvaluationFailure&){throw;}
    catch(const McpTransportCancelled&){throw SchemaEvaluationCancelled("Native schema evaluation cancelled");}
    catch(...){throw SchemaEvaluationFailure("Native schema worker failed or exceeded its budget");}
}
}
