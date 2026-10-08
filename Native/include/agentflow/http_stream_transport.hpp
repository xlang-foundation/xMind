#pragma once
#include "agentflow/secret_protection.hpp"
#include <chrono>
#include <functional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>

namespace agentflow {
struct TransportError : std::runtime_error {using std::runtime_error::runtime_error;};
struct TransportCancelled : TransportError {using TransportError::TransportError;};
struct TransportTimeout : TransportError {using TransportError::TransportError;};
struct ProviderHttpError : TransportError {
    int status;
    std::string type,code,param;
    explicit ProviderHttpError(int code):TransportError("Provider returned HTTP "+std::to_string(code)),status(code) {}
};
// Backend-selected credential placement; callers cannot supply arbitrary headers.
enum class CredentialHeader {bearer,x_api_key,x_goog_api_key};
struct HttpStreamRequest {
    std::string url,body;
    std::chrono::milliseconds deadline{120000},idle_timeout{60000};
    CredentialHeader credential_header=CredentialHeader::bearer;
};
// Platform transport, outside the core. HTTPS uses OS certificate verification;
// unencrypted HTTP is accepted only for explicitly configured loopback endpoints.
// No redirect, implicit credential forwarding, or automatic retry.
void post_event_stream(const HttpStreamRequest& request,const SecretBytes* bearer,
    const std::function<void(std::string_view)>& consume,std::stop_token cancel={});
// Bounded JSON discovery through the same certificate, redirect and deadline policy.
std::string get_json(const HttpStreamRequest& request,const SecretBytes* bearer,std::stop_token cancel={});
}
