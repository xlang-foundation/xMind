#pragma once
#include "agentflow/secret_protection.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <optional>

namespace agentflow {
struct TransportError : std::runtime_error {using std::runtime_error::runtime_error;};
struct TransportCancelled : TransportError {using TransportError::TransportError;};
struct TransportTimeout : TransportError {using TransportError::TransportError;};
struct ProviderHttpError : TransportError {
    int status;
    int json_rpc_code=0;
    bool a2a_version_not_supported=false,a2a_supports_legacy_03=false;
    std::string type,code,param;
    explicit ProviderHttpError(int code):TransportError("Provider returned HTTP "+std::to_string(code)),status(code) {}
};
// Backend-selected credential placement; callers cannot supply arbitrary headers.
enum class CredentialHeader {bearer,x_api_key,x_goog_api_key};
enum class ProviderHttpProtocol {generic,anthropic,a2a_jsonrpc};
// Backend-selected remote JSON binding, never arbitrary caller headers.
struct RemoteHttpBinding {std::string server_id,scope_id,grant_id;std::int64_t protocol_version=1,grant_revision=0;};
struct HttpStreamRequest {
    std::string url,body;
    std::chrono::milliseconds deadline{120000},idle_timeout{60000};
    CredentialHeader credential_header=CredentialHeader::bearer;
    ProviderHttpProtocol protocol=ProviderHttpProtocol::generic;
    std::string a2a_version;
    // Optional SSE reconnection cursor, selected by the trusted A2A owner.
    // It is never populated from arbitrary caller headers.
    std::string last_event_id;
    std::function<void(int,const std::string&,const std::string&)> observe_response;
    // Runs immediately before the native transport begins a request body send.
    // Effect callers use this boundary to distinguish a known pre-dispatch
    // stop from a possibly delivered remote request.
    std::function<void()> on_sending;
    std::optional<RemoteHttpBinding> remote_binding;
};
// Platform transport, outside the core. HTTPS uses OS certificate verification;
// unencrypted HTTP is accepted only for explicitly configured loopback endpoints.
// No redirect, implicit credential forwarding, or automatic retry.
void post_event_stream(const HttpStreamRequest& request,const SecretBytes* bearer,
    const std::function<void(std::string_view)>& consume,std::stop_token cancel={});
// Bounded JSON discovery through the same certificate, redirect and deadline policy.
std::string get_json(const HttpStreamRequest& request,const SecretBytes* bearer,std::stop_token cancel={});
std::string get_bounded_json(const HttpStreamRequest& request,const SecretBytes* bearer,
    std::size_t max_response_bytes,std::stop_token cancel={});
// Bounded non-streaming provider POST through the same transport policy. The
// native API adapter must select a nonempty serialized body and an explicit
// response byte limit (1..8 MiB), and validate the returned protocol JSON.
// This transport preserves response bytes; it does not interpret opaque state.
std::string post_json(const HttpStreamRequest& request,const SecretBytes* bearer,
    std::size_t max_response_bytes,std::stop_token cancel={});
// Native form POST with no provider credentials or caller-selected headers.
// Body and returned JSON bytes stay in wiping buffers; request.body must be empty.
// OAuth callers additionally require HTTPS and validate issuer/resource binding.
SecretBytes post_form_json(const HttpStreamRequest& request,const SecretBytes& form,
    std::size_t max_response_bytes,std::stop_token cancel={});
}
