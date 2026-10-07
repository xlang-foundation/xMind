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
    explicit ProviderHttpError(int code):TransportError("Provider returned HTTP "+std::to_string(code)),status(code) {}
};
struct HttpStreamRequest {
    std::string url,body;
    std::chrono::milliseconds deadline{120000},idle_timeout{60000};
};
// Platform transport, outside the core. HTTPS uses OS certificate verification;
// unencrypted HTTP is accepted only for explicitly configured loopback endpoints.
// No redirect, implicit credential forwarding, or automatic retry.
void post_event_stream(const HttpStreamRequest& request,const SecretBytes* bearer,
    const std::function<void(std::string_view)>& consume,std::stop_token cancel={});
}
