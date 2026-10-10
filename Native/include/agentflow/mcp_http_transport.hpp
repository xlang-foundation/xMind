#pragma once
#include "agentflow/mcp_http_metadata.hpp"
#include "agentflow/secret_protection.hpp"
#include <chrono>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace agentflow {
struct McpHttpPost {
    std::string url,body;
    McpWireEra era=McpWireEra::modern;
    std::string legacy_protocol="2025-11-25";
    std::optional<std::string> legacy_session,input_schema;
    std::chrono::milliseconds deadline{120000},idle_timeout{60000};
};
struct McpHttpResponseHead {
    int status=0;
    std::string media_type;
    std::optional<std::string> legacy_session,authenticate;
};
// Native POST byte transport. Headers are derived from the actual request and
// trusted discovered schema. No arbitrary headers, redirect, retry or dispatch
// outcome inference. on_sending marks the conservative possibly-sent boundary;
// the approval owner still owns admission, correlation and effect receipts.
// Response metadata/body are backend-private untrusted protocol input.
void post_mcp_http(const McpHttpPost& request,const SecretBytes* bearer,
    const std::function<void(const McpHttpResponseHead&)>& on_head,
    const std::function<void(std::string_view)>& consume,
    const std::function<void()>& on_sending,std::stop_token cancel={});
}
