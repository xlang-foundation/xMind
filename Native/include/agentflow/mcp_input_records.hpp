#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace agentflow {
struct McpInputRequestSpec {
    std::string id,run_id,operation_id,server_config_id,peer_tool,request_key,request_json;
    std::int64_t config_revision=0,expires_unix_ms=0;
};
struct McpInputRequest {
    std::string id,run_id,operation_id,server_config_id,peer_tool,request_key,request_json,state,response_json,actor;
    std::int64_t config_revision=0,expires_unix_ms=0,published_event_seq=0;
    std::optional<std::int64_t> answered_event_seq;
};
}
