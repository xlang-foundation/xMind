#include "agentflow/schema_worker.hpp"
#include "agentflow/mcp_requests.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <algorithm>

namespace agentflow {
namespace {
using Json=nlohmann::json;
SchemaWorkerExecutable adjacent_worker() {
    std::wstring path(32768,L'\0');const auto count=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!count || count>=path.size())throw SchemaEvaluationFailure("Cannot locate the native schema worker");path.resize(count);
    const auto directory=std::filesystem::path(path).parent_path();
    const auto encode=[](const std::filesystem::path& value){const auto bytes=value.u8string();return std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());};
    return {encode(directory/"xmind_schema_worker.exe"),encode(directory),{}};
}
}
SchemaWorker::SchemaWorker(std::optional<SchemaWorkerExecutable> executable):executable_(executable?std::move(*executable):adjacent_worker()) {}
void SchemaWorker::evaluate(const std::string& schema,std::optional<std::string> instance,McpStdioProcess::Deadline requested,std::stop_token cancel) const {
    if(schema.empty() || schema.size()>256*1024)throw SchemaInvalid("MCP schema exceeds byte limits");
    if(instance && (instance->empty() || instance->size()>65536))throw SchemaArgumentsInvalid("MCP arguments exceed byte limits");
    if(cancel.stop_requested())throw SchemaEvaluationCancelled("Native schema evaluation cancelled before launch");
    const auto deadline=std::min(requested,std::chrono::steady_clock::now()+std::chrono::seconds(2));
    if(std::chrono::steady_clock::now()>=deadline)throw SchemaEvaluationFailure("Native schema evaluation deadline expired");
    try {
        McpStdioConfiguration configuration{executable_.executable,executable_.working_directory,executable_.arguments,{}};
        configuration.stdout_buffer_limit=8192;configuration.process_memory_limit=128*1024*1024;configuration.process_cpu_limit=std::chrono::seconds(2);configuration.active_process_limit=1;
        McpStdioProcess process(configuration);McpRequestTracker requests(1);std::optional<McpCorrelatedReply> reply;
        McpLineStream wire([&](const auto& message){if(reply)throw McpProtocolError("Duplicate native schema reply");reply=requests.receive(message);});
        Json params{{"schema_json",schema}};if(instance)params["instance_json"]=*instance;
        const auto request=requests.prepare("schema/validate",params.dump(),McpWireEra::legacy);process.write(request.frame,deadline,cancel);
        while(!reply){const auto bytes=process.read(deadline,cancel);if(!bytes){wire.finish();throw SchemaEvaluationFailure("Native schema worker exited without an outcome");}wire.feed(*bytes);}
        process.shutdown();if(process.status().exit_code!=0)throw SchemaEvaluationFailure("Native schema worker did not exit cleanly");
        const auto value=Json::parse(reply->response.payload_json);
        if(reply->response.kind==McpMessageKind::error) {
            if(value.value("code",0)!=-32602 || !value.contains("data") || !value["data"].is_object())throw SchemaEvaluationFailure("Invalid native schema error outcome");
            const auto kind=value["data"].value("kind",std::string{});
            if(kind=="schema_invalid")throw SchemaInvalid("Invalid JSON Schema 2020-12 document");
            if(kind=="arguments_invalid" && instance)throw SchemaArgumentsInvalid("Arguments do not match the MCP schema");
            throw SchemaEvaluationFailure("Invalid native schema outcome kind");
        }
        if(reply->response.kind!=McpMessageKind::result || value!=Json{{"schema_valid",true},{"instance_valid",instance?Json(true):Json(nullptr)}})throw SchemaEvaluationFailure("Invalid native schema success outcome");
    }catch(const SchemaInvalid&){throw;}catch(const SchemaArgumentsInvalid&){throw;}catch(const SchemaEvaluationFailure&){throw;}
    catch(const McpTransportCancelled&){throw SchemaEvaluationCancelled("Native schema evaluation cancelled");}
    catch(...){throw SchemaEvaluationFailure("Native schema worker failed or exceeded its budget");}
}
}
