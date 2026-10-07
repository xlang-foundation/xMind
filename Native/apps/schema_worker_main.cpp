#include "agentflow/json_schema.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <optional>
#if defined(_WIN32)
#include <windows.h>
#endif
int main(int argc,char**) {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#endif
    if(argc!=1)return 2;
    using namespace agentflow;using Json=nlohmann::json;
    try {
        std::string bytes;char value;
        while(std::cin.get(value)) {if(value=='\n')break;if(bytes.size()>=1024*1024)return 2;bytes.push_back(value);}
        if(!std::cin || bytes.empty())return 2;
        std::optional<McpWireMessage> request;McpLineStream wire([&](const auto& message){if(request)throw McpProtocolError("Duplicate private schema request");request=message;});wire.feed(bytes+"\n");wire.finish();
        if(!request || request->kind!=McpMessageKind::request || request->method!="schema/validate")return 2;
        const auto args=Json::parse(request->payload_json);
        if(!args.contains("schema_json") || !args["schema_json"].is_string() || (args.contains("instance_json") && !args["instance_json"].is_string()) || args.size()!=(args.contains("instance_json")?2:1))return 2;
        Json reply{{"jsonrpc","2.0"},{"id",Json::parse(request->id_json)}};
        try {
            JsonSchema schema(args["schema_json"].get<std::string>());
            if(args.contains("instance_json"))schema.validate_object(args["instance_json"].get<std::string>());
            reply["result"]={{"schema_valid",true},{"instance_valid",args.contains("instance_json")?Json(true):Json(nullptr)}};
        }catch(const SchemaInvalid&){reply["error"]={{"code",-32602},{"message","Schema document rejected"},{"data",{{"kind","schema_invalid"}}}};}
        catch(const SchemaArgumentsInvalid&){reply["error"]={{"code",-32602},{"message","Schema arguments rejected"},{"data",{{"kind","arguments_invalid"}}}};}
        std::cout<<reply.dump()<<'\n';std::cout.flush();return std::cout?0:2;
    }catch(...){return 2;}
}
